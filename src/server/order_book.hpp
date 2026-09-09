#pragma once

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
    unsigned long long owner_seq = 0;
};

struct Fill {
    std::string instrument;
    long long   qty;
    long long   price;
    int         buyer_fd;
    int         sell_fd;
    unsigned long long buyer_seq = 0;
    unsigned long long sell_seq  = 0;
};

class OrderBook {
public:
    // Assign the next order id (>= 0, unique for the server's lifetime).
    long long next_id();

    std::vector<Fill> submit(Order o);

    bool cancel(long long order_id, int requester_fd, unsigned long long requester_seq);

    std::vector<long long> remove_orders_of(int owner_fd);

private:
    long long next_id_ = 0;

    struct InstrumentBook {
        std::map<long long, std::deque<Order>> buys;   // price -> FIFO of resting BUYs
        std::map<long long, std::deque<Order>> sells;  // price -> FIFO of resting SELLs
    };
    std::unordered_map<std::string, InstrumentBook> books_;   // instrument -> its book
};
