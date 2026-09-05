#!/usr/bin/env python3
"""Phase 6 check (GUIDE.md Phase 6 + handout 2.2/2.3/2.8).
Run INSIDE the VM:  python3 ~/A2_ROLL1_ROLL2/t6.py     Delete before submitting."""
import socket, subprocess, sys, time

HOST, PORT = "127.0.0.1", 5000
fails = []

def check(label, got, want_prefix):
    ok = got.startswith(want_prefix)
    print(f"{'PASS' if ok else 'FAIL'}  {label}: sent -> {got!r}")
    if not ok:
        fails.append(f"{label}: expected {want_prefix!r}, got {got!r}")

class Client:
    def __init__(self, name):
        self.name = name
        self.s = socket.create_connection((HOST, PORT), timeout=3)
        self.buf = b""
    def cmd(self, line):
        self.s.sendall(line.encode() + b"\n")
        return self.readline()
    def readline(self):
        while b"\n" not in self.buf:
            try:
                chunk = self.s.recv(4096)
            except socket.timeout:
                return "<timeout: no response>"
            if not chunk:
                return "<closed>"
            self.buf += chunk
        line, self.buf = self.buf.split(b"\n", 1)
        return line.decode()
    def close(self):
        try: self.s.close()
        except OSError: pass

server = subprocess.Popen(["./server/run-server", HOST, str(PORT)],
                          stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
time.sleep(1.0)

try:
    alice = Client("alice"); bob = Client("bob"); md = Client("md")

    print("--- the five cases GUIDE Phase 6 specifies ---")
    check("alice LOGIN alice",        alice.cmd("LOGIN alice"),     "OK")
    check("bob LOGIN alice (in use)", bob.cmd("LOGIN alice"),       "ERROR")
    check("md SUBSCRIBE JNST",        md.cmd("SUBSCRIBE JNST"),     "OK")
    check("alice SUBSCRIBE (trader)", alice.cmd("SUBSCRIBE JNST"),  "ERROR")
    check("md BUY (read-only)",       md.cmd("BUY JNST 1 1"),       "ERROR")

    print("--- role enforcement, both directions (handout 2.8) ---")
    check("md CANCEL rejected",       md.cmd("CANCEL 1"),           "ERROR")
    check("md LOGIN rejected",        md.cmd("LOGIN eve"),          "ERROR")
    check("alice UNSUBSCRIBE rejctd", alice.cmd("UNSUBSCRIBE JNST"),"ERROR")

    print("--- LOGIN rules (handout 2.2.1) ---")
    check("bob LOGIN bob",            bob.cmd("LOGIN bob"),         "OK")
    check("alice LOGIN twice",        alice.cmd("LOGIN alice2"),    "ERROR")
    check("LOGIN with no username",   Client("x").cmd("LOGIN"),     "ERROR")

    print("--- SUBSCRIBE / UNSUBSCRIBE ---")
    check("md SUBSCRIBE IMCT",        md.cmd("SUBSCRIBE IMCT"),     "OK")
    check("md SUBSCRIBE bogus",       md.cmd("SUBSCRIBE AAPL"),     "ERROR")
    check("md SUBSCRIBE twice",       md.cmd("SUBSCRIBE JNST"),     "OK")
    check("md UNSUBSCRIBE JNST",      md.cmd("UNSUBSCRIBE JNST"),   "OK")
    check("md UNSUBSCRIBE no args",   md.cmd("UNSUBSCRIBE"),        "ERROR")

    print("--- misc ---")
    check("unknown command",          alice.cmd("FOOBAR 1 2"),      "ERROR")

    print("--- a username frees up when its owner disconnects (2.2.1) ---")
    bob.close(); time.sleep(0.5)
    check("reuse bob after close",    Client("bob2").cmd("LOGIN bob"), "OK")

    print("--- QUIT closes the connection with no reply (2.3) ---")
    q = Client("quitter")
    q.cmd("LOGIN quitter")
    q.s.sendall(b"QUIT\n")
    check("QUIT -> closed",           q.readline(),                 "<closed>")

finally:
    server.terminate()
    try: server.wait(timeout=3)
    except subprocess.TimeoutExpired: server.kill()

print()
if fails:
    print(f"PHASE 6 INCOMPLETE -- {len(fails)} failure(s):")
    for f in fails: print("  " + f)
    sys.exit(1)
print("PHASE 6 OK")
