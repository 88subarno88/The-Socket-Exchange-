#pragma once

#include "../common/net_utils.hpp"
#include <string>
#include <set>
#include <deque>

enum class Role { UNKNOWN, TRADER, MARKET_DATA };

struct ClientSession {
    int             fd = -1;

    unsigned long long session_seq = 0;
    Role            role = Role::UNKNOWN;
    std::string     username;                 // set on successful LOGIN (traders)
    std::set<std::string> subscriptions;      // instruments (market-data clients)

    net::LineBuffer inbuf;                     // reassembles '\n'-delimited msgs

    std::deque<std::string> outbuf;
    bool            want_write = false;       // is this fd registered for EVFILT_WRITE?

    bool            logged_in() const { return !username.empty(); }
};
