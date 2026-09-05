#pragma once
// ============================================================================
// client_session.hpp  --  Everything the server remembers about ONE connection.
//
// The server keeps a std::unordered_map<int fd, ClientSession> (or map<fd,...>).
// Each accepted socket gets one ClientSession. This is where per-client state
// lives: the framing buffer, the role, the login name, subscriptions, and the
// pending-output queue used for backpressure (Experiment 7).
// ============================================================================

#include "../common/net_utils.hpp"
#include <string>
#include <set>
#include <deque>

// A connection's role is UNKNOWN until its first command reveals it.
// DESIGN DECISION (document this in report.pdf, handout 8.1):
//   There is no explicit "I am a trader" handshake message in the protocol.
//   The server infers role from the first command it sees:
//     LOGIN/BUY/SELL/CANCEL   -> TRADER
//     SUBSCRIBE/UNSUBSCRIBE    -> MARKET_DATA
//   Once set, a command that doesn't belong to the role gets "ERROR" (handout
//   2.8 / 4.5: a client must not use commands outside its role).
enum class Role { UNKNOWN, TRADER, MARKET_DATA };

struct ClientSession {
    int             fd = -1;

    // WHY THIS EXISTS: an order outlives its owner's connection (handout 2.6),
    // but the kernel recycles fds immediately on close -- so the NEXT client to
    // connect can be handed the same fd number. Identifying an order's owner by
    // fd alone would let that stranger receive the departed trader's BOUGHT/SOLD
    // and cancel their orders. `session_seq` is unique for the server's lifetime,
    // so (fd, session_seq) names one connection and never a later reuse of it.
    unsigned long long session_seq = 0;
    Role            role = Role::UNKNOWN;
    std::string     username;                 // set on successful LOGIN (traders)
    std::set<std::string> subscriptions;      // instruments (market-data clients)

    net::LineBuffer inbuf;                     // reassembles '\n'-delimited msgs

    // Outbound backpressure queue: if send() can't take everything right now
    // (slow/blocked reader -> full TCP send buffer -> EAGAIN), stash the tail
    // here and flush it when kqueue/poll reports the socket writable again.
    // WITHOUT this, a slow market-data client (Experiment 7) can make the whole
    // server stall. WITH it, the slow client's data just backs up on its own fd.
    std::deque<std::string> outbuf;
    bool            want_write = false;       // is this fd registered for EVFILT_WRITE?

    bool            logged_in() const { return !username.empty(); }
};
