#pragma once

#include <string>
#include <cstddef>
#include <sys/types.h>   // ssize_t (used below) -- POSIX, not pulled in by <string>
                         // on FreeBSD's libc++, only by glibc's libstdc++ by luck

namespace net {

int make_listen_socket(const std::string& host, int port, int backlog);

// Create a TCP socket and connect() it to host:port. Returns connected fd or -1.
// Used by BOTH clients (trader + market-data) and by the bonus generator.
int connect_to(const std::string& host, int port);

int set_nonblocking(int fd);

ssize_t send_all(int fd, const char* data, size_t len);

// Convenience overload for std::string. Appends nothing -- caller frames.
ssize_t send_line(int fd, const std::string& line_with_newline);

class LineBuffer {
public:
    // Append freshly-recv()'d bytes to the internal buffer.
    void feed(const char* data, size_t len);

    bool next_line(std::string& out);

private:
    std::string buf_;  // holds bytes received but not yet consumed as a line
};

} // namespace net
