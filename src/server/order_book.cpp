#include "order_book.hpp"
#include <algorithm>   // std::min

long long OrderBook::next_id() {
    return next_id_++;
}

std::vector<Fill> OrderBook::submit(Order o) {
    std::vector<Fill> fills;

    // Defensive: the server validates qty/price before calling, but a book that
    // can't be corrupted by a bad caller is easier to trust.
    if (o.qty <= 0) return fills;

    InstrumentBook& ib = books_[o.instrument];

    std::map<long long, std::deque<Order>>& opposite =
        (o.side == Side::BUY) ? ib.sells : ib.buys;

    auto lvl = opposite.find(o.price);
    if (lvl != opposite.end()) {
        std::deque<Order>& queue = lvl->second;

        // Consume resting orders oldest-first until we're filled or the level
        // runs dry.
        while (o.qty > 0 && !queue.empty()) {
            Order& resting = queue.front();

            // Trade size is the smaller of the two REMAINING quantities.
            const long long traded = std::min(o.qty, resting.qty);

            Fill f;
            f.instrument = o.instrument;
            f.qty        = traded;
            f.price      = o.price;   // both sides agree on price by construction
            // Whichever side is the BUY supplies buyer_fd; the other supplies sell_fd.
            if (o.side == Side::BUY) {
                f.buyer_fd = o.owner_fd;       f.buyer_seq = o.owner_seq;
                f.sell_fd  = resting.owner_fd; f.sell_seq  = resting.owner_seq;
            } else {
                f.buyer_fd = resting.owner_fd; f.buyer_seq = resting.owner_seq;
                f.sell_fd  = o.owner_fd;       f.sell_seq  = o.owner_seq;
            }
            fills.push_back(f);

            o.qty       -= traded;
            resting.qty -= traded;

            // A fully-filled resting order leaves the book.
            if (resting.qty == 0) queue.pop_front();
        }

        // Maintain the invariant: never leave an empty price level behind.
        if (queue.empty()) opposite.erase(lvl);
    }

    // Whatever is left rests and may match a future order (handout 2.6).
    if (o.qty > 0) {
        std::map<long long, std::deque<Order>>& same =
            (o.side == Side::BUY) ? ib.buys : ib.sells;
        same[o.price].push_back(o);
    }

    return fills;
}

bool OrderBook::cancel(long long order_id, int requester_fd,
                       unsigned long long requester_seq) {
    for (auto& kv : books_) {
        InstrumentBook& ib = kv.second;
        for (std::map<long long, std::deque<Order>>* side : {&ib.buys, &ib.sells}) {
            for (auto lvl = side->begin(); lvl != side->end(); ++lvl) {
                std::deque<Order>& queue = lvl->second;
                for (auto it = queue.begin(); it != queue.end(); ++it) {
                    if (it->id != order_id) continue;

                    if (it->owner_fd != requester_fd) return false;
                    if (it->owner_seq != requester_seq) return false;

                    queue.erase(it);
                    if (queue.empty()) side->erase(lvl);
                    return true;
                }
            }
        }
    }
    return false;   // unknown id, already filled, or already cancelled
}

std::vector<long long> OrderBook::remove_orders_of(int owner_fd) {
    std::vector<long long> removed;

    // Not called on disconnect: handout 2.6 keeps such orders in the book.
    for (auto& kv : books_) {
        InstrumentBook& ib = kv.second;
        for (std::map<long long, std::deque<Order>>* side : {&ib.buys, &ib.sells}) {
            for (auto lvl = side->begin(); lvl != side->end(); ) {
                std::deque<Order>& queue = lvl->second;
                for (auto it = queue.begin(); it != queue.end(); ) {
                    if (it->owner_fd == owner_fd) {
                        removed.push_back(it->id);
                        it = queue.erase(it);      // erase returns the next iterator
                    } else {
                        ++it;
                    }
                }
                // Erasing a whole price level must not invalidate our loop.
                if (queue.empty()) lvl = side->erase(lvl);
                else               ++lvl;
            }
        }
    }
    return removed;
}
