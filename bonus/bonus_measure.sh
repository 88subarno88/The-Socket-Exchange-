#!/bin/sh
# ============================================================================
# bonus_measure.sh -- collect every column of the handout 6.9 table in one shot.
#
#   sh bonus/bonus_measure.sh [port]        (default port 5000)
#
# Run this while conn_generator is holding its connections. The output is sized
# to fit one terminal window so a single screenshot satisfies handout 6.9.1(3):
# "Each screenshot must clearly show the number of active connections and the
# corresponding measurements."
#
# Column -> source used here:
#   idle connections        netstat, counting server-side ESTABLISHED sockets
#   server memory           ps -o rss (resident set size)
#   server CPU              ps -o %cpu and accumulated CPU time
#   server open fds         procstat -f, counted
#   system-wide open files  sysctl kern.openfiles vs kern.maxfiles
#   socket-buffer usage     netstat -m (mbuf/cluster use vs limit)
#   max established         the count above, vs what the generator reported
#
# NOTE ON COUNTING: both endpoints of every connection are local, so each
# connection appears TWICE in netstat. We count only rows whose LOCAL address is
# the server port, which yields the true connection count.
# ============================================================================

PORT="${1:-5000}"
SRV=$(pgrep -x exchange_server | head -1)

echo "============================================================"
echo " BONUS MEASUREMENT   $(date '+%Y-%m-%d %H:%M:%S')   port ${PORT}"
echo "============================================================"

if [ -z "$SRV" ]; then
    echo " exchange_server is NOT running -- start it first."
    exit 1
fi

# ---- connection counts -----------------------------------------------------
EST=$(netstat -an -p tcp | awk -v p=".${PORT}\$" \
      '$6=="ESTABLISHED" && $4 ~ p {n++} END {print n+0}')
ALLEST=$(netstat -an -p tcp | awk '$6=="ESTABLISHED" {n++} END {print n+0}')

printf " Idle connections established : %s\n" "$EST"
printf "   (netstat rows total %s = %s x2, both ends local)\n" "$ALLEST" "$EST"
printf " Server PID                   : %s\n" "$SRV"
echo "------------------------------------------------------------"

# ---- memory + CPU ----------------------------------------------------------
RSS=$(ps -o rss= -p "$SRV" | tr -d ' ')
VSZ=$(ps -o vsz= -p "$SRV" | tr -d ' ')
CPU=$(ps -o %cpu= -p "$SRV" | tr -d ' ')
CPUTIME=$(ps -o time= -p "$SRV" | tr -d ' ')

printf " Server memory RSS            : %s KB (%s MB)\n" \
       "$RSS" "$(echo "$RSS" | awk '{printf "%.1f", $1/1024}')"
printf " Server memory VSZ            : %s KB\n" "$VSZ"
printf " Server CPU (instantaneous)   : %s %%\n" "$CPU"
printf " Server CPU time accumulated  : %s\n" "$CPUTIME"
if [ "$EST" -gt 0 ]; then
    printf " Bytes of RSS per connection  : %s\n" \
        "$(echo "$RSS $EST" | awk '{printf "%.0f", ($1*1024)/$2}')"
fi
echo "------------------------------------------------------------"

# ---- file descriptors ------------------------------------------------------
SRVFD=$(procstat -f "$SRV" 2>/dev/null | tail -n +2 | wc -l | tr -d ' ')
OPENF=$(sysctl -n kern.openfiles)
MAXF=$(sysctl -n kern.maxfiles)
MAXFPP=$(sysctl -n kern.maxfilesperproc)

printf " Server open file descriptors : %s\n" "$SRVFD"
printf " System-wide open files       : %s / %s  (%s%% of kern.maxfiles)\n" \
       "$OPENF" "$MAXF" "$(echo "$OPENF $MAXF" | awk '{printf "%.1f", ($1*100)/$2}')"
printf " kern.maxfilesperproc         : %s\n" "$MAXFPP"
echo "------------------------------------------------------------"

# ---- socket buffer / mbuf usage -------------------------------------------
echo " Socket-buffer (mbuf) usage vs limit:"
netstat -m | grep -E "mbufs in use|clusters in use|bytes allocated|denied|delayed" \
           | sed 's/^/   /'
echo "------------------------------------------------------------"

# ---- tuning currently in force --------------------------------------------
echo " Tuning in force:"
sysctl kern.ipc.somaxconn kern.ipc.nmbclusters \
       net.inet.ip.portrange.first net.inet.ip.portrange.last \
       net.inet.tcp.sendspace net.inet.tcp.recvspace 2>/dev/null | sed 's/^/   /'
echo "============================================================"
