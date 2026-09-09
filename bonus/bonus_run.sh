#!/bin/sh
# ============================================================================
# bonus_run.sh -- run ONE level of the handout 6.9 scalability table, unattended.
#
#   sh bonus/bonus_run.sh <count> [port]
#   sh bonus/bonus_run.sh 70000
#
# Does the whole cycle by itself: clean up, start the server, ramp the
# connections, wait until the generator has finished ramping, measure, then tear
# everything down. Everything is written to files, so it does not matter if the
# machine becomes unreachable while the connections are held -- which it does at
# high counts, since a saturated system cannot accept a new SSH session.
#
# Outputs (N = count):
#   ~/evidence/bonus/bonus_<N>.txt        the measurement table block
#   ~/evidence/bonus/gen_<N>.log          generator: SUCCESS/FAILED + errno
#   ~/evidence/bonus/srv_<N>.log          server accept log (large)
#
# Run it detached so an SSH drop cannot kill it:
#   nohup sh bonus/bonus_run.sh 70000 > /tmp/run70k.out 2>&1 &
# ============================================================================

COUNT="$1"
PORT="${2:-5000}"
SRCIPS="127.0.0.1,127.0.0.2,127.0.0.3"

[ -n "$COUNT" ] || { echo "usage: $0 <count> [port]"; exit 1; }

HERE=$(cd "$(dirname "$0")/.." && pwd)
OUT="$HOME/evidence/bonus"
mkdir -p "$OUT"

echo "=== bonus_run: $COUNT connections on port $PORT ==="

# ---- 1) clean slate --------------------------------------------------------
pkill -x conn_generator 2>/dev/null
pkill -x exchange_server 2>/dev/null

# Tearing down tens of thousands of sockets is not instant. Wait until the port
# is genuinely free, otherwise the new server fails to bind, the generator talks
# to the DYING old server, and the run dies with ECONNRESET partway through.
i=0
while sockstat -4l 2>/dev/null | grep -q ":${PORT}[^0-9]" && [ $i -lt 60 ]; do
    sleep 1
    i=$((i + 1))
done
sleep 3

# ---- 2) server, logging to a FILE (never a pty: it fflushes every accept) ---
"$HERE/server/run-server" 127.0.0.1 "$PORT" > "$OUT/srv_${COUNT}.log" 2>&1 &
sleep 2

if ! pgrep -x exchange_server > /dev/null; then
    echo "server failed to start; see $OUT/srv_${COUNT}.log"
    exit 1
fi

# ---- 3) ramp the connections ----------------------------------------------
echo "ramping..."
"$HERE/bin/conn_generator" 127.0.0.1 "$PORT" "$COUNT" "$SRCIPS" \
    > "$OUT/gen_${COUNT}.log" 2>&1 &
GEN=$!

# Wait until the generator reports it has stopped ramping -- either all
# connections are up, or it hit a wall and printed the errno. Cap the wait so a
# hang cannot block forever.
WAITED=0
while [ "$WAITED" -lt 900 ]; do
    if grep -q "take your measurements now" "$OUT/gen_${COUNT}.log" 2>/dev/null; then
        break
    fi
    if ! kill -0 "$GEN" 2>/dev/null; then
        echo "generator exited early"
        break
    fi
    sleep 5
    WAITED=$((WAITED + 5))
done

echo "ramp finished after ${WAITED}s; letting it settle"
sleep 5

# ---- 4) measure ------------------------------------------------------------
sh "$HERE/bonus/bonus_measure.sh" "$PORT" > "$OUT/bonus_${COUNT}.txt" 2>&1

# Append the generator's own verdict -- the errno that stopped it IS the answer
# to handout question 2.
{
    echo
    echo "---------------- generator verdict ----------------"
    tail -8 "$OUT/gen_${COUNT}.log"
} >> "$OUT/bonus_${COUNT}.txt"

# ---- 5) tear down ----------------------------------------------------------
pkill -x conn_generator 2>/dev/null
sleep 2
pkill -x exchange_server 2>/dev/null

echo "=== done: $OUT/bonus_${COUNT}.txt ==="
cat "$OUT/bonus_${COUNT}.txt"
