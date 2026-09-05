#!/bin/sh
# Phase 3 automated check (GUIDE.md §Phase 3 / Experiment 1 + 3).
# Run INSIDE the VM:  sh ~/A2_ROLL1_ROLL2/t3.sh
# Delete before submitting.
#
# Proves three things at once:
#   1. two clients are served concurrently (neither blocks the other)
#   2. one message SPLIT across two writes is reassembled into one line
#   3. two messages in ONE write are split into two lines
set -u
PORT=${PORT:-5000}
cd "$(dirname "$0")" || exit 1

pkill -f exchange_server 2>/dev/null
pkill nc 2>/dev/null
sleep 1

LOG=/tmp/p3.log
rm -f "$LOG"
./server/run-server 127.0.0.1 "$PORT" > "$LOG" 2>&1 &
SRV=$!
sleep 1

# Client A: sends "BUY JNST 100 " ... 2s gap ... "238\n"
# -N makes nc shutdown() the socket at EOF; without it nc half-closes and hangs.
( printf 'BUY JNST 100 '; sleep 3; printf '238\n' ) | nc -N 127.0.0.1 "$PORT" &
A=$!

sleep 1
# Client B connects while A is MID-MESSAGE and completes entirely before A does.
( printf 'SELL JNST 60 238\nCANCEL 1\n' ) | nc -N 127.0.0.1 "$PORT" &
B=$!

sleep 1
echo "=== sockstat: both clients connected at the same time ==="
sockstat -4 | grep "$PORT"
echo
echo "=== netstat: 1 LISTEN + 1 ESTABLISHED pair per client ==="
netstat -an -p tcp | grep "$PORT"
echo

wait $A $B
sleep 1
kill $SRV 2>/dev/null

echo "=== server output ==="
cat "$LOG"

echo
echo "=== verdict ==="
ok=0
grep -q 'BUY JNST 100 238'            "$LOG" && echo "PASS  split message reassembled"      || { echo "FAIL  split message"; ok=1; }
grep -q 'SELL JNST 60 238'            "$LOG" && grep -q 'CANCEL 1' "$LOG" \
                                              && echo "PASS  coalesced messages separated"  || { echo "FAIL  coalesced messages"; ok=1; }
grep -q '2 client(s) connected'       "$LOG" && echo "PASS  two clients served at once"     || { echo "FAIL  concurrency"; ok=1; }
grep -q 'peer closed (FIN)'           "$LOG" && echo "PASS  FIN detected"                   || { echo "FAIL  FIN handling"; ok=1; }
[ $ok -eq 0 ] && echo "PHASE 3 OK" || echo "PHASE 3 INCOMPLETE"
exit $ok
