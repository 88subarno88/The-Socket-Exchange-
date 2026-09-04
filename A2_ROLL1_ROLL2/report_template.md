# A2 Experiment Report — The Socket Exchange
Team: <ROLL1>, <ROLL2>

> Convert this to `report.pdf` before submitting. Easiest path in the VM:
>   `pkg install pandoc` then `pandoc report_template.md -o report.pdf`
> or paste into Google Docs / Overleaf and export. The submission needs
> `report.pdf` at the repo root (handout 7.1).

For EACH experiment include: (1) your answer, (2) how you investigated,
(3) exact commands/tools, (4) the required screenshot(s).

---

## Implementation Decisions (handout 8.1)
- **Concurrency / I/O model:** single-threaded event loop using `kqueue()`.
- **Why:** one thread services only sockets the kernel reports ready, so an idle
  client never blocks others (Exp 4/5), and per-connection cost stays tiny,
  which makes the 70k bonus feasible. Compared with thread-per-connection
  (simpler but heavy) and `poll()` (portable but O(n) scan), `kqueue` is the
  native FreeBSD scalable choice.
- **Message framing:** accumulate bytes per connection in a `LineBuffer`, split
  on `\n`; never assume one `recv()` == one message.
- **Role inference:** first command decides Trader vs Market-Data; disallowed
  commands for a role get `ERROR`.
- **Backpressure:** per-connection output queue + `EVFILT_WRITE`, so a slow
  reader backs up on its own socket only.
- Other decisions: <fill in>.

---

## Experiment 1 — Listening vs connected sockets
**Tools:** `sockstat -4l`, `netstat -an`.
**Approach:** run the harness, then list the server's TCP sockets.
**Answer:** The listening socket sits in `LISTEN` on `host:port` with no remote
peer (`*:*`); each accepted connection is a *separate* socket in `ESTABLISHED`
with a fully specified 4-tuple (local host:port + remote host:port). <expand>
**Screenshot:** _[insert]_

## Experiment 2 — TCP connection states over its lifetime
**Tools:** `netstat -an` (watch state), `tcpdump -i lo0`.
**Answer:** Establishment goes `SYN_SENT`/`SYN_RCVD` → `ESTABLISHED`
(3-way handshake); orderly close walks `FIN_WAIT_1/2`, `CLOSE_WAIT`,
`LAST_ACK`, `TIME_WAIT` depending on which side closes first. <map each event>
**Screenshots:** _[before / during / after]_

## Experiment 3 — TCP is a byte stream
**Tools:** server-side logging of each `recv()` size; optional `tcpdump`.
**Answer:** A single application message sent in pieces arrives across multiple
`recv()` calls (and multiple messages can arrive in one `recv()`), which is why
the `\n`-delimited `LineBuffer` is required. TCP preserves byte order, not
message boundaries. <expand>
**Screenshot:** _[insert]_

## Experiment 4 — One idle client must not stall others
**Tools:** `procstat -k <server_pid>` (kernel stack → what it's blocked in),
`sockstat`, `truss`/`ktrace`.
**Answer:** With the kqueue loop, an idle Client 1 leaves its socket simply
not-ready; the server keeps servicing Client 2. A *naïve* blocking server that
`recv()`s clients in a fixed loop would block in `recv()` on the idle fd —
`procstat -k` would show it parked there. <state which operation decides it>
**Screenshot:** _[insert]_

## Experiment 5 — Multiple clients / I/O multiplexing (optional)
**Tools:** `sockstat`, `netstat -an`, kqueue readiness.
**Answer:** Only sockets with buffered inbound data are "ready"; evidence is the
recv-queue column (`netstat -an` Recv-Q > 0) / which fds `kevent` returns. <expand>
**Screenshot:** _[insert]_

## Experiment 6 — FIN vs RST
**Tools:** `tcpdump -i lo0 -n` (look for `F`/FIN vs `R`/RST flags), server logs.
**Answer:** Orderly `QUIT`/close sends a **FIN** → server `recv()` returns 0.
Abrupt termination (e.g. `SO_LINGER{1,0}` or kill) sends a **RST** → server
`recv()`/`send()` fails with `ECONNRESET`/`EPIPE`. <describe how you forced each>
**Screenshots:** _[tcpdump FIN vs RST]_

## Experiment 7 — Backpressure / slow receiver
**Tools:** `netstat -an` (watch Send-Q on the slow conn grow), `sockstat`, `tcpdump`.
**Answer:** As the slow client stops reading, its receive window closes; the
server's Send-Q for that connection fills and `send()` would block/EAGAIN. With
per-connection output queuing the *other* clients keep flowing; without it the
server could stall. <cite the Send-Q evidence>
**Screenshot:** _[insert]_

## Experiment 8 — Unexpected disconnection
**Tools:** `tcpdump -i lo0`, `netstat -an`, server logs.
**Answer:** When the client vanishes, the server detects it via `recv()`→0 (FIN)
or an RST → `ECONNRESET`; the other client's connection is unaffected. <expand
on how detection surfaces at the socket layer>
**Screenshot:** _[insert]_

---

## Bonus — Scalability table (handout 6.9)
| Idle conns | Server mem (RSS) | CPU | Server FDs | System FDs | Sock-buf usage | Max established |
|-----------:|------------------|-----|-----------:|-----------:|----------------|----------------:|
| 10,000 |  |  |  |  |  |  |
| 20,000 |  |  |  |  |  |  |
| 30,000 |  |  |  |  |  |  |
| 40,000 |  |  |  |  |  |  |
| 50,000 |  |  |  |  |  |  |
| 60,000 |  |  |  |  |  |  |
| 70,000 |  |  |  |  |  |  |

**Q1 all conns maintained?** <...>  **Q2 first bottleneck (with data)?** <...>
**Q3 how I/O design drives it?** <...>  **Q4 would changing model help?** <...>
**Q5 measured trade-off after a change:** <...>
Screenshots required at 10k / 40k / 70k.
