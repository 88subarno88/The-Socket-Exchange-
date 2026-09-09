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
