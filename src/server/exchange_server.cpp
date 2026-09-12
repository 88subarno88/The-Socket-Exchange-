#include "../common/net_utils.hpp"
#include "../common/protocol.hpp"
#include "order_book.hpp"
#include "client_session.hpp"

#include <sys/types.h>
#include <sys/event.h>     // kqueue / kevent
#include <sys/socket.h>
#include <netinet/in.h>    // struct sockaddr_in
#include <arpa/inet.h>     // inet_ntop
#include <unistd.h>
#include <csignal>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>
#include <unordered_map>

// ---- Global-ish server state. For a single-threaded loop, plain locals in
//      main() are cleaner than globals; shown here grouped for clarity. --------
struct Server {
    int kq = -1;                                     // kqueue descriptor
    int listen_fd = -1;
    OrderBook book;
    std::unordered_map<int, ClientSession> sessions; // fd -> session

    unsigned long long next_session_seq = 1;

    // Register/unregister interest in a socket. Wrap EV_SET + kevent() so the
    // rest of the code reads clearly.
    void kq_add(int fd, int filter);      // filter = EVFILT_READ or EVFILT_WRITE
    void kq_del(int fd, int filter);
};

// QUIT has to tear down the session from inside handle_line, which is defined
// before close_session, so declare it up front.
static void close_session(Server& s, int fd);

static void enqueue(Server& s, ClientSession& c, const std::string& line) {
    // The '\n' is added HERE and nowhere else, so a line can never be double-framed.
    const std::string framed = line + "\n";

    // Something is already queued: appending is the only way to keep the
    // client's message order intact. Draining is on_writable's job.
    if (!c.outbuf.empty()) {
        c.outbuf.push_back(framed);
        return;
    }

    // Fast path: try to hand it straight to the kernel.
    size_t sent = 0;
    while (sent < framed.size()) {
        ssize_t n = send(c.fd, framed.data() + sent, framed.size() - sent, 0);
        if (n > 0) { sent += (size_t)n; continue; }
        if (n < 0 && errno == EINTR) continue;
        // The peer's receive window is full -- this is backpressure, not failure.
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        return;
    }

    if (sent < framed.size()) {
        c.outbuf.push_back(framed.substr(sent));
        if (!c.want_write) {
            s.kq_add(c.fd, EVFILT_WRITE);
            c.want_write = true;
        }
    }
}

static void broadcast_trade(Server& s, const std::string& instrument,
                            long long qty, long long price) {
    const std::string msg = proto::msg_trade(instrument, qty, price);

    for (auto& kv : s.sessions) {
        ClientSession& sub = kv.second;
        if (sub.role != Role::MARKET_DATA) continue;         // traders never get TRADE
        if (sub.subscriptions.count(instrument) == 0) continue;
        enqueue(s, sub, msg);
    }
}

