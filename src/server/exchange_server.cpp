// ============================================================================
// exchange_server.cpp  --  The Exchange Server (handout 3.1).
//
// CONCURRENCY / I/O CHOICE (handout 4.7 + report 8.1):
//   This skeleton uses a SINGLE-THREADED EVENT LOOP built on kqueue() -- the
//   native FreeBSD I/O-multiplexing facility. One thread watches ALL sockets;
//   it only ever touches a socket the kernel says is ready, so an idle client
//   can never block progress on another (this is the point of Experiments 4/5),
//   and there is no per-connection thread cost (this is what makes the 70k
//   bonus feasible).
//
//   Alternatives you could justify instead:
//     * poll()  -- portable, simpler API, O(n) scan each loop. Fine for this
//                  assignment's scale; swap kevent() for poll() if you prefer.
//     * thread-per-connection -- simplest to reason about, but Experiment 4
//                  becomes trivially "no it doesn't stall" only if EACH thread
//                  blocks independently, and the 70k bonus will drown in thread
//                  memory. If you pick this, be ready to defend it in the viva.
//
// LEARN kqueue:
//   man 2 kqueue   (read the EXAMPLE section at the bottom -- it's excellent)
//   "Kqueue: A Generic and Scalable Event Notification Facility", J. Lemon.
//   Beej's guide covers poll()/select(); kqueue is the FreeBSD upgrade path.
//
// USAGE (called by server/run-server):   ./exchange_server <host> <port>
//   e.g.  ./exchange_server 127.0.0.1 5000
// ============================================================================

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

    // Never reused, never reset: stamped onto each accepted session so an fd
    // recycled by the kernel is still distinguishable from the session that
    // owned it before. See ClientSession::session_seq.
    unsigned long long next_session_seq = 1;

    // Register/unregister interest in a socket. Wrap EV_SET + kevent() so the
    // rest of the code reads clearly.
    void kq_add(int fd, int filter);      // filter = EVFILT_READ or EVFILT_WRITE
    void kq_del(int fd, int filter);
};

// QUIT has to tear down the session from inside handle_line, which is defined
// before close_session, so declare it up front.
static void close_session(Server& s, int fd);

// ---------------------------------------------------------------------------
// Queue a line to a client. Appends '\n' (frames it), tries an immediate send,
// and on partial send stashes the remainder + arms EVFILT_WRITE.
// This ONE helper is what protects you in Experiment 7.
// ---------------------------------------------------------------------------
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
        // Hard error (EPIPE after SIGPIPE was ignored, ECONNRESET, ...). Don't
        // tear the session down from the write path; the read side will see it
        // and run close_session() once, in one place.
        return;
    }

    // Short write: park the tail and ask to be told when the socket drains.
    // WITHOUT this, one slow market-data client would stall the whole server
    // (Experiment 7); WITH it, the backlog stays on that client's own fd.
    if (sent < framed.size()) {
        c.outbuf.push_back(framed.substr(sent));
        if (!c.want_write) {
            s.kq_add(c.fd, EVFILT_WRITE);
            c.want_write = true;
        }
    }
}

// Broadcast a TRADE to every market-data session subscribed to `instrument`.
// This is the one-to-many push of handout 2.5: the recipients asked for nothing,
// and the message says only what happened -- never who was on which side.
static void broadcast_trade(Server& s, const std::string& instrument,
                            long long qty, long long price) {
    const std::string msg = proto::msg_trade(instrument, qty, price);

    // Safe to iterate while enqueueing: enqueue() only appends to a session's
    // outbuf and may arm EVFILT_WRITE -- it never erases a session. (That is
    // exactly why enqueue() leaves teardown to the read path.)
    for (auto& kv : s.sessions) {
        ClientSession& sub = kv.second;
        if (sub.role != Role::MARKET_DATA) continue;         // traders never get TRADE
        if (sub.subscriptions.count(instrument) == 0) continue;
        enqueue(s, sub, msg);
    }
}

