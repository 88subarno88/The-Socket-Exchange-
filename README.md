# A2 — The Socket Exchange (C++)

TCP-based simulated trading system: one Exchange Server, many Trader and
Market-Data clients, all over raw POSIX sockets. Built and tested on
**FreeBSD 14.4-RELEASE**.

Team: **Rohit Meena** (2024CS10030), **Subarno Saha** (2024CS50431)

## Language / toolchain
- Language: **C++17**
- Compiler: `c++` (clang++ ships with FreeBSD base) or `g++` via `pkg install gcc`
- No third-party networking libraries — only the POSIX socket API + `kqueue`
  (handout 4.1.2 forbids frameworks like Boost.Asio/libevent/asyncio).

## Build
```sh
make            # builds bin/exchange_server, bin/trader_client,
                # bin/market_data_client, bin/conn_generator
```
(If BSD `make` gives trouble: `pkg install gmake` then `gmake`.)

**Run `make` first — the launchers depend on it.** `server/run-server` and the
two client launchers `exec` the binaries in `bin/`, which is created by the
build. No prebuilt binaries are shipped, so `make` is required before
`server/run-server`, the client launchers, or `experiment.py` will work.

## Run
Start the server (host + port):
```sh
./server/run-server 127.0.0.1 5000
```
In other terminals, start clients:
```sh
./client/run-trader       127.0.0.1 5000 alice
./client/run-trader       127.0.0.1 5000 bob
./client/run-market-data  127.0.0.1 5000 JNST
./client/run-market-data  127.0.0.1 5000 JNST IMCT
```
Then type protocol commands into the trader terminal, e.g.:
```
LOGIN alice
BUY JNST 100 238
SELL JNST 60 238
CANCEL 0
QUIT
```
Market-data terminals will print `TRADE ...` lines automatically.

## Configuration / assumptions
- Server binds the host+port given on the command line (loopback is fine).
- Instruments supported: `JNST`, `IMCT`.
- Numeric ranges (handout 2.1): quantity and price are integers in
  `1..2147483647`; order ids are `0..2147483647`. A `BUY`/`SELL` carrying a
  value outside that range is answered with `ERROR` and is neither accepted
  nor executed.
- Orders outlive their owner's connection (handout 2.6): a disconnect does not
  cancel resting orders. They can still match; the departed trader receives no
  `BOUGHT`/`SOLD`, while subscribed Market-Data clients still receive `TRADE`.
- Concurrency/I/O: single-threaded **kqueue** event loop (see `report.pdf`
  §Implementation Decisions).

## Layout
```
server/run-server            launcher (exec bin/exchange_server)
client/run-trader            launcher (exec bin/trader_client)
client/run-market-data       launcher (exec bin/market_data_client)
src/common/                  protocol parsing + socket helpers + line framing
src/server/                  event loop, order book, per-connection session
src/trader/                  trader client
src/market_data/             market-data client
bonus/conn_generator.cpp     70k idle-connection generator (handout 6.9)
Makefile                     builds everything into bin/
report.pdf                   experiment answers + implementation decisions
```

## Bonus
See `bonus/conn_generator.cpp` and `report.pdf`. Raise `ulimit -n` and the
relevant `sysctl` fd limits in the VM before running large connection counts.

## Bonus — 70,000 idle connections (handout 6.9)
`bonus/conn_generator.cpp` opens and holds N idle TCP connections:
```sh
./bin/conn_generator 127.0.0.1 5000 70000 127.0.0.1,127.0.0.2,127.0.0.3
```
Helpers: `bonus/bonus_run.sh <count>` runs one measurement level end to end;
`bonus/bonus_measure.sh 5000` measures a running server.

Reaching 70,000 requires kernel tuning first (as root; runtime-only, re-apply
after a reboot). Full rationale is in `report.pdf` §3.

```sh
sysctl kern.ipc.maxsockets=400000    # sizes the socket UMA zone -- the real limit
sysctl kern.maxfiles=400000
sysctl kern.maxfilesperproc=200000
sysctl kern.ipc.somaxconn=4096
sysctl net.inet.ip.portrange.first=1024
ifconfig lo0 alias 127.0.0.2/32      # extra source IPs: 70k needs >65k tuples
ifconfig lo0 alias 127.0.0.3/32
```
