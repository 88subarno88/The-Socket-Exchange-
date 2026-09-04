# A2 — The Socket Exchange (C++)

TCP-based simulated trading system: one Exchange Server, many Trader and
Market-Data clients, all over raw POSIX sockets. Built and tested on
**FreeBSD 14.4-RELEASE**.

> Rename the submission folder/zip to `A2_<ROLL1>_<ROLL2>.zip` with the two
> roll numbers in **alphabetical order** (handout 7.1). This scaffold ships as
> `A2_ROLL1_ROLL2` — replace both placeholders.

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
- Instruments supported: `JNST`, `IMCT`. All numbers are positive integers.
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
