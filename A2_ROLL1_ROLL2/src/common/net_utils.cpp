// ============================================================================
// net_utils.cpp
// send_all / set_nonblocking / LineBuffer are given as WORKING reference code
// (with comments) because they are pure plumbing. make_listen_socket and
// connect_to are left as GUIDED SKELETONS -- fill the syscalls yourself so you
// can explain each one in the viva (viva = 20% of the grade).
// ============================================================================
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

namespace net {

int set_nonblocking(int fd) {
    // Reference implementation -- study the two fcntl() calls.
    int flags = fcntl(fd, F_GETFL, 0);
    if (flags < 0) { perror("fcntl F_GETFL"); return -1; }
    if (fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) { perror("fcntl F_SETFL"); return -1; }
    return 0;
}

int make_listen_socket(const std::string& host, int port, int backlog) {
    // ---- GUIDED SKELETON -- replace each TODO with the real syscall. ----
    //
    // 1) int fd = socket(AF_INET, SOCK_STREAM, 0);
    //       check fd < 0 -> perror("socket"); return -1;
    //
    // 2) int yes = 1;
    //    setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(yes));
    //       WHY: without this, re-running the server right after it exits fails
    //       with EADDRINUSE while the old socket sits in TIME_WAIT.
    //
    // 3) struct sockaddr_in addr; memset(&addr,0,sizeof addr);
    //    addr.sin_family = AF_INET;
    //    addr.sin_port   = htons(port);        // host->network byte order!
    //    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);   // "127.0.0.1"
    //
    // 4) bind(fd, (struct sockaddr*)&addr, sizeof addr);  check < 0
    //
    // 5) listen(fd, backlog);                             check < 0
    //       backlog: pass SOMAXCONN (or your `backlog` arg).
    //
    // 6) return fd;
    (void)host; (void)port; (void)backlog;
    fprintf(stderr, "make_listen_socket: NOT IMPLEMENTED YET\n");
    return -1;
}

int connect_to(const std::string& host, int port) {
    // ---- GUIDED SKELETON ----
    //
    // 1) int fd = socket(AF_INET, SOCK_STREAM, 0);   check < 0
    // 2) struct sockaddr_in addr; memset(&addr,0,sizeof addr);
    //    addr.sin_family = AF_INET;
    //    addr.sin_port   = htons(port);
    //    inet_pton(AF_INET, host.c_str(), &addr.sin_addr);
    // 3) connect(fd, (struct sockaddr*)&addr, sizeof addr);  check < 0
    // 4) return fd;
    //
    // (For the BONUS 70k generator you'll want a NON-BLOCKING variant of this
    //  so thousands of connects can be in flight at once -- see bonus/README note.)
    (void)host; (void)port;
    fprintf(stderr, "connect_to: NOT IMPLEMENTED YET\n");
    return -1;
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
