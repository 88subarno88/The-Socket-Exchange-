#pragma once
// ============================================================================
// order_book.hpp  --  The exchange's matching engine (handout 2.6).
//
// This module knows NOTHING about sockets. It takes orders in, produces "events"
// (fills + trades) out, and the server layer turns those events into BOUGHT/
// SOLD/TRADE messages on the right sockets. Keeping matching separate from I/O
// makes it unit-testable and keeps the viva story clean.
//
// MATCHING RULES (handout 2.6) -- two orders match iff ALL hold:
//   * same instrument
//   * one BUY and one SELL
//   * EXACTLY equal price   (no "better price" crossing -- this is simplified)
// Trade quantity = min(remaining_buy, remaining_sell). Leftover stays resting
// in the book and may match a future order. Match against RESTING orders in
// arrival order (FIFO) at that price -- pick a rule and document it.
// ============================================================================

#include <string>
#include <vector>
#include <cstdint>
#include <map>
#include <deque>
#include <unordered_map>

enum class Side { BUY, SELL };

// A resting or incoming order.
struct Order {
    long long   id;          // assigned by the server, unique & increasing
    Side        side;
    std::string instrument;  // "JNST" or "IMCT"
    long long   qty;         // REMAINING quantity (decreases as it fills)
    long long   price;
    int         owner_fd;    // which trader connection submitted it
    // Identifies WHICH session on owner_fd placed this, so a later client that
    // inherits the same fd number is never mistaken for the original owner.
    // Orders survive their owner's disconnect (handout 2.6), which is precisely
    // when fd reuse becomes possible.
    unsigned long long owner_seq = 0;
};

// One execution against one resting order. The server converts each Fill into:
//   -> BOUGHT to the buy side's owner_fd
//   -> SOLD   to the sell side's owner_fd
//   -> TRADE  broadcast to every Market-Data client subscribed to `instrument`
struct Fill {
    std::string instrument;
    long long   qty;
    long long   price;
    int         buyer_fd;
    int         sell_fd;
    // The session each side belonged to. The server sends BOUGHT/SOLD only when
    // the fd's CURRENT session still matches -- otherwise the trader has gone
    // and, per handout 2.6, gets no execution report while the TRADE broadcast
    // to market-data subscribers still happens.
    unsigned long long buyer_seq = 0;
    unsigned long long sell_seq  = 0;
};

class OrderBook {
public:
    // Assign the next order id (>= 0, unique for the server's lifetime).
    long long next_id();

    // Insert a NEW order and immediately try to match it against the book.
    // Returns the list of Fills produced (possibly empty). Any unfilled
    // remainder of `o` is left resting in the book.
    //   IMPLEMENTATION OUTLINE:
    //     1. while (o.qty > 0) find a resting order on the OPPOSITE side, same
    //        instrument, same price (FIFO earliest).
    //     2. if none -> break.
    //     3. traded = min(o.qty, resting.qty);
    //        record a Fill (set buyer_fd/sell_fd based on which side is BUY).
    //        o.qty -= traded; resting.qty -= traded;
    //        if resting.qty == 0 -> remove it from the book.
    //     4. after the loop, if o.qty > 0 -> insert o into the book.
    //   DATA STRUCTURE HINT (start simple, optimise later):
    //     std::unordered_map<std::string /*instrument*/, InstrumentBook>
    //     where InstrumentBook has two price->FIFO-queue maps (buys, sells).
    //     std::map<long long, std::deque<Order>> gives ordered prices + FIFO.
    //     For THIS spec you only ever match at EQUAL price, so you can even key
    //     directly by price and skip best-price logic -- but a price-ordered map
    //     keeps you honest and is easy to explain in the viva.
    std::vector<Fill> submit(Order o);

    // Cancel the REMAINING quantity of an existing order (handout 2.2.4 / 2.3).
    // Rules: order must exist, still have unfilled qty, AND belong to requester_fd.
    // On success remove it and return true. On any failure return false (server
    // then sends ERROR).
    bool cancel(long long order_id, int requester_fd, unsigned long long requester_seq);

    // When a trader disconnects, drop all their resting orders so they can't
    // match against ghosts. (Handout 4.4: one client's failure must not corrupt
    // the server.) Return the ids removed if you want to log them.
    std::vector<long long> remove_orders_of(int owner_fd);

private:
    long long next_id_ = 0;

    // STORAGE / MATCHING POLICY (document this in report.pdf):
    //   books_[instrument] holds two price-keyed maps, one per side. Each price
    //   level is a deque used strictly as FIFO: new orders push_back, matching
    //   consumes from the front, so the OLDEST resting order at a price trades
    //   first (time priority). Since handout 2.6 only ever matches at EXACTLY
    //   equal price, matching is a single lookup of the opposite side at
    //   o.price -- no best-price search is needed. std::map (ordered) rather
    //   than unordered_map keeps price levels in a predictable order, which
    //   makes the book easy to dump and reason about in the viva.
    //   INVARIANT: a price level is erased as soon as its deque empties, so an
    //   empty deque never lingers in the map.
    struct InstrumentBook {
        std::map<long long, std::deque<Order>> buys;   // price -> FIFO of resting BUYs
        std::map<long long, std::deque<Order>> sells;  // price -> FIFO of resting SELLs
    };
    std::unordered_map<std::string, InstrumentBook> books_;   // instrument -> its book
};
