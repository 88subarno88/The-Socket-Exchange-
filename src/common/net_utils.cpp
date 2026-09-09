#include "net_utils.hpp"

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <fcntl.h>
#include <cstring>
#include <cstdio>
#include <cerrno>
#include <cstdint>

namespace net {

int set_nonblocking(int fd) {
    // Reference implementation -- study the two fcntl() calls.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { perror("fcntl F_GETFL"); return -1; }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { perror("fcntl F_SETFL"); return -1; }
    return 0;
}

static int fill_addr(const std::string& host, int port, struct sockaddr_in& addr) {
    memset(&addr, 0, sizeof addr);
    addr.sin_family = AF_INET;
    addr.sin_port   = htons((uint16_t)port);   // host -> network byte order
    if (host.empty() || host == "0.0.0.0") {
        addr.sin_addr.s_addr = htonl(INADDR_ANY);   // bind on every interface
        return 0;
    }
    // inet_pton returns 1 on success, 0 on a malformed address, -1 on a bad family.
    int rc = inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    if (rc != 1) {
        fprintf(stderr, "bad IPv4 address: %s\n", host.c_str());
        return -1;
    }
    return 0;
}

int make_listen_socket(const std::string& host, int port, int backlog) {
    // 1) socket(): allocate an endpoint. AF_INET + SOCK_STREAM = TCP over IPv4.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    // 2) SO_REUSEADDR: without it, restarting the server while the previous
    //    listening address still sits in TIME_WAIT fails with EADDRINUSE.
    int yes = 1;
    if (setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes) < 0) {
        perror("setsockopt SO_REUSEADDR");
        close(fd);
        return -1;
    }

    // 3) Build the address to bind to.
    struct sockaddr_in addr;
    if (fill_addr(host, port, addr) < 0) { close(fd); return -1; }

    // 4) bind(): claim the address:port for this socket.
    if (bind(fd, (struct sockaddr*)&addr, sizeof addr) < 0) {
        perror("bind");
        close(fd);
        return -1;
    }

    // 5) listen(): move the socket into LISTEN state so it can queue incoming
    //    connections. This is the socket Experiment 1 sees as LISTEN in netstat.
    if (listen(fd, backlog > 0 ? backlog : SOMAXCONN) < 0) {
        perror("listen");
        close(fd);
        return -1;
    }

    return fd;
}

int connect_to(const std::string& host, int port) {
    // 1) socket(): same endpoint creation as the server side.
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) { perror("socket"); return -1; }

    // 2) Build the peer address.
    struct sockaddr_in addr;
    if (fill_addr(host, port, addr) < 0) { close(fd); return -1; }

    if (connect(fd, (struct sockaddr*)&addr, sizeof addr) < 0) {
        perror("connect");
        close(fd);
        return -1;
    }

    return fd;
}

ssize_t send_all(int fd, const char* data, size_t len) {
    // Reference implementation of the "loop until fully sent" pattern.
    size_t sent = 0;
    while (sent < len) {
        ssize_t n = send(fd, data + sent, len - sent, 0);
        if (n < 0) {
            if (errno == EINTR) continue;      // interrupted -> retry
            // On a NON-BLOCKING socket you'd get EAGAIN/EWOULDBLOCK here and
            // should stop, buffer the rest, and resume on a writable event.
            perror("send");
            return -1;
        }
        if (n == 0) return (ssize_t)sent;      // peer closed
        sent += (size_t)n;
    }
    return (ssize_t)sent;
}

ssize_t send_line(int fd, const std::string& line_with_newline) {
    return send_all(fd, line_with_newline.data(), line_with_newline.size());
}

// ---- LineBuffer: the Experiment-3 answer, given as working reference. --------
void LineBuffer::feed(const char* data, size_t len) {
    buf_.append(data, len);
}

bool LineBuffer::next_line(std::string& out) {
    size_t pos = buf_.find('\n');
    if (pos == std::string::npos) return false;   // no complete message yet
    out = buf_.substr(0, pos);                     // line without the '\n'
    buf_.erase(0, pos + 1);                        // consume line + '\n'
    // Optional hardening: strip a trailing '\r' so CRLF input still parses.
    if (!out.empty() && out.back() == '\r') out.pop_back();
    return true;
}

} // namespace net
