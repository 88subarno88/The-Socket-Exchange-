// ============================================================================
// order_book.cpp  --  Fill in the matching logic described in order_book.hpp.
//
// TESTING TIP: write a tiny main() in a throwaway file that drives OrderBook
// directly (no sockets) and asserts on the Fills returned. Example scenario
// straight from the handout (2.6):
//     submit(BUY  JNST 100 238)  -> no fills, rests 100
//     submit(SELL JNST  60 238)  -> 1 fill of 60 @238; buy has 40 left resting
// Get this green before wiring the engine to the server.
// ============================================================================
#include "order_book.hpp"

long long OrderBook::next_id() {
    return next_id_++;
}

std::vector<Fill> OrderBook::submit(Order o) {
    std::vector<Fill> fills;
    // TODO: implement the matching loop from the header's outline.
    //   Remember to set buyer_fd/sell_fd correctly depending on whether the
    //   INCOMING order or the RESTING order is the buy side.
    (void)o;
    return fills;
}

bool OrderBook::cancel(long long order_id, int requester_fd) {
    // TODO: find order_id in the book; verify owner == requester_fd and qty > 0;
    //       erase it; return true. Otherwise return false.
    (void)order_id; (void)requester_fd;
    return false;
}

std::vector<long long> OrderBook::remove_orders_of(int owner_fd) {
    std::vector<long long> removed;
    // TODO: sweep the book, erase every order with this owner_fd, collect ids.
    (void)owner_fd;
    return removed;
}