// ---------------------------------------------------------------------------
// Dispatch ONE fully-framed command line from client `c`.
// This is the protocol brain. Structure it as: tokenize -> switch on tokens[0].
//
// RETURNS false if the session was closed (QUIT). The caller MUST stop touching
// `c` immediately in that case -- close_session() erases it from s.sessions, so
// the reference dangles the moment we return.
// ---------------------------------------------------------------------------
static bool handle_line(Server& s, ClientSession& c, const std::string& line) {
    auto t = proto::tokenize(line);
    if (t.empty()) { enqueue(s, c, proto::msg_error("empty")); return true; }
    const std::string& cmd = t[0];

    // ---- ROLE INFERENCE + ENFORCEMENT (handout 2.8) -------------------------
    // There is no handshake in the protocol, so the FIRST role-specific command
    // decides what this connection is. QUIT is deliberately excluded: it is legal
    // for both roles, so it must not pin down an as-yet-unknown one.
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
            enqueue(s, c, proto::msg_error("quantity must be a positive integer"));
            return true;
        }
        if (!proto::parse_positive_int(t[3], price)) {
            enqueue(s, c, proto::msg_error("price must be a positive integer"));
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
            // BOUGHT/SOLD are PRIVATE execution reports for the two traders
            // involved; TRADE is the public one. A fill's counterparty may be
            // this same session (self-match is not forbidden by the handout),
            // in which case it correctly receives both BOUGHT and SOLD.
            // Handout 2.6: if a side has disconnected, the trade still happens
            // but that side gets no execution report. Matching session_seq (not
            // just fd) is what stops a NEW client on a recycled fd from
            // receiving a departed trader's BOUGHT/SOLD.
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
            enqueue(s, c, proto::msg_error("order id must be a non-negative integer"));
            return true;
        }
        // The book enforces ownership and existence in one place: cancel()
        // fails for an unknown id, an already-filled/cancelled order, and
        // anyone else's order alike.
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

// Tear down a connection: remove resting orders, unregister from kqueue, close.
static void close_session(Server& s, int fd) {
    auto it = s.sessions.find(fd);
    if (it == s.sessions.end()) return;   // already torn down -- never double-close

    // Handout 2.6 ("Connection termination and outstanding orders"): closing a
    // Trader Client's connection does NOT cancel its accepted-but-unexecuted
    // orders. They stay in the book and may still be matched; the departed
    // trader simply receives no BOUGHT/SOLD, while subscribed Market-Data
    // Clients still receive the TRADE. So we deliberately do NOT call
    // book.remove_orders_of(fd) here -- ClientSession::session_seq is what keeps
    // a recycled fd from impersonating the original owner.

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

// A socket is readable: recv into a temp buffer, feed the LineBuffer, then pull
// out and handle every complete line. recv()==0 means orderly FIN (Experiment 6
// FIN case / Experiment 8); recv()<0 with ECONNRESET means an RST (abrupt).
static void on_readable(Server& s, int fd) {
    auto it = s.sessions.find(fd);
    if (it == s.sessions.end()) return;
    ClientSession& c = it->second;

    char tmp[4096];
    for (;;) {
        ssize_t n = recv(fd, tmp, sizeof tmp, 0);

        if (n > 0) {
            // EXPERIMENT 3 EVIDENCE: logging the raw recv() size next to the
            // lines it yielded is what shows that recv boundaries and message
            // boundaries are unrelated -- one recv can carry half a message or
            // several messages.
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

    // ---- Event loop ----------------------------------------------------------
    // ONE thread, ALL sockets. kevent() blocks until at least one fd is ready,
    // then hands back only the ready ones -- so an idle client costs nothing and
    // can never delay another (Experiments 4/5).
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

// ---- kqueue registration helpers -------------------------------------------
// EV_SET only fills in a struct kevent; it is kevent() itself that hands the
// change to the kernel. Passing the changelist with a NULL eventlist means
// "apply these changes, return immediately, don't wait for events".
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
