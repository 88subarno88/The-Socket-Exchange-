// Phase 1 scratch test (GUIDE.md Phase 1 + handout 2.1-2.3). Delete before submitting.
#include "src/common/protocol.hpp"
#include <cassert>
#include <cstdio>
int main() {
    long long v = -1;
    // --- parse_positive_int: strict, and 0 is NOT positive ---
    assert(!proto::parse_positive_int("0", v));
    assert(!proto::parse_positive_int("-5", v));
    assert(!proto::parse_positive_int("12a", v));
    assert(!proto::parse_positive_int("", v));
    assert(!proto::parse_positive_int(" 7", v));
    assert(!proto::parse_positive_int("7 ", v));
    assert(!proto::parse_positive_int("+7", v));
    assert(!proto::parse_positive_int("99999999999999999999", v));   // overflow
    assert( proto::parse_positive_int("238", v) && v == 238);
    assert( proto::parse_positive_int("1", v) && v == 1);
    // --- parse_nonnegative_int: same, but 0 is legal (order id 0 exists) ---
    assert( proto::parse_nonnegative_int("0", v) && v == 0);
    assert(!proto::parse_nonnegative_int("-1", v));
    assert(!proto::parse_nonnegative_int("x", v));
    assert( proto::parse_nonnegative_int("42", v) && v == 42);
    // --- instruments are exactly JNST and IMCT, case-sensitive ---
    assert( proto::is_valid_instrument("JNST"));
    assert( proto::is_valid_instrument("IMCT"));
    assert(!proto::is_valid_instrument("AAPL"));
    assert(!proto::is_valid_instrument("jnst"));
    assert(!proto::is_valid_instrument(""));
    // --- tokenize ---
    auto t = proto::tokenize("BUY JNST 100 238");
    assert(t.size() == 4 && t[0] == "BUY" && t[3] == "238");
    assert(proto::tokenize("").empty());
    assert(proto::tokenize("   ").empty());
    assert(proto::tokenize("  LOGIN   alice  ").size() == 2);
    // --- wire formats, exactly as the handout prints them (2.3/2.5) ---
    assert(proto::msg_ok() == "OK");
    assert(proto::msg_error("nope") == "ERROR nope");
    assert(proto::msg_order_accepted(42) == "ORDER_ACCEPTED 42");
    assert(proto::msg_order_cancelled(42) == "ORDER_CANCELLED 42");
    assert(proto::msg_bought("JNST", 60, 238) == "BOUGHT JNST 60 238");
    assert(proto::msg_sold("JNST", 60, 238) == "SOLD JNST 60 238");
    assert(proto::msg_trade("JNST", 60, 238) == "TRADE JNST 60 238");
    puts("protocol OK");
}
