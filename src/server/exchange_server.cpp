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

    // Register/unregister interest in a socket. Wrap EV_SET + kevent() so the
    // rest of the code reads clearly.
    void kq_add(int fd, int filter);      // filter = EVFILT_READ or EVFILT_WRITE
    void kq_del(int fd, int filter);
};

// ---------------------------------------------------------------------------
// Queue a line to a client. Appends '\n' (frames it), tries an immediate send,
// and on partial send stashes the remainder + arms EVFILT_WRITE.
// This ONE helper is what protects you in Experiment 7.
// ---------------------------------------------------------------------------
static void enqueue(Server& s, ClientSession& c, const std::string& line) {
    // TODO:
    //   std::string framed = line + "\n";
    //   if c.outbuf empty, try send() once; on EAGAIN/short-write push the
    //   unsent tail into c.outbuf and call s.kq_add(c.fd, EVFILT_WRITE),
    //   set c.want_write = true. If outbuf already non-empty, just append.
    (void)s; (void)c; (void)line;
}

// Broadcast a TRADE to every market-data session subscribed to `instrument`.
static void broadcast_trade(Server& s, const std::string& instrument,
                            long long qty, long long price) {
    // TODO: iterate s.sessions; for each MARKET_DATA session whose
    //       subscriptions contains `instrument`, enqueue(msg_trade(...)).
    (void)s; (void)instrument; (void)qty; (void)price;
}

// ---------------------------------------------------------------------------
// Dispatch ONE fully-framed command line from client `c`.
// This is the protocol brain. Structure it as: tokenize -> switch on tokens[0].
// ---------------------------------------------------------------------------
static void handle_line(Server& s, ClientSession& c, const std::string& line) {
    auto t = proto::tokenize(line);
    if (t.empty()) { enqueue(s, c, proto::msg_error("empty")); return; }
    const std::string& cmd = t[0];

    // ---- ROLE INFERENCE + ENFORCEMENT (see client_session.hpp) --------------
    // TODO: if c.role == UNKNOWN, set it based on cmd. Then, if cmd is not
    //       allowed for c.role, reply ERROR and return (handout 2.8 table).

    // ---- Handle each command. Validate arguments with proto:: helpers. ------
    if (cmd == "LOGIN") {
        // Trader only. Args: <username>. Username must be unique among CONNECTED
        // traders. On success set c.username and reply OK; else ERROR.
        // TODO
    } else if (cmd == "BUY" || cmd == "SELL") {
        // Trader only. Args: <instrument> <qty> <price>.
        // Validate instrument + positive ints. Build an Order, o.id = book.next_id().
        // 1) reply ORDER_ACCEPTED <id>   (handout 2.6: accept BEFORE matching)
        // 2) fills = book.submit(order);
        // 3) for each fill: enqueue BOUGHT to buyer_fd's session, SOLD to
        //    sell_fd's session, then broadcast_trade(instrument, qty, price).
        //    (BOUGHT/SOLD go to the OWNING traders; TRADE goes to subscribers.)
        // TODO
    } else if (cmd == "CANCEL") {
        // Trader only. Args: <order_id>. If book.cancel(id, c.fd) -> reply
        // ORDER_CANCELLED <id>; else ERROR. (Note: success is NOT "OK" here.)
        // TODO
    } else if (cmd == "SUBSCRIBE") {
        // Market-data only. Args: <instrument>. Validate, add to c.subscriptions,
        // reply OK. (Idempotent: subscribing twice can still reply OK.)
        // TODO
    } else if (cmd == "UNSUBSCRIBE") {
        // Market-data only. Remove from c.subscriptions, reply OK.
        // TODO
    } else if (cmd == "QUIT") {
        // Both roles. Graceful close: you may reply (optional), then shutdown()/
        // close() this fd and drop the session. See close_session().
        // TODO
    } else {
        enqueue(s, c, proto::msg_error("unknown command"));
    }
}

// Tear down a connection: remove resting orders, unregister from kqueue, close.
static void close_session(Server& s, int fd) {
    auto it = s.sessions.find(fd);
    if (it == s.sessions.end()) return;   // already torn down -- never double-close

    // Drop this trader's resting orders so nothing can match against a ghost
    // (handout 4.4). Harmless for market-data sessions, which own no orders.
    s.book.remove_orders_of(fd);

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
    // handle_line/broadcast_trade get wired up in Phase 6/7. Reference them so
    // -Wunused-function stays quiet until then; delete once handle_line() is
    // actually called from the loop below.
    (void)&handle_line; (void)&broadcast_trade;

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
                // PHASE 3 is print-only so framing can be watched over a real
                // socket. PHASE 6 replaces this printf with:
                //     handle_line(s, c, line);
                printf("[%d] %s\n", fd, line.c_str());
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
    // TODO:
    //   while (!c.outbuf.empty()) {
    //     send the front string; on full send pop_front; on partial send, keep
    //     the remainder at the front and return (still blocked); on EAGAIN return.
    //   }
    //   if c.outbuf now empty: s.kq_del(fd, EVFILT_WRITE); c.want_write=false;
    (void)s; (void)fd;
}

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <host> <port>\n", argv[0]); return 1; }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);

    // IMPORTANT: a client that vanishes can make send() raise SIGPIPE, which by
    // default kills the process. Ignore it and handle EPIPE via errno instead.
    signal(SIGPIPE, SIG_IGN);

    Server s;
    s.listen_fd = net::make_listen_socket(host, port, /*backlog=*/128); // TODO impl
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
