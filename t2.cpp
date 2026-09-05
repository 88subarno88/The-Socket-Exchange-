// Phase 2 scratch test (GUIDE.md §Phase 2). Delete before submitting.
//   c++ -std=c++17 -Wall -Wextra t2.cpp src/common/net_utils.cpp -o t2 && ./t2
//
// FEED() takes the byte count from the literal itself, so a hand-counted
// length can never disagree with the string. (The GUIDE's own version says
// 16 for a 17-byte literal, which silently eats a character.)
#include "src/common/net_utils.hpp"
#include <cassert>
#include <cstdio>

#define FEED(lb, lit) (lb).feed(lit, sizeof(lit) - 1)

int main() {
    { // --- the GUIDE's scenario, with correct lengths ---
        net::LineBuffer lb; std::string out;
        FEED(lb, "BU");                        assert(!lb.next_line(out));   // partial
        FEED(lb, "Y JNST 100 238\nSE");
        assert(lb.next_line(out) && out == "BUY JNST 100 238");              // split across recv()s
        assert(!lb.next_line(out));                                          // "SE" still partial
        FEED(lb, "LL JNST 60 238\nCANCEL 1\n");
        assert(lb.next_line(out) && out == "SELL JNST 60 238");
        assert(lb.next_line(out) && out == "CANCEL 1");                      // two msgs, one recv
        assert(!lb.next_line(out));
    }
    { // --- one byte at a time: the worst case Experiment 3 provokes ---
        net::LineBuffer lb; std::string out;
        const char* msg = "LOGIN alice TRADER\n";
        for (const char* p = msg; *p; ++p) {
            bool got = lb.next_line(out);
            assert(got == (p == msg + 19));   // never true before the '\n' arrives
            lb.feed(p, 1);
        }
        assert(lb.next_line(out) && out == "LOGIN alice TRADER");
        assert(!lb.next_line(out));
    }
    { // --- many messages coalesced into a single recv() ---
        net::LineBuffer lb; std::string out;
        FEED(lb, "A\nB\nC\nD\n");
        for (const char* e : {"A", "B", "C", "D"})
            assert(lb.next_line(out) && out == e);
        assert(!lb.next_line(out));
    }
    { // --- empty line, CRLF, and a lone '\r' that is NOT a terminator ---
        net::LineBuffer lb; std::string out;
        FEED(lb, "\n");                assert(lb.next_line(out) && out == "");
        FEED(lb, "QUIT\r\n");          assert(lb.next_line(out) && out == "QUIT");
        FEED(lb, "PAR\rTIAL");         assert(!lb.next_line(out));  // '\r' alone frames nothing
        FEED(lb, "\n");                assert(lb.next_line(out) && out == "PAR\rTIAL");
    }
    { // --- buffer survives a partial tail across many feeds (no data loss) ---
        net::LineBuffer lb; std::string out;
        for (int i = 0; i < 1000; ++i) FEED(lb, "x");
        assert(!lb.next_line(out));
        FEED(lb, "\n");
        assert(lb.next_line(out) && out.size() == 1000);
        assert(!lb.next_line(out));
    }
    puts("framing OK");
}
