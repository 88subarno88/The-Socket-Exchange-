#pragma once

#include <string>
#include <vector>
#include <cstdint>
#include <charconv>
#include <cctype>
namespace proto {

// The two valid instruments (handout 2.1). Keep these as the single source of
// truth; validate every BUY/SELL/SUBSCRIBE instrument against this list.
inline const char* INSTRUMENTS[] = {"JNST", "IMCT"};

// Returns true iff `s` is one of INSTRUMENTS. Implemented in protocol.cpp.
bool is_valid_instrument(const std::string& s);

inline constexpr long long MAX_VALUE = 2147483647LL;

bool parse_positive_int(const std::string& s, long long& out);

bool parse_nonnegative_int(const std::string& s, long long& out);

std::vector<std::string> tokenize(const std::string& line);

std::string msg_ok();                                            // "OK"
std::string msg_error(const std::string& reason);               // "ERROR <reason>"
std::string msg_order_accepted(long long id);                   // "ORDER_ACCEPTED <id>"
std::string msg_order_cancelled(long long id);                  // "ORDER_CANCELLED <id>"
std::string msg_bought(const std::string& inst, long long qty, long long px); // "BOUGHT ..."
std::string msg_sold(const std::string& inst, long long qty, long long px);   // "SOLD ..."
std::string msg_trade(const std::string& inst, long long qty, long long px);  // "TRADE ..."

} // namespace proto
