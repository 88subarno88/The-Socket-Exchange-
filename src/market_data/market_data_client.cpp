// ============================================================================
// market_data_client.cpp  --  Market-Data Client (handout 3.2, read-only).
//
// This client SUBSCRIBEs to one or more instruments and then mostly just
// listens: the server pushes TRADE updates whenever a matching trade happens
// (handout 2.5). It never sends BUY/SELL/CANCEL -- if it does, the server must
// answer ERROR (role enforcement). It may still type SUBSCRIBE/UNSUBSCRIBE/QUIT,
// so we use the same poll(stdin, sock) structure as the trader.
//
// EXPERIMENT 7 NOTE (slow receiver): to *play* the slow client you deliberately
// stop calling recv() for a while (e.g. sleep between reads). Don't build that
// into the normal client -- the experiment.py harness / a flag will drive it.
// For normal operation, drain the socket promptly.
//
// USAGE (via client/run-market-data): ./market_data_client <host> <port> [INSTR...]
//   Any instruments passed on the command line are auto-subscribed on connect,
//   e.g.  ./market_data_client 127.0.0.1 5000 JNST IMCT
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
    if (argc < 3) { fprintf(stderr, "usage: %s <host> <port> [INSTRUMENT...]\n", argv[0]); return 1; }
    const std::string host = argv[1];
    const int port = std::atoi(argv[2]);

    int fd = net::connect_to(host, port);   // TODO: implement connect_to
    if (fd < 0) return 1;

    // Auto-subscribe to every instrument given on the command line.
    // TODO: for (int i = 3; i < argc; i++)
    //           net::send_line(fd, std::string("SUBSCRIBE ") + argv[i] + "\n");

    net::LineBuffer server_lines;
    struct pollfd pfds[2];
    pfds[0].fd = STDIN_FILENO; pfds[0].events = POLLIN;
    pfds[1].fd = fd;           pfds[1].events = POLLIN;

    for (;;) {
        int r = poll(pfds, 2, -1);
        if (r < 0) { perror("poll"); break; }

        if (pfds[0].revents & POLLIN) {
            std::string line;
            if (!std::getline(std::cin, line)) break;  // Ctrl-D
            // TODO: send line + "\n" (SUBSCRIBE / UNSUBSCRIBE / QUIT).
        }

        if (pfds[1].revents & (POLLIN | POLLHUP)) {
            char buf[4096];
            ssize_t n = recv(fd, buf, sizeof buf, 0);
            if (n > 0) {
                server_lines.feed(buf, (size_t)n);
                std::string msg;
                while (server_lines.next_line(msg)) {
                    // TODO: print msg -- you'll see OK for subscribe, then a
                    //       stream of "TRADE <instr> <qty> <price>" lines.
                }
            } else if (n == 0) { fprintf(stderr, "[server closed]\n"); break; }
            else { perror("recv"); break; }
        }
    }
    close(fd);
    return 0;
}