static bool handle_line(Server& s, ClientSession& c, const std::string& line) {
    auto t = proto::tokenize(line);
    if (t.empty()) { enqueue(s, c, proto::msg_error("empty")); return true; }
    const std::string& cmd = t[0];

    const bool trader_cmd = (cmd == "LOGIN" || cmd == "BUY" ||
                             cmd == "SELL"  || cmd == "CANCEL");
    const bool md_cmd     = (cmd == "SUBSCRIBE" || cmd == "UNSUBSCRIBE");

    if (c.role == Role::UNKNOWN) {
        if (trader_cmd)   c.role = Role::TRADER;
        else if (md_cmd)  c.role = Role::MARKET_DATA;
    }

    // A command outside this connection's role is refused. This is what makes a
    // Market-Data client genuinely read-only.
    if (trader_cmd && c.role != Role::TRADER) {
        enqueue(s, c, proto::msg_error("market-data clients cannot send " + cmd));
        return true;
    }
    if (md_cmd && c.role != Role::MARKET_DATA) {
        enqueue(s, c, proto::msg_error("traders cannot send " + cmd));
        return true;
    }

    // ---- Handle each command. Validate arguments with proto:: helpers. ------
    if (cmd == "LOGIN") {
        if (t.size() != 2) { enqueue(s, c, proto::msg_error("usage: LOGIN <username>")); return true; }
        if (c.logged_in()) { enqueue(s, c, proto::msg_error("already logged in")); return true; }

        // "Unique among CURRENTLY CONNECTED trader clients" (handout 2.2.1) --
        // so a name frees up again once its owner disconnects.
        for (const auto& kv : s.sessions) {
            if (kv.first != c.fd && kv.second.username == t[1]) {
                enqueue(s, c, proto::msg_error("username already in use"));
                return true;
            }
        }
        c.username = t[1];
        enqueue(s, c, proto::msg_ok());

    } else if (cmd == "BUY" || cmd == "SELL") {
        if (t.size() != 4) {
            enqueue(s, c, proto::msg_error("usage: " + cmd + " <instrument> <quantity> <price>"));
            return true;
        }
        if (!proto::is_valid_instrument(t[1])) {
            enqueue(s, c, proto::msg_error("unknown instrument " + t[1]));
            return true;
        }
        long long qty = 0, price = 0;
        if (!proto::parse_positive_int(t[2], qty)) {
            enqueue(s, c, proto::msg_error("quantity must be an integer in 1..2147483647"));
            return true;
        }
        if (!proto::parse_positive_int(t[3], price)) {
            enqueue(s, c, proto::msg_error("price must be an integer in 1..2147483647"));
            return true;
        }

        Order o;
        o.id         = s.book.next_id();
        o.side       = (cmd == "BUY") ? Side::BUY : Side::SELL;
        o.instrument = t[1];
        o.qty        = qty;
        o.price      = price;
        o.owner_fd   = c.fd;
        o.owner_seq  = c.session_seq;

        // Handout 2.6: the order is ACCEPTED first, and only then matched.
        // ORDER_ACCEPTED says "I have your order", not "it traded".
        enqueue(s, c, proto::msg_order_accepted(o.id));

        for (const Fill& f : s.book.submit(o)) {
            auto buyer = s.sessions.find(f.buyer_fd);
            if (buyer != s.sessions.end() && buyer->second.session_seq == f.buyer_seq)
                enqueue(s, buyer->second, proto::msg_bought(f.instrument, f.qty, f.price));

            auto seller = s.sessions.find(f.sell_fd);
            if (seller != s.sessions.end() && seller->second.session_seq == f.sell_seq)
                enqueue(s, seller->second, proto::msg_sold(f.instrument, f.qty, f.price));

            broadcast_trade(s, f.instrument, f.qty, f.price);
        }

    } else if (cmd == "CANCEL") {
        if (t.size() != 2) {
            enqueue(s, c, proto::msg_error("usage: CANCEL <order_id>"));
            return true;
        }
        long long oid = 0;
        // NOTE: non-negative, NOT positive -- id 0 is the first order the server
        // ever issues and must be cancellable (handout 2.1).
        if (!proto::parse_nonnegative_int(t[1], oid)) {
            enqueue(s, c, proto::msg_error("order id must be an integer in 0..2147483647"));
            return true;
        }
        if (s.book.cancel(oid, c.fd, c.session_seq)) {
            // Success is ORDER_CANCELLED, not OK (handout 2.3).
            enqueue(s, c, proto::msg_order_cancelled(oid));
        } else {
            enqueue(s, c, proto::msg_error("cannot cancel order " + t[1]));
        }

    } else if (cmd == "SUBSCRIBE") {
        if (t.size() != 2) { enqueue(s, c, proto::msg_error("usage: SUBSCRIBE <instrument>")); return true; }
        if (!proto::is_valid_instrument(t[1])) {
            enqueue(s, c, proto::msg_error("unknown instrument " + t[1]));
            return true;
        }
        c.subscriptions.insert(t[1]);   // std::set -> subscribing twice is a no-op
        enqueue(s, c, proto::msg_ok());

    } else if (cmd == "UNSUBSCRIBE") {
        if (t.size() != 2) { enqueue(s, c, proto::msg_error("usage: UNSUBSCRIBE <instrument>")); return true; }
        if (!proto::is_valid_instrument(t[1])) {
            enqueue(s, c, proto::msg_error("unknown instrument " + t[1]));
            return true;
        }
        // Idempotent, mirroring SUBSCRIBE: unsubscribing something you never had
        // still answers OK. The handout doesn't specify; documented in report.pdf.
        c.subscriptions.erase(t[1]);
        enqueue(s, c, proto::msg_ok());

    } else if (cmd == "QUIT") {
        // Handout 2.3 lists OK as the reply to LOGIN/SUBSCRIBE/UNSUBSCRIBE only,
        // so QUIT gets no reply -- just an orderly close.
        close_session(s, c.fd);
        return false;               // `c` is gone; caller must not touch it

    } else {
        enqueue(s, c, proto::msg_error("unknown command"));
    }
    return true;
}

