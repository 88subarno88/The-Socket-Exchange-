// ============================================================================
// conn_generator.cpp  --  BONUS (handout 6.9). Open & hold N idle TCP conns.
//
// GOAL: prove the Exchange Server can hold >= 70,000 simultaneously ESTABLISHED,
// IDLE connections, and measure where the first resource wall is (memory / CPU /
// file descriptors / socket buffers).
//
// WHY a single-threaded kqueue server matters here: thread-per-connection would
// need ~70k threads, each with its own stack, and would exhaust memory long
// before 70k. An event loop keeps per-idle-fd cost down to a file descriptor
// plus the kernel's socket state, so the wall you hit is an OS resource limit
// rather than the design -- which is exactly what the bonus asks you to analyse.
//
// ---------------------------------------------------------------------------
// THE 4-TUPLE PROBLEM  (this is why the program takes a source-IP list)
// ---------------------------------------------------------------------------
// A TCP connection is identified by (src IP, src port, dst IP, dst port). Every
// connection here goes to the SAME destination 127.0.0.1:5000, so three of the
// four elements are fixed and uniqueness rests entirely on the source port -- a
// 16-bit number. FreeBSD allocates those from
// net.inet.ip.portrange.first .. .last, which defaults to 10000..65535 = 55,536
// ports. Even widened to 1024..65535 that is only 64,512.
//
// So 70,000 connections are UNREACHABLE from a single source address, however
// the limits are tuned. The fix is to vary another element of the tuple: bind()
// each client socket to one of several loopback source addresses, round-robin,
// before connect(). Each source IP carries its own ephemeral port space.
//
// Configure the extra addresses first (as root; they do not survive a reboot):
//     ifconfig lo0 alias 127.0.0.2/32
//     ifconfig lo0 alias 127.0.0.3/32
//
// ---------------------------------------------------------------------------
// LIMITS TO RAISE BEFORE THIS WORKS  (document these in the report)
// ---------------------------------------------------------------------------
//   kern.maxfiles         -- system-wide open files. BOTH ends of every
//                            connection are local here, so N connections cost
//                            2N file entries. Must exceed 2 * target.
//   kern.maxfilesperproc  -- per-process cap; must exceed the target.
//   kern.ipc.somaxconn    -- listen backlog cap; the default 128 is far below
//                            the rate at which this program connects.
//   net.inet.ip.portrange.first -- lower to 1024 to widen each IP's port space.
// This program raises its OWN RLIMIT_NOFILE to the hard limit automatically.
//
// ---------------------------------------------------------------------------
// USAGE
// ---------------------------------------------------------------------------
//   ./conn_generator <host> <port> <count> [srcip[,srcip...]]
//
//   ./conn_generator 127.0.0.1 5000 10000
//   ./conn_generator 127.0.0.1 5000 70000 127.0.0.1,127.0.0.2,127.0.0.3
//
// With no source list the kernel chooses the source address, which caps out at
// one ephemeral port range worth of connections.
// ============================================================================

#include <sys/types.h>
#include <sys/socket.h>
#include <sys/resource.h>   // getrlimit / setrlimit
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <cerrno>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <ctime>
#include <string>
#include <vector>

namespace {

// Raise our own open-file limit to the hard cap. Without this we inherit the
// shell's soft limit and stop there -- which looks like a server failure but is
// really a client-side one, an easy way to misread the whole experiment.
void raise_fd_limit() {
    struct rlimit rl;
    if (getrlimit(RLIMIT_NOFILE, &rl) < 0) { perror("getrlimit"); return; }

    const rlim_t before = rl.rlim_cur;
    rl.rlim_cur = rl.rlim_max;

    if (setrlimit(RLIMIT_NOFILE, &rl) < 0) {
        perror("setrlimit");
        fprintf(stderr, "  (still holding soft limit %llu)\n",
                (unsigned long long)before);
        return;
    }

    fprintf(stderr, "RLIMIT_NOFILE: soft %llu -> %llu (hard %llu)\n",
            (unsigned long long)before,
            (unsigned long long)rl.rlim_cur,
            (unsigned long long)rl.rlim_max);
}

// Split "a,b,c" into its comma-separated parts.
std::vector<std::string> split_csv(const std::string& s) {
    std::vector<std::string> out;
    size_t start = 0;
    for (;;) {
        size_t comma = s.find(',', start);
        std::string piece = s.substr(start, comma - start);
        if (!piece.empty()) out.push_back(piece);
        if (comma == std::string::npos) break;
        start = comma + 1;
    }
    return out;
}

// One connection: socket(), optionally bind() to a chosen source address, then
// connect(). Binding to port 0 lets the kernel pick an ephemeral port out of
// that source IP's own range -- which is what buys more than 65k connections in
// total. Returns the connected fd, or -1 with errno preserved.
int open_one(const struct sockaddr_in& dst, const char* src_ip) {
    int fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) return -1;

    if (src_ip != nullptr) {
        struct sockaddr_in src;
        memset(&src, 0, sizeof src);
        src.sin_family = AF_INET;
        src.sin_port   = 0;                 // kernel picks the ephemeral port
        if (inet_pton(AF_INET, src_ip, &src.sin_addr) != 1) {
            close(fd);
            errno = EINVAL;
            return -1;
        }
        if (bind(fd, (struct sockaddr*)&src, sizeof src) < 0) {
            const int saved = errno;
            close(fd);
            errno = saved;
            return -1;
        }
    }

