// ============================================================================
// protocol.cpp  --  Implementations for protocol.hpp
// These are pure string/number utilities: no sockets here. Get them correct and
// unit-test them on the command line BEFORE you touch networking, because every
// bug in framing/parsing looks like a "networking" bug once sockets are involved.
// ============================================================================
#include "protocol.hpp"
#include <sstream>
#include <cctype>

namespace proto {

bool is_valid_instrument(const std::string& s) {
    for (size_t i = 0; i < sizeof(INSTRUMENTS) / sizeof(INSTRUMENTS[0]); i++){
        if (s==INSTRUMENTS[i]){
            return true;
        }
    }
    return false;
}

bool parse_nonnegative_int(const std::string& s, long long& out) {
    // Strict per handout 2.1: digits only, no sign, no decimal point, no
    // leading/trailing space. Anything else must become an ERROR response
    // upstream, never a crash. 0 IS accepted here -- see the header for why.
    if (s.empty()) {
        return false;
    }
    // Reject anything that isn't a clean run of digits. This is what kills
    // "-5", "+5", "12a", "1.5" and " 7" before from_chars ever sees them.
    for (char c : s) {
        if (!std::isdigit(static_cast<unsigned char>(c))) {
            return false;
        }
    }
    // from_chars reports overflow as result_out_of_range instead of wrapping.
    auto [ptr, ec] = std::from_chars(s.data(), s.data() + s.size(), out);
    if (ec != std::errc()) {
        return false;
    }
    return true;
}

bool parse_positive_int(const std::string& s, long long& out) {
    // Identical parsing, one extra rule: quantity and price must be > 0.
    // Only "0" (or "000") can reach the check non-positive.
    if (!parse_nonnegative_int(s, out)) return false;
    return out > 0;
}

std::vector<std::string> tokenize(const std::string& line) {
    std::vector<std::string> tok;
    std::istringstream iss(line);
    std::string w;
    while (iss >> w) tok.push_back(w);
    return tok; // this one is done for you as a reference pattern.
}

// ---- message builders --------------------------------------------------------
// Wire forms come straight from handout 2.3 (trader responses) and 2.5 (market
// data). None of these append '\n' -- the newline is added exactly once at send
// time so a line can never get double-framed.
std::string msg_ok() { return "OK"; }

std::string msg_error(const std::string& reason)       { return "ERROR " + reason; }
std::string msg_order_accepted(long long id)           { return "ORDER_ACCEPTED " + std::to_string(id); }
std::string msg_order_cancelled(long long id)          { return "ORDER_CANCELLED " + std::to_string(id); }
std::string msg_bought(const std::string& i, long long q, long long p) { return "BOUGHT " + i + " " + std::to_string(q) + " " + std::to_string(p); }
std::string msg_sold(const std::string& i, long long q, long long p)   { return "SOLD "   + i + " " + std::to_string(q) + " " + std::to_string(p); }
std::string msg_trade(const std::string& i, long long q, long long p)  { return "TRADE "  + i + " " + std::to_string(q) + " " + std::to_string(p); }

} // namespace proto