// Tear down a connection: unregister from kqueue and close the socket.
// NOTE: resting orders are deliberately LEFT IN THE BOOK. Handout 2.6 says a
// disconnect does not cancel them -- they can still match, and subscribed
// Market-Data clients still get the TRADE; only the departed trader misses its
// BOUGHT/SOLD. (OrderBook::remove_orders_of exists for CANCEL-style use and is
// intentionally NOT called here.)
static void close_session(Server& s, int fd) {
    auto it = s.sessions.find(fd);
    if (it == s.sessions.end()) return;   // already torn down -- never double-close

    // close() would remove the kqueue registrations by itself; doing it
    // explicitly keeps the connection lifecycle visible in the code.
    s.kq_del(fd, EVFILT_READ);
    if (it->second.want_write) s.kq_del(fd, EVFILT_WRITE);

    close(fd);
    s.sessions.erase(it);
    printf("[-] fd=%d closed (%zu client(s) still connected)\n", fd, s.sessions.size());
    fflush(stdout);
}

// Accept ALL pending connections (loop until accept() returns EAGAIN, because
// one EVFILT_READ on the listen socket can mean several queued connections).
static void accept_new(Server& s) {
    for (;;) {
        struct sockaddr_in peer;
        socklen_t plen = sizeof peer;
        int cfd = accept(s.listen_fd, (struct sockaddr*)&peer, &plen);
        if (cfd < 0) {
            if (errno == EAGAIN || errno == EWOULDBLOCK) break;  // queue drained
            if (errno == EINTR) continue;                        // signal, retry
            // The peer vanished between the SYN and our accept(). Not our
            // problem -- skip it and keep serving everyone else.
            if (errno == ECONNABORTED) continue;
            perror("accept");
            break;
        }

        // Non-blocking is mandatory: with a readiness-based loop, a blocking
        // recv()/send() on ONE socket would stall every other client.
        net::set_nonblocking(cfd);

        ClientSession& c = s.sessions[cfd];
        c.fd = cfd;
        c.session_seq = s.next_session_seq++;
        s.kq_add(cfd, EVFILT_READ);

        char ip[INET_ADDRSTRLEN] = {0};
        inet_ntop(AF_INET, &peer.sin_addr, ip, sizeof ip);
        printf("[+] accepted fd=%d from %s:%d (%zu client(s) connected)\n",
               cfd, ip, ntohs(peer.sin_port), s.sessions.size());
        fflush(stdout);
    }
}

static void on_readable(Server& s, int fd) {
    auto it = s.sessions.find(fd);
    if (it == s.sessions.end()) return;
    ClientSession& c = it->second;

    char tmp[4096];
    for (;;) {
        ssize_t n = recv(fd, tmp, sizeof tmp, 0);

        if (n > 0) {
            printf("[%d] recv() -> %zd byte(s)\n", fd, n);
            c.inbuf.feed(tmp, (size_t)n);

            std::string line;
            while (c.inbuf.next_line(line)) {
                printf("[%d] %s\n", fd, line.c_str());   // keep the trace for Exp. 3
                fflush(stdout);
                // QUIT destroys the session mid-loop. Stop at once: `c` and the
                // fd are both invalid from here on.
                if (!handle_line(s, c, line)) return;
            }
            fflush(stdout);
            continue;   // keep draining: the socket may hold more bytes
        }

        if (n == 0) {
            // Orderly shutdown: the peer sent FIN (Experiment 6 FIN case).
            printf("[%d] recv() -> 0 : peer closed (FIN)\n", fd);
            fflush(stdout);
            close_session(s, fd);
            return;                       // `c` is dangling now -- must not touch it
        }

        if (errno == EINTR) continue;                          // signal, retry
        if (errno == EAGAIN || errno == EWOULDBLOCK) break;    // fully drained

        if (errno == ECONNRESET) {
            // Abrupt teardown: the peer sent RST (Experiment 6 RST case).
            printf("[%d] recv() -> ECONNRESET : peer reset (RST)\n", fd);
            fflush(stdout);
        } else {
            perror("recv");
        }
        close_session(s, fd);
        return;
    }
}