    if (connect(fd, (const struct sockaddr*)&dst, sizeof dst) < 0) {
        const int saved = errno;
        close(fd);
        errno = saved;
        return -1;
    }

    return fd;
}

// Turn a failure into an explanation the report can quote directly. Which errno
// stops the run IS the answer to handout question 2 (the first bottleneck).
const char* diagnose(int err) {
    switch (err) {
        case EADDRNOTAVAIL:
            return "source address not configured on lo0 "
                   "(ifconfig lo0 alias <ip>/32), or that IP's ephemeral ports "
                   "are exhausted -- widen net.inet.ip.portrange or add a source IP";
        case EADDRINUSE:
            return "no free source port for this 4-tuple -- port space exhausted";
        case EMFILE:
            return "per-process fd limit hit -- raise kern.maxfilesperproc / ulimit -n";
        case ENFILE:
            return "SYSTEM-WIDE open file limit hit -- raise kern.maxfiles "
                   "(both ends are local, so N conns cost 2N file entries)";
        case ECONNREFUSED:
            return "nothing listening -- is the Exchange Server running?";
        case ETIMEDOUT:
            return "connect timed out -- listen backlog may be full "
                   "(raise kern.ipc.somaxconn and the server's backlog)";
        case ECONNRESET:
            return "server reset the connection -- it may have hit its own fd limit";
        case ENOBUFS:
            return "kernel out of socket buffer space -- raise kern.ipc.nmbclusters";
        default:
            return "see errno above";
    }
}

} // namespace

int main(int argc, char** argv) {
    if (argc < 4) {
        fprintf(stderr,
                "usage: %s <host> <port> <count> [srcip[,srcip...]]\n\n"
                "  %s 127.0.0.1 5000 10000\n"
                "  %s 127.0.0.1 5000 70000 127.0.0.1,127.0.0.2,127.0.0.3\n",
                argv[0], argv[0], argv[0]);
        return 1;
    }

    const std::string host  = argv[1];
    const int         port  = std::atoi(argv[2]);
    const int         count = std::atoi(argv[3]);

    if (count <= 0) { fprintf(stderr, "count must be positive\n"); return 1; }

    std::vector<std::string> src_ips;
    if (argc >= 5) src_ips = split_csv(argv[4]);

    // ---- 1) Raise our own open-file limit -----------------------------------
    raise_fd_limit();

    // ---- 2) Resolve the destination once ------------------------------------
    struct sockaddr_in dst;
    memset(&dst, 0, sizeof dst);
    dst.sin_family = AF_INET;
    dst.sin_port   = htons((uint16_t)port);
    if (inet_pton(AF_INET, host.c_str(), &dst.sin_addr) != 1) {
        fprintf(stderr, "bad destination address: %s\n", host.c_str());
        return 1;
    }

    if (src_ips.empty()) {
        fprintf(stderr,
                "source addresses: (kernel default)\n"
                "  NOTE: with a single source IP the 4-tuple limit is one\n"
                "  ephemeral port range -- about 55k by default. Pass a\n"
                "  source-IP list to exceed that.\n");
    } else {
        fprintf(stderr, "source addresses:");
        for (const std::string& ip : src_ips) fprintf(stderr, " %s", ip.c_str());
        fprintf(stderr, "  (%zu, round-robin)\n", src_ips.size());
    }

    fprintf(stderr, "target: %d idle connections to %s:%d\n\n",
            count, host.c_str(), port);

    // ---- 3) Open `count` connections and KEEP them --------------------------
    std::vector<int> fds;
    fds.reserve((size_t)count);

    const time_t started = time(nullptr);
    int fail_errno = 0;
    int failed_at  = -1;

    for (int i = 0; i < count; i++) {
        const char* src = src_ips.empty()
                        ? nullptr
                        : src_ips[(size_t)i % src_ips.size()].c_str();

        const int fd = open_one(dst, src);
        if (fd < 0) {
            fail_errno = errno;
            failed_at  = i;
            break;
        }

        fds.push_back(fd);
        // Send NO application data -- these must stay idle (handout 6.9).

        if ((i + 1) % 5000 == 0) {
            fprintf(stderr, "  established %d connections (%.0fs)\n",
                    i + 1, difftime(time(nullptr), started));
        }
    }

    const double elapsed = difftime(time(nullptr), started);

    // ---- 4) Report exactly what happened ------------------------------------
    fprintf(stderr, "\n----------------------------------------------------------\n");
    if (failed_at >= 0) {
        fprintf(stderr, "FAILED at connection %d of %d\n", failed_at, count);
        fprintf(stderr, "  errno %d: %s\n", fail_errno, strerror(fail_errno));
        fprintf(stderr, "  likely cause: %s\n", diagnose(fail_errno));
        fprintf(stderr, "  -> record %zu as \"maximum connections successfully "
                        "established\"\n", fds.size());
    } else {
        fprintf(stderr, "SUCCESS: all %d connections established\n", count);
    }
    fprintf(stderr, "holding %zu idle connections (%.0fs to build)\n",
            fds.size(), elapsed);
    fprintf(stderr, "take your measurements now; Ctrl-C to release\n");
    fprintf(stderr, "----------------------------------------------------------\n");
    fflush(stderr);

    // ---- 5) Block forever so the connections stay ESTABLISHED ---------------
    // The kernel closes every fd when this process exits, which is what makes
    // Ctrl-C a clean teardown.
    pause();
    return 0;
}
