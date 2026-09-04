#pragma once
// ============================================================================
// protocol.hpp  --  Shared definitions for the Socket Exchange protocol.
//
// This file is used by BOTH the server and the clients. Keeping the wire format
// in one place means the two sides can never silently disagree on the spelling
// of a message. Read Section 2 of the handout ("Protocol Specification") once
// with this file open -- every constant below maps to a line in the spec.
//
// Wire format reminder (handout 2.0):
//   * Every application message is ONE line ending in '\n'.
//   * All numbers are POSITIVE integers (qty, price); order IDs are >= 0.
//   * Only two instruments exist: JNST and IMCT.
// ============================================================================

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

// TODO: parse a positive integer strictly.
//   Return false if the string is empty, has non-digit chars, is 0, is
//   negative, or overflows. The spec forbids decimals and negatives, so reject
//   anything that isn't a clean run of digits. Do NOT use atoi() (it silently
//   ignores garbage). Prefer std::from_chars or a manual digit loop.
//   WHY strict: Experiment/grading will throw malformed input at you and expect
//   an ERROR response, not a crash.
bool parse_positive_int(const std::string& s, long long& out);

// Split a single already-de-framed line into whitespace-separated tokens.
// Example: "BUY JNST 100 238" -> ["BUY","JNST","100","238"].
//   HINT: std::istringstream + >> is the shortest correct way. Do not try to
//   handle quoting/escaping -- the protocol has none.
std::vector<std::string> tokenize(const std::string& line);

// ---- Convenience builders for server->client messages (handout 2.3 / 2.5) ---
// These just format strings so you never typo a keyword. Each returns a line
// WITHOUT the trailing '\n' -- add the newline exactly once, at send time, so
// you can't accidentally double-frame.
//   TODO: implement each of these in protocol.cpp. They are one-liners.
std::string msg_ok();                                            // "OK"
std::string msg_error(const std::string& reason);               // "ERROR <reason>"
std::string msg_order_accepted(long long id);                   // "ORDER_ACCEPTED <id>"
std::string msg_order_cancelled(long long id);                  // "ORDER_CANCELLED <id>"
std::string msg_bought(const std::string& inst, long long qty, long long px); // "BOUGHT ..."
std::string msg_sold(const std::string& inst, long long qty, long long px);   // "SOLD ..."
std::string msg_trade(const std::string& inst, long long qty, long long px);  // "TRADE ..."

} // namespace proto