// A socket is writable: flush queued output (backpressure drain, Experiment 7).
static void on_writable(Server& s, int fd) {
    auto it = s.sessions.find(fd);
    if (it == s.sessions.end()) return;
    ClientSession& c = it->second;

    while (!c.outbuf.empty()) {
        std::string& front = c.outbuf.front();
        ssize_t n = send(fd, front.data(), front.size(), 0);

        if (n > 0) {
            if ((size_t)n < front.size()) {
                front.erase(0, (size_t)n);   // still blocked: keep the remainder
                return;                       // stay armed for the next writable event
            }
            c.outbuf.pop_front();
            continue;
        }
        if (n < 0 && errno == EINTR) continue;
        if (n < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return;  // still full

        close_session(s, fd);   // hard error: the peer really is gone
        return;
    }

    // Drained. Disarm, or kevent() would report "writable" forever and spin.
    if (c.want_write) {
        s.kq_del(fd, EVFILT_WRITE);
        c.want_write = false;
    }
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <host> <port>\n", argv[0]); return 1; }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);

    // IMPORTANT: a client that vanishes can make send() raise SIGPIPE, which by
    // default kills the process. Ignore it and handle EPIPE via errno instead.
    signal(SIGPIPE, SIG_IGN);

    Server s;
    s.listen_fd = net::make_listen_socket(host, port, /*backlog=*/128);
    if (s.listen_fd < 0) return 1;
    net::set_nonblocking(s.listen_fd);

    s.kq = kqueue();
    if (s.kq < 0) { perror("kqueue"); close(s.listen_fd); return 1; }

    // Watch the listening socket. It becomes "readable" when a connection is
    // waiting to be accept()ed.
    s.kq_add(s.listen_fd, EVFILT_READ);

    printf("exchange_server listening on %s:%d (kqueue fd=%d, listen fd=%d)\n",
           host.c_str(), port, s.kq, s.listen_fd);
    fflush(stdout);

    struct kevent events[1024];
    for (;;) {
        int n = kevent(s.kq, nullptr, 0, events, 1024, nullptr);  // block
        if (n < 0) {
            if (errno == EINTR) continue;   // interrupted by a signal, not an error
            perror("kevent");
            break;
        }

        for (int i = 0; i < n; i++) {
            const int fd = (int)events[i].ident;

            if (events[i].flags & EV_ERROR) {
                fprintf(stderr, "kevent error on fd=%d: %s\n",
                        fd, strerror((int)events[i].data));
                if (fd != s.listen_fd) close_session(s, fd);
                continue;
            }

            if (fd == s.listen_fd) { accept_new(s); continue; }

            if (events[i].filter == EVFILT_READ) {
                on_readable(s, fd);
                // on_readable may already have torn the session down (FIN/RST).
                // Only then consider EV_EOF, so we never close the same fd twice.
                if (s.sessions.count(fd) && (events[i].flags & EV_EOF)) {
                    close_session(s, fd);
                }
            } else if (events[i].filter == EVFILT_WRITE) {
                on_writable(s, fd);   // Phase 9: backpressure drain
            }
        }
    }

    close(s.listen_fd);
    close(s.kq);
    return 0;
}

void Server::kq_add(int fd, int filter) {
    struct kevent ev;
    EV_SET(&ev, fd, filter, EV_ADD | EV_ENABLE, 0, 0, nullptr);
    if (kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0) {
        perror("kevent EV_ADD");
    }
}

void Server::kq_del(int fd, int filter) {
    struct kevent ev;
    EV_SET(&ev, fd, filter, EV_DELETE, 0, 0, nullptr);
    // ENOENT just means the filter was never armed (or close() already dropped
    // it). That is normal during teardown, so don't cry wolf about it.
    if (kevent(kq, &ev, 1, nullptr, 0, nullptr) < 0 && errno != ENOENT) {
        perror("kevent EV_DELETE");
    }
}
