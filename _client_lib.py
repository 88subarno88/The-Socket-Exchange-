import socket, subprocess, sys, time, struct
HOST, PORT = "127.0.0.1", 5000
fails = []
def check(label, got, want):
    ok = (got == want)
    print(f"{'PASS' if ok else 'FAIL'}  {label}")
    if not ok:
        print(f"        expected {want}")
        print(f"        got      {got}")
        fails.append(label)
def check_error(label, got):
    ok = len(got) == 1 and got[0].startswith("ERROR")
    print(f"{'PASS' if ok else 'FAIL'}  {label} -> {got}")
    if not ok: fails.append(label)
class Client:
    def __init__(self):
        self.s = socket.create_connection((HOST, PORT), timeout=3)
        self.buf = b""
    def send(self, line):
        self.s.sendall(line.encode() + b"\n")
    def drain(self, wait=0.4):
        time.sleep(wait)
        self.s.setblocking(False)
        try:
            while True:
                try:
                    chunk = self.s.recv(65536)
                except (BlockingIOError, socket.error):
                    break
                if not chunk:
                    break
                self.buf += chunk
        finally:
            self.s.setblocking(True)
            self.s.settimeout(3)
        out, _, self.buf = self.buf.rpartition(b"\n")
        return [l.decode() for l in out.split(b"\n") if l]
    def close(self):
        try: self.s.close()
        except OSError: pass
    def rst(self):
        self.s.setsockopt(socket.SOL_SOCKET, socket.SO_LINGER, struct.pack("ii", 1, 0))
        self.s.close()
def start_server():
    p = subprocess.Popen(["./server/run-server", HOST, str(PORT)],
                         stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    time.sleep(1.0)
    return p
def finish(server, phase):
    server.terminate()
    try: server.wait(timeout=3)
    except subprocess.TimeoutExpired: server.kill()
    print()
    if fails:
        print(f"PHASE {phase} INCOMPLETE -- {len(fails)} failure(s): " + ", ".join(fails))
        sys.exit(1)
    print(f"PHASE {phase} OK")
