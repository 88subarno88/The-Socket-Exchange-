# BUILD GUIDE — what to write first, and what to test after what

The golden rule: **never implement a layer until the one below it passes a test.**
Bugs at the socket layer are almost always framing/parsing bugs in disguise, so
you test those in isolation *before* sockets enter the picture.

Dependency order (bottom → top):
```
protocol.cpp  ─┐
               ├─►  net_utils (sockets + LineBuffer)  ─►  server dispatch  ─►  experiments
order_book.cpp ┘                     ▲
                              clients (poll loop)
```

Everything lives in ONE folder (`A2_ROLL1_ROLL2/`). The server is
`src/server/`, launched by `server/run-server`. There is no separate server
project.

---

## Phase 0 — Environment & "does it even build" (30 min)
**Do:** boot the FreeBSD VM, `pkg install python3`, then from the repo root:
```sh
make
./bin/exchange_server 127.0.0.1 5000     # prints "not implemented", exits
```
**Test / done when:** all four binaries appear in `bin/` and the server binary
runs without a linker error. This proves your toolchain works. (The stubs
compile on FreeBSD because `<sys/event.h>` is present there.)

---

## Phase 1 — `src/common/protocol.cpp` (pure functions, NO sockets)
**Write:** `is_valid_instrument`, `parse_positive_int`, and the 7 message
builders (`msg_error`, `msg_order_accepted`, …).
**Test:** drop this scratch file in the repo and compile it standalone:
```cpp
// t.cpp  ->  c++ -std=c++17 t.cpp src/common/protocol.cpp -o t && ./t
#include "src/common/protocol.hpp"
#include <cassert>
#include <cstdio>
int main(){
  long long v;
  assert(!proto::parse_positive_int("0", v));      // 0 not allowed
  assert(!proto::parse_positive_int("-5", v));     // negatives banned
  assert(!proto::parse_positive_int("12a", v));    // garbage rejected
  assert(!proto::parse_positive_int("", v));
  assert( proto::parse_positive_int("238", v) && v==238);
  assert( proto::is_valid_instrument("JNST"));
  assert(!proto::is_valid_instrument("AAPL"));
  assert(proto::msg_order_accepted(42)=="ORDER_ACCEPTED 42");
  assert(proto::msg_trade("JNST",60,238)=="TRADE JNST 60 238");
  puts("protocol OK");
}
```
**Done when:** `protocol OK` prints. Delete `t.cpp` afterwards (don't ship it).

---

## Phase 2 — `src/common/net_utils` LineBuffer (framing = Experiment 3)
The `LineBuffer` is given as working reference, but you must *understand* it and
confirm it. This is the single most important correctness property in the whole
assignment.
**Test:** another scratch main:
```cpp
#include "src/common/net_utils.hpp"
#include <cassert>
#include <cstdio>
int main(){
  net::LineBuffer lb; std::string out;
  lb.feed("BU", 2);                 assert(!lb.next_line(out));   // partial
  lb.feed("Y JNST 100 238\nSE", 17);   // 17 bytes, not 16 -- an off-by-one here
                                       // silently truncates the feed
  assert(lb.next_line(out) && out=="BUY JNST 100 238");          // 1st complete
  assert(!lb.next_line(out));                                    // "SE" partial
  lb.feed("LL JNST 60 238\nCANCEL 1\n", 24);
  assert(lb.next_line(out) && out=="SELL JNST 60 238");
  assert(lb.next_line(out) && out=="CANCEL 1");                  // two-in-one recv
  assert(!lb.next_line(out));
  puts("framing OK");
}
```
**Done when:** `framing OK`. You now handle split messages AND coalesced
messages — exactly what Experiment 3 asks you to demonstrate.

---

## Phase 3 — sockets up: `make_listen_socket`, `connect_to`, and a print-only server
**Write:** in `net_utils.cpp` implement `make_listen_socket` + `connect_to`.
Then in `exchange_server.cpp` implement, in this sub-order:
1. `kqueue()` + `Server::kq_add` / `kq_del`
2. `accept_new`
3. `on_readable` — but for NOW, instead of `handle_line`, just
   `printf("[%d] %s\n", fd, line)` so you can watch framing over a real socket.
4. `close_session`
5. the event loop in `main`.

**Test (this is also Experiment 1):**
```sh
./server/run-server 127.0.0.1 5000        # terminal A
nc 127.0.0.1 5000                         # terminal B — type lines, watch A print
nc 127.0.0.1 5000                         # terminal C — a SECOND client at once
# terminal D — inspect the sockets:
sockstat -4 | grep 5000
netstat -an | grep 5000
```
**Done when:** both `nc` clients' lines print on the server AT THE SAME TIME
(neither blocks the other), and `netstat` shows one `LISTEN` socket plus one
`ESTABLISHED` socket per client. Screenshot this for Experiment 1.
> `nc` (netcat) is in the FreeBSD base system — perfect as a throwaway client
> before your own client works.

---

## Phase 4 — the clients: `trader_client.cpp` & `market_data_client.cpp`
**Write:** the `poll(stdin, sock)` loop in both — send typed lines, print server
lines, auto-`LOGIN`/auto-`SUBSCRIBE` from argv.
**Test:** replace `nc` with your real clients against the still-print-only server:
```sh
./client/run-trader      127.0.0.1 5000 alice
./client/run-market-data 127.0.0.1 5000 JNST
```
**Done when:** typing in the trader shows up on the server, and the client also
prints whatever the server sends back. Two clients stay independent.

---

## Phase 5 — `src/server/order_book.cpp` (matching, NO sockets)
**Write:** `submit`, `cancel`, `remove_orders_of`.
**Test:** the handout's own example (Section 2.6), in isolation:
```cpp
#include "src/server/order_book.hpp"
#include <cassert>
#include <cstdio>
int main(){
  OrderBook b;
  auto f1 = b.submit({b.next_id(), Side::BUY,  "JNST", 100, 238, /*fd*/7});
  assert(f1.empty());                                   // nothing to match yet
  auto f2 = b.submit({b.next_id(), Side::SELL, "JNST",  60, 238, /*fd*/8});
  assert(f2.size()==1 && f2[0].qty==60 && f2[0].price==238);
  assert(f2[0].buyer_fd==7 && f2[0].sell_fd==8);
  // buy has 40 left resting; a SELL 40 @238 should finish it:
  auto f3 = b.submit({b.next_id(), Side::SELL, "JNST",  40, 238, 9});
  assert(f3.size()==1 && f3[0].qty==40);
  // price mismatch must NOT match:
  auto f4 = b.submit({b.next_id(), Side::SELL, "JNST",  10, 999, 9});
  assert(f4.empty());
  puts("matching OK");
}
```
**Done when:** `matching OK`. Also eyeball: different instruments never cross;
same-side orders never match.

---

## Phase 6 — dispatch part 1: LOGIN / SUBSCRIBE / role enforcement
**Write:** `enqueue`, then in `handle_line`: role inference + `LOGIN`,
`SUBSCRIBE`, `UNSUBSCRIBE`. Now swap the Phase-3 `printf` for a real
`handle_line` call.
**Test:**
```
trader alice:  LOGIN alice        -> OK
trader bob:    LOGIN alice        -> ERROR (name in use)
md client:     SUBSCRIBE JNST     -> OK
trader alice:  SUBSCRIBE JNST     -> ERROR (traders can't subscribe)
md client:     BUY JNST 1 1       -> ERROR (md is read-only)
```
**Done when:** each line returns exactly the response above.

---

## Phase 7 — dispatch part 2: BUY / SELL / notifications (the payoff)
**Write:** `BUY`/`SELL` handling + `broadcast_trade`. Order of sends on a new
order: `ORDER_ACCEPTED <id>` first, then for every fill send `BOUGHT` to the
buyer, `SOLD` to the seller, and `TRADE` to every subscriber of that instrument.
**Test — full end-to-end (this is the heart of the 35% implementation grade):**
```
md client (subscribed JNST):
trader alice:  BUY JNST 100 238   -> ORDER_ACCEPTED 0
trader bob:    SELL JNST 60 238   -> ORDER_ACCEPTED 1
                                     alice sees: BOUGHT JNST 60 238
                                     bob   sees: SOLD   JNST 60 238
                                     md    sees: TRADE  JNST 60 238
trader bob:    SELL JNST 40 238   -> completes alice's remaining 40, more BOUGHT/SOLD/TRADE
```
**Done when:** all six messages land on the right terminals with the right
numbers, and the market-data client — which sent no request — receives TRADE
automatically.

---

## Phase 8 — CANCEL, QUIT, and disconnects
**Write:** `CANCEL` (→ `ORDER_CANCELLED <id>` or `ERROR`), `QUIT` (graceful
close), `recv()==0` handling, `remove_orders_of` on disconnect, and confirm
`signal(SIGPIPE, SIG_IGN)` is in `main`.
**Test:**
```
alice: BUY JNST 5 100  -> ORDER_ACCEPTED 2
alice: CANCEL 2        -> ORDER_CANCELLED 2
alice: CANCEL 2        -> ERROR (already gone)
alice: CANCEL 999      -> ERROR (never existed)
# then: Ctrl-C one client. Server must keep running and keep serving the others.
```
**Done when:** cancel logic is right AND killing any one client neither crashes
the server nor disturbs other clients (handout 4.4).

---

## Phase 9 — backpressure: `on_writable` + EVFILT_WRITE (Experiment 7)
**Write:** finish `enqueue` (buffer on short/EAGAIN send, arm EVFILT_WRITE) and
`on_writable` (drain `outbuf`, disarm when empty).
**Test:** run a market-data client that deliberately stops reading (sleep in its
loop) while a fast one keeps reading; generate lots of trades.
```sh
netstat -an | grep 5000      # watch Send-Q on the SLOW connection climb
```
**Done when:** the slow client's Send-Q grows but the fast client keeps
receiving TRADE messages uninterrupted.

---

## Phase 10 — Experiments 1–8 (40% of the grade — budget real time here)
Now that the system works, run the harness and investigate. Tools per experiment:

| Exp | What it shows | Main tools |
|----:|---------------|------------|
| 1 | LISTEN vs ESTABLISHED socket | `sockstat`, `netstat -an` |
| 2 | TCP state transitions | `netstat -an`, `tcpdump -i lo0` |
| 3 | byte-stream framing | server recv logging, `tcpdump` |
| 4 | idle client doesn't stall others | `procstat -k <pid>`, `ktrace` |
| 5 | which sockets are ready (opt.) | `netstat -an` (Recv-Q), kqueue |
| 6 | FIN vs RST | `tcpdump -n` (F vs R flags) |
| 7 | slow-receiver backpressure | `netstat -an` (Send-Q) |
| 8 | unexpected disconnect detection | `tcpdump`, server logs |

For each: run `python3 experiment.py <n>`, capture the screenshot the handout
asks for, and write the answer into `report_template.md`. Do them in order.

---

## Phase 11 — Bonus (optional, only if time): `bonus/conn_generator.cpp`
Raise limits FIRST (`ulimit -n 200000`; `sysctl kern.maxfiles`,
`kern.maxfilesperproc`), then run the generator at 10k…70k and fill the table.
Screenshots required at 10k / 40k / 70k. This is where the single-threaded
kqueue design pays off vs thread-per-connection.

---

## Phase 12 — Package & submit (do NOT skip the checks)
```sh
make clean                                   # ship source, not binaries
chmod +x server/run-server client/run-trader client/run-market-data
# rename the folder to your REAL roll numbers, alphabetically sorted:
#   A2_2024CSxxxxx_2024CSyyyyy
cd .. && zip -r A2_2024CSxxxxx_2024CSyyyyy.zip A2_2024CSxxxxx_2024CSyyyyy
```
Final checklist:
- [ ] `server/run-server`, `client/run-trader`, `client/run-market-data` exist & executable
- [ ] `README.md` and `report.pdf` at the root (convert the template!)
- [ ] fresh unzip → `make` → clients connect (test on a clean checkout)
- [ ] zip filename = alphabetically-sorted roll numbers, no spaces/hyphens
- [ ] only ONE teammate uploads
