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
#include <sys/socket.h>   // recv/send
#include <poll.h>
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <string>
#include <iostream>

int main(int argc, char** argv) {
    if (argc < 3) { fprintf(stderr, "usage: %s <host> <port> [username]\n", argv[0]); return 1; }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);

    int fd = net::connect_to(host, port);   // TODO: implement connect_to
    if (fd < 0) return 1;

    // If a username was supplied, frame + send "LOGIN <username>\n" immediately.
    // TODO: if (argc >= 4) net::send_line(fd, std::string("LOGIN ")+argv[3]+"\n");

    net::LineBuffer server_lines;           // frame the SERVER->client stream too!

    // Set up poll on two fds: STDIN_FILENO (0) and fd.
    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO; pfds[0].events = POLLIN;
    pfds[1].fd = fd;           pfds[1].events = POLLIN;

    for (;;) {
        int r = poll(pfds, 2, /*timeout ms=*/-1);   // -1 = block until something ready
        if (r < 0) { perror("poll"); break; }

        // ---- keyboard has a line to send ------------------------------------
        if (pfds[0].revents & POLLIN) {
            std::string line;
            if (!std::getline(std::cin, line)) {
                // EOF on stdin (Ctrl-D): optionally send QUIT then close.
                // TODO: net::send_line(fd, "QUIT\n"); break;
                break;
            }
            // TODO: append '\n' and net::send_line(fd, line + "\n");
            //       If the user typed QUIT, you may break after sending.
        }

        // ---- server sent us something ---------------------------------------
        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n > 0) {
                server_lines.feed(buf, (size_t)n);
                std::string msg;
                while (server_lines.next_line(msg)) {
                    // TODO: print msg (e.g. std::cout << msg << "\n";)
                    // This is where OK / ORDER_ACCEPTED / BOUGHT / SOLD / ERROR appear.
                }
            } else if (n == 0) {
                fprintf(stderr, "[server closed the connection]\n"); // FIN
                break;
            } else {
                perror("recv"); break;                                // maybe RST
            }
        }
    }

    close(fd);   // TODO: consider shutdown(fd, SHUT_WR) before close for a clean FIN
    return 0;
}
