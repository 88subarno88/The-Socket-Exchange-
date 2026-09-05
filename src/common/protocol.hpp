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

// Parse a strictly POSITIVE integer. False if empty, non-digit, 0, negative, or
// overflowing. Decimals and signs are rejected -- only a clean run of digits
// passes. (atoi() is unusable here: it silently ignores trailing garbage.)
//   WHY strict: Experiment/grading will throw malformed input at you and expect
//   an ERROR response, not a crash.
//   USE FOR: quantity and price (handout 2.1).
bool parse_positive_int(const std::string& s, long long& out);

// Same strictness, but 0 is ALLOWED.
//   USE FOR: order ids, and nothing else.
//   WHY a separate function: handout 2.1 draws a real distinction -- quantity
//   and price must be POSITIVE, while order ids are only NON-NEGATIVE, and the
//   very first order the server issues has id 0. Validating "CANCEL 0" with
//   parse_positive_int would reject a legal request -- the README's own example.
bool parse_nonnegative_int(const std::string& s, long long& out);

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
