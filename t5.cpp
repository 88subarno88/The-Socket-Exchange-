// Phase 5 scratch test (GUIDE.md §Phase 5 + handout §2.6). Delete before submitting.
//   c++ -std=c++17 -Wall -Wextra t5.cpp src/server/order_book.cpp -o t5 && ./t5
#include "src/server/order_book.hpp"
#include <cassert>
#include <cstdio>

int main() {
    { // --- 1. the handout's own §2.6 example, exactly as GUIDE gives it ---
        OrderBook b;
        auto f1 = b.submit({b.next_id(), Side::BUY,  "JNST", 100, 238, 7});
        assert(f1.empty());                                   // nothing to match yet
        auto f2 = b.submit({b.next_id(), Side::SELL, "JNST",  60, 238, 8});
        assert(f2.size() == 1 && f2[0].qty == 60 && f2[0].price == 238);
        assert(f2[0].buyer_fd == 7 && f2[0].sell_fd == 8);
        assert(f2[0].instrument == "JNST");
        auto f3 = b.submit({b.next_id(), Side::SELL, "JNST", 40, 238, 9});   // finishes the 40
        assert(f3.size() == 1 && f3[0].qty == 40 && f3[0].buyer_fd == 7 && f3[0].sell_fd == 9);
        auto f4 = b.submit({b.next_id(), Side::SELL, "JNST", 10, 999, 9});   // price mismatch
        assert(f4.empty());
    }
    { // --- 2. same instrument + opposite side + EXACT price are all required ---
        OrderBook b;
        b.submit({b.next_id(), Side::BUY, "JNST", 100, 238, 7});
        // different instrument must not cross
        assert(b.submit({b.next_id(), Side::SELL, "IMCT", 100, 238, 8}).empty());
        // same side must never match
        assert(b.submit({b.next_id(), Side::BUY,  "JNST", 100, 238, 8}).empty());
        // near-miss prices must not match (no "better price" crossing in this spec)
        assert(b.submit({b.next_id(), Side::SELL, "JNST", 100, 237, 8}).empty());
        assert(b.submit({b.next_id(), Side::SELL, "JNST", 100, 239, 8}).empty());
        // ...and the original BUY is still there to be matched at its own price
        auto f = b.submit({b.next_id(), Side::SELL, "JNST", 100, 238, 9});
        assert(f.size() == 1 && f[0].qty == 100 && f[0].buyer_fd == 7);
    }
    { // --- 3. FIFO time priority: oldest resting order at a price trades first ---
        OrderBook b;
        b.submit({b.next_id(), Side::BUY, "JNST", 100, 238, 7});   // earlier
        b.submit({b.next_id(), Side::BUY, "JNST",  50, 238, 8});   // later
        auto f = b.submit({b.next_id(), Side::SELL, "JNST", 150, 238, 9});
        assert(f.size() == 2);
        assert(f[0].qty == 100 && f[0].buyer_fd == 7);   // fd 7 first: it rested first
        assert(f[1].qty ==  50 && f[1].buyer_fd == 8);
        assert(f[0].sell_fd == 9 && f[1].sell_fd == 9);
        // book is now empty on both sides at 238
        assert(b.submit({b.next_id(), Side::SELL, "JNST", 1, 238, 9}).empty());
    }
    { // --- 4. incoming order larger than the whole level rests the remainder ---
        OrderBook b;
        b.submit({b.next_id(), Side::SELL, "JNST", 30, 238, 8});
        auto f = b.submit({b.next_id(), Side::BUY, "JNST", 100, 238, 7});
        assert(f.size() == 1 && f[0].qty == 30);
        // 70 of the BUY should be resting now
        auto g = b.submit({b.next_id(), Side::SELL, "JNST", 70, 238, 9});
        assert(g.size() == 1 && g[0].qty == 70 && g[0].buyer_fd == 7);
    }
    { // --- 5. cancel: owner-only, unknown id, and no double-cancel ---
        OrderBook b;
        long long id = b.next_id();
        b.submit({id, Side::BUY, "JNST", 100, 238, 7});
        assert(!b.cancel(id, 8));            // not your order
        assert(!b.cancel(999, 7));           // no such order
        assert( b.cancel(id, 7));            // yours -> ok
        assert(!b.cancel(id, 7));            // already gone
        // and it really left the book: nothing to match against
        assert(b.submit({b.next_id(), Side::SELL, "JNST", 100, 238, 9}).empty());
    }
    { // --- 6. a fully-filled order can no longer be cancelled ---
        OrderBook b;
        long long id = b.next_id();
        b.submit({id, Side::BUY, "JNST", 60, 238, 7});
        b.submit({b.next_id(), Side::SELL, "JNST", 60, 238, 8});   // fills it completely
        assert(!b.cancel(id, 7));            // handout: only unfulfilled qty is cancellable
    }
    { // --- 7. a PARTIALLY filled order keeps its remainder and is cancellable ---
        OrderBook b;
        long long id = b.next_id();
        b.submit({id, Side::BUY, "JNST", 100, 238, 7});
        b.submit({b.next_id(), Side::SELL, "JNST", 60, 238, 8});   // 40 left resting
        assert(b.cancel(id, 7));
        assert(b.submit({b.next_id(), Side::SELL, "JNST", 40, 238, 9}).empty());
    }
    { // --- 8. remove_orders_of touches only that fd, across instruments/sides ---
        OrderBook b;
        long long a1 = b.next_id(); b.submit({a1, Side::BUY,  "JNST", 10, 238, 7});
        long long a2 = b.next_id(); b.submit({a2, Side::SELL, "IMCT", 20, 500, 7});
        long long c1 = b.next_id(); b.submit({c1, Side::BUY,  "JNST", 30, 238, 8});
        auto gone = b.remove_orders_of(7);
        assert(gone.size() == 2);
        assert((gone[0] == a1 || gone[0] == a2) && (gone[1] == a1 || gone[1] == a2) && gone[0] != gone[1]);
        assert(b.remove_orders_of(7).empty());               // idempotent
        // fd 8's order survived and still matches
        auto f = b.submit({b.next_id(), Side::SELL, "JNST", 30, 238, 9});
        assert(f.size() == 1 && f[0].buyer_fd == 8);
    }
    { // --- 9. ids are unique and increasing; defensive on non-positive qty ---
        OrderBook b;
        long long p = b.next_id();
        for (int i = 0; i < 100; ++i) { long long n = b.next_id(); assert(n > p); p = n; }
        assert(b.submit({b.next_id(), Side::BUY, "JNST", 0,  238, 7}).empty());
        assert(b.submit({b.next_id(), Side::BUY, "JNST", -5, 238, 7}).empty());
        // neither should have rested, so a matching SELL finds nothing
        assert(b.submit({b.next_id(), Side::SELL, "JNST", 1, 238, 8}).empty());
    }
    puts("matching OK");
}
