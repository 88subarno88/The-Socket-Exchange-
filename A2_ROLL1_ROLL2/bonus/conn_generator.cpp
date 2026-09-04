// ============================================================================
// conn_generator.cpp  --  BONUS (handout 6.9). Open & hold N idle TCP conns.
//
// GOAL: prove your server can hold >= 70,000 simultaneously ESTABLISHED, IDLE
// connections, and measure where the first resource wall is (memory / CPU / file
// descriptors / socket buffers).
//
// WHY a single-threaded kqueue/poll server matters here: thread-per-connection
// would need ~70k threads (each with its own stack, often ~ MB) and die on
// memory long before 70k. An event loop keeps ~O(1) memory per idle fd, so the
// wall you hit is usually file-descriptor limits or socket-buffer memory -- which
// is exactly the phenomenon the bonus asks you to analyse.
//
// THINGS YOU MUST RAISE BEFORE THIS WORKS (do these in the VM, document them):
//   * This program's own fd limit:   getrlimit/setrlimit(RLIMIT_NOFILE)  (man 2 setrlimit)
//   * Shell limit:                    ulimit -n 200000   (before running server AND generator)
//   * System-wide fd cap (FreeBSD):   sysctl kern.maxfiles / kern.maxfilesperproc
//     (see:  man 8 sysctl  and  /etc/sysctl.conf ;  kern.ipc.somaxconn too)
//   * Ephemeral port range on the CLIENT side is NOT a limit here because all
//     conns go to ONE server port over loopback, but the 4-tuple must stay
//     unique -- loopback client ports still come from net.inet.ip.portrange.
//
// MEASURE (fill the handout table at 10k/20k/.../70k):
//   sockstat        -> per-socket + fd view          (man 1 sockstat)
//   netstat -an     -> count ESTABLISHED conns       (pipe through grep -c ESTAB)
//   procstat -f PID -> open fds of the server proc   (man 1 procstat)
//   ps -o rss,vsz   -> server memory (RSS)           (or  top -p PID)
//   sysctl kern.openfiles / kern.maxfiles -> systemwide fd usage vs cap
//
// USAGE:  ./conn_generator <host> <port> <count>
//   e.g.  ./conn_generator 127.0.0.1 5000 70000
// ============================================================================

#include "../src/common/net_utils.hpp"
#include <sys/resource.h>   // getrlimit / setrlimit
#include <unistd.h>
#include <cstdio>
#include <cstdlib>
#include <vector>
#include <string>

int main(int argc, char** argv) {
    if (argc < 4) { fprintf(stderr, "usage: %s <host> <port> <count>\n", argv[0]); return 1; }
    const std::string host = argv[1];
    const int port  = std::atoi(argv[2]);
    const int count = std::atoi(argv[3]);

    // 1) Raise our OWN open-file limit so we can hold `count` fds.
    // TODO:
    //   struct rlimit rl; getrlimit(RLIMIT_NOFILE, &rl);
    //   rl.rlim_cur = rl.rlim_max;   // or an explicit big number <= rl.rlim_max
    //   setrlimit(RLIMIT_NOFILE, &rl);
    //   (If rl.rlim_max itself is too low, raise kern.maxfilesperproc via sysctl.)

    // 2) Open `count` connections and KEEP them.
    std::vector<int> fds;
    fds.reserve(count);
    for (int i = 0; i < count; i++) {
        int fd = net::connect_to(host, port);   // TODO: implement connect_to
        if (fd < 0) {
            fprintf(stderr, "failed at connection %d (check limits / errno)\n", i);
            break;   // record THIS number -> "max connections successfully established"
        }
        fds.push_back(fd);
        // Send NO application data -- these must stay idle (handout 6.9).
        if ((i % 5000) == 0) fprintf(stderr, "established %d connections\n", i);
    }
    fprintf(stderr, "holding %zu idle connections; take your measurements now.\n", fds.size());

    // 3) Block forever so the connections stay ESTABLISHED while you measure.
    //    TODO: pause();  (man 2 pause)  -- or  for(;;) sleep(3600);
    //    Then Ctrl-C when done; the OS closes all fds on exit.
    pause();
    return 0;
}
