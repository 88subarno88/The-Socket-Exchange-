#pragma once
// ============================================================================
// net_utils.hpp  --  Thin wrappers over the POSIX socket API.
//
// The assignment REQUIRES you to call socket()/bind()/listen()/accept()/
// connect()/send()/recv()/close()/shutdown() directly (handout 4.1). These
// wrappers do NOT hide those calls -- they just bundle the boilerplate and the
// error checking so your main logic stays readable. Every wrapper names, in a
// comment, the exact syscall it makes so you can explain it in the viva.
//
// LEARN THE SYSCALLS FIRST:
//   * Beej's Guide to Network Programming -- https://beej.us/guide/bgnet/
//     (Read sections 5 & 6. This is the single best socket tutorial. ~2 hours.)
//   * FreeBSD man pages (run inside the VM):
//       man 2 socket   man 2 bind    man 2 listen   man 2 accept
//       man 2 connect  man 2 recv    man 2 send     man 2 shutdown
//       man 2 setsockopt   man 2 fcntl   man 2 close
//   * Stevens, "UNIX Network Programming, Vol 1" -- the definitive reference.
// ============================================================================

#include <string>
#include <cstddef>
#include <sys/types.h>   // ssize_t (used below) -- POSIX, not pulled in by <string>
                         // on FreeBSD's libc++, only by glibc's libstdc++ by luck

namespace net {

// Create a TCP listening socket bound to host:port and put it in LISTEN state.
// Returns the listening fd, or -1 on error.
// Internally you will call, in order:
//   socket(AF_INET, SOCK_STREAM, 0)
//   setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, ...)   <-- so restarts don't hit
//                                                       "Address already in use"
//   bind(fd, ...)
//   listen(fd, backlog)                             <-- backlog: SOMAXCONN is fine
// See net_utils.cpp for the guided skeleton.
int make_listen_socket(const std::string& host, int port, int backlog);

// Create a TCP socket and connect() it to host:port. Returns connected fd or -1.
// Used by BOTH clients (trader + market-data) and by the bonus generator.
int connect_to(const std::string& host, int port);

// Put a file descriptor into non-blocking mode (fcntl F_GETFL / F_SETFL |O_NONBLOCK).
// You need this for the server's accepted sockets when you use kqueue/poll, and
// for the bonus generator's mass connect(). Returns 0 on success, -1 on error.
int set_nonblocking(int fd);

// Send the ENTIRE buffer, looping over send() until all bytes are written or an
// error occurs. TCP send() may accept fewer bytes than you asked (a "short
// write") -- especially once the peer's receive window fills (see Experiment 7,
// backpressure). Returns total bytes sent, or -1 on hard error.
//   NOTE: for a NON-BLOCKING socket this may return early with EAGAIN/EWOULDBLOCK;
//   the server must then buffer the remainder and finish later on a writable
//   event. This reference version assumes a BLOCKING fd (fine for the clients).
ssize_t send_all(int fd, const char* data, size_t len);

// Convenience overload for std::string. Appends nothing -- caller frames.
ssize_t send_line(int fd, const std::string& line_with_newline);

// ---------------------------------------------------------------------------
// LineBuffer  --  turns a raw TCP byte stream into complete '\n'-delimited lines.
//
// THIS CLASS IS THE ANSWER TO EXPERIMENT 3. TCP is a byte stream: one recv()
// may return half a message, or two-and-a-half messages. You must accumulate
// bytes and only act on a message once you've seen its terminating '\n'.
// ---------------------------------------------------------------------------
class LineBuffer {
public:
    // Append freshly-recv()'d bytes to the internal buffer.
    void feed(const char* data, size_t len);

    // Extract the next complete line (WITHOUT the trailing '\n') if one exists.
    // Returns true and fills `out` when a full line is available; returns false
    // when the buffer holds only a partial line (wait for more recv()).
    // Call this in a WHILE loop after each feed() -- one recv() can complete
    // several messages at once.
    bool next_line(std::string& out);

private:
    std::string buf_;  // holds bytes received but not yet consumed as a line
};

} // namespace net
