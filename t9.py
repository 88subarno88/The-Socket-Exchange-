#!/usr/bin/env python3
"""Phase 9 check: backpressure -- a slow reader must not stall the server (Experiment 7)."""
import socket, subprocess, sys, time
HOST, PORT = "127.0.0.1", 5000
TRADES = 3000
fails = []
def ok(label, cond, detail=""):
    print(f"{'PASS' if cond else 'FAIL'}  {label}{('  ' + detail) if detail else ''}")
    if not cond: fails.append(label)
def conn():
    s = socket.create_connection((HOST, PORT), timeout=5); return s
def drain(s):
    """Read whatever is pending; return byte count."""
    total = 0; s.setblocking(False)
    try:
        while True:
            try: chunk = s.recv(65536)
            except (BlockingIOError, socket.error): break
            if not chunk: break
            total += len(chunk)
    finally: s.setblocking(True); s.settimeout(5)
    return total
server = subprocess.Popen(["./server/run-server", HOST, str(PORT)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1.0)
try:
    slow, fast = conn(), conn()
    buyer, seller = conn(), conn()
    slow.sendall(b"SUBSCRIBE JNST\n")
    fast.sendall(b"SUBSCRIBE JNST\n")
    buyer.sendall(b"LOGIN buyer\n")
    seller.sendall(b"LOGIN seller\n")
    time.sleep(0.5)
    for s in (fast, buyer, seller): drain(s)
    # `slow` deliberately never reads.

    print(f"--- generating {TRADES} trades; one subscriber never reads ---")
    fast_bytes = 0
    t0 = time.monotonic()
    for i in range(TRADES):
        buyer.sendall(b"BUY JNST 1 238\n")
        seller.sendall(b"SELL JNST 1 238\n")
        # traders must keep reading or THEY become slow receivers too
        drain(buyer); drain(seller)
        fast_bytes += drain(fast)
    elapsed = time.monotonic() - t0
    time.sleep(1.0)
    fast_bytes += drain(fast)
    print(f"    {elapsed:.1f}s, fast subscriber received {fast_bytes} bytes")

    ok("server survived the flood", server.poll() is None)
    ok("fast subscriber kept receiving", fast_bytes > 10000, f"({fast_bytes} bytes)")

    print("--- is the server still responsive while the slow client is backed up? ---")
    t0 = time.monotonic()
    late = conn()
    late.sendall(b"LOGIN latecomer\n")
    late.settimeout(5)
    reply = late.recv(4096).decode().strip()
    dt = time.monotonic() - t0
    ok("new client served promptly", reply.startswith("OK") and dt < 2.0, f"({reply!r} in {dt:.3f}s)")

    print("--- traders still transacting normally ---")
    buyer.sendall(b"BUY JNST 7 999\n")
    time.sleep(0.4)
    r = drain(buyer)
    ok("trader still gets responses", r > 0, f"({r} bytes)")

    print("--- the slow client is backed up, not disconnected ---")
    slow.settimeout(2)
    try:
        got = slow.recv(65536)
        ok("slow client still connected", len(got) > 0, f"({len(got)} bytes buffered for it)")
    except socket.timeout:
        ok("slow client still connected", False, "(no data)")
finally:
    server.terminate()
    try: server.wait(timeout=5)
    except subprocess.TimeoutExpired: server.kill()
print()
if fails: print(f"PHASE 9 INCOMPLETE -- {len(fails)} failure(s): " + ", ".join(fails)); sys.exit(1)
print("PHASE 9 OK")
