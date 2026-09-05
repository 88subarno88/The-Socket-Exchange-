// ============================================================================
// trader_client.cpp  --  Trader Client (handout 3.2).
//
// A trader can TYPE commands (LOGIN/BUY/SELL/CANCEL/QUIT) AND receive server
// messages that arrive ASYNCHRONOUSLY (ORDER_ACCEPTED now, BOUGHT/SOLD much
// later when a match happens -- handout 2.7). So the client must watch TWO
// inputs at once: the keyboard (stdin, fd 0) and the socket. If you only ever
// blocked on recv(), you couldn't read the keyboard; if you only blocked on
// keyboard input, you'd miss async fills.
//
// SIMPLEST CORRECT DESIGN: poll() over { stdin, sockfd }.
//   man 2 poll   +   Beej's guide section on poll().
//   (A second reader thread also works; poll() keeps it single-threaded and is
//    the same idea the server uses, so it's good practice.)
//
// USAGE (via client/run-trader):  ./trader_client <host> <port> [username]
//   If username is given, auto-send "LOGIN <username>" on connect; otherwise
//   the user types LOGIN themselves. The experiment script may pass a username.
// ============================================================================

#include "../common/net_utils.hpp"
#include "../common/protocol.hpp"
#include <sys/socket.h>   // recv/send/shutdown
#include <poll.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>

// WHY read() AND NOT std::getline(std::cin, ...):
//   poll() reports readiness of the KERNEL's buffer, while std::cin keeps its
//   own userspace buffer on top. One getline() can pull 4 KB into cin's buffer
//   and hand back only the first line; poll() then says "stdin not ready"
//   because the kernel buffer is empty, and the already-buffered lines never get
//   sent. That deadlocks the moment input is piped rather than typed. Reading
//   the fd directly and reassembling with the same LineBuffer the server uses
//   keeps exactly one buffer in play, and framing logic identical on both sides.
int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <host> <port> [username]\n", argv[0]); return 1; }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);

    int fd = net::connect_to(host, port);
    if (fd < 0) return 1;

    // If a username was supplied, log in immediately (handout 2.2.1). The
    // experiment harness relies on this so it doesn't have to type.
    if (argc >= 4) {
        if (net::send_line(fd, std::string("LOGIN ") + argv[3] + "\n") < 0) {
            close(fd);
            return 1;
        }
    }

    net::LineBuffer server_lines;   // frame the SERVER->client stream too!
    net::LineBuffer stdin_lines;    // ...and the keyboard/pipe stream.

    // Watch BOTH inputs at once. Blocking on either alone would mean missing the
    // other -- and BOUGHT/SOLD arrive asynchronously, long after the order
    // (handout 2.7), so the socket must stay watched while we wait on the user.
    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO; pfds[0].events = POLLIN;
    pfds[1].fd = fd;           pfds[1].events = POLLIN;

    bool sent_quit = false;

    for (;;) {
        pfds[0].revents = pfds[1].revents = 0;
        int r = poll(pfds, 2, /*timeout ms=*/-1);   // -1 = block until something ready
        if (r < 0) {
            if (errno == EINTR) continue;           // signal, not a failure
            perror("poll");
            break;
        }

        // ---- keyboard/pipe has input to send --------------------------------
        // A negative fd tells poll() to skip this slot; we use that after QUIT
        // so we stop reading input but keep draining the server's replies.
        if (pfds[0].fd >= 0 && (pfds[0].revents & (POLLIN | POLLHUP))) {
            char buf[4096];
            ssize_t n = read(STDIN_FILENO, buf, sizeof buf);
            if (n > 0) {
                stdin_lines.feed(buf, (size_t)n);
                std::string line;
                while (stdin_lines.next_line(line)) {
                    if (net::send_line(fd, line + "\n") < 0) { sent_quit = true; break; }
                    auto t = proto::tokenize(line);
                    if (!t.empty() && t[0] == "QUIT") { sent_quit = true; break; }
                }
            } else if (n == 0) {
                // EOF on stdin (Ctrl-D, or the end of a pipe): leave politely.
                net::send_line(fd, "QUIT\n");
                sent_quit = true;
            } else if (errno != EINTR && errno != EAGAIN) {
                perror("read stdin");
                break;
            }

            if (sent_quit) {
                // Half-close: send FIN so the server sees an orderly shutdown
                // (this is the FIN case in Experiment 6), but keep the read
                // direction open so any final server message still arrives.
                shutdown(fd, SHUT_WR);
                pfds[0].fd = -1;
            }
        }

        // ---- server sent us something ---------------------------------------
        if (pfds[1].revents & (POLLIN | POLLHUP | POLLERR)) {
            char buf[4096];
            ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n > 0) {
                server_lines.feed(buf, (size_t)n);
                std::string msg;
                while (server_lines.next_line(msg)) {
                    // OK / ERROR / ORDER_ACCEPTED / ORDER_CANCELLED / BOUGHT / SOLD
                    std::cout << msg << std::endl;   // endl: flush so piped output
                                                     // stays in step with events
                }
            } else if (n == 0) {
                fprintf(stderr, "[server closed the connection]\n"); // FIN
                break;
            } else {
                if (errno == EINTR || errno == EAGAIN) continue;
                perror("recv");                                       // maybe RST
                break;
            }
        }
    }

    close(fd);
    return 0;
}
