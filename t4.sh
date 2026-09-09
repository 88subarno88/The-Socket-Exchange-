#!/bin/sh
# Phase 4 check (GUIDE.md §Phase 4): the two clients.
# Run INSIDE the VM:  sh ~/A2_ROLL1_ROLL2/t4.sh          Delete before submitting.
#
# Done-when (GUIDE): typing in a client shows up on the server, the client
# prints what the server sends back, and two clients stay independent.
# The Phase-3 server never replies, so part D uses `nc -l` as a stand-in
# server to exercise the server->client direction.
set -u
cd "$(dirname "$0")" || exit 1
PORT=5000
FAKE=5001
ok=0

pkill -x exchange_server 2>/dev/null; pkill nc 2>/dev/null; sleep 1
SRV=/tmp/p4_srv.log; rm -f $SRV
./server/run-server 127.0.0.1 $PORT > $SRV 2>&1 &
SPID=$!
sleep 1

echo "=== A: trader — auto-LOGIN from argv, then typed lines ==="
printf 'BUY JNST 100 238\nCANCEL 1\nQUIT\n' \
  | timeout 10 ./client/run-trader 127.0.0.1 $PORT alice > /tmp/p4_trader.out 2>&1
echo "trader exit=$?"

echo "=== B: market-data — auto-SUBSCRIBE for each instrument in argv ==="
printf 'UNSUBSCRIBE JNST\nQUIT\n' \
  | timeout 10 ./client/run-market-data 127.0.0.1 $PORT JNST NIFTY > /tmp/p4_md.out 2>&1
echo "market-data exit=$?"

echo "=== C: both clients at once (independence) ==="
( printf 'BUY JNST 1 238\n'; sleep 3; printf 'QUIT\n' ) \
  | timeout 10 ./client/run-trader 127.0.0.1 $PORT bob > /dev/null 2>&1 &
C1=$!
sleep 1
printf 'QUIT\n' | timeout 10 ./client/run-market-data 127.0.0.1 $PORT JNST > /dev/null 2>&1
echo "  fast client finished while slow client still connected"
wait $C1

sleep 1
kill $SPID 2>/dev/null
echo
echo "=== server saw ==="
grep -v 'recv() ->' $SRV

echo
echo "=== D: does the client PRINT what a server sends? (nc stand-in) ==="
( sleep 1; printf 'WELCOME alice\nTRADE JNST 60 238\n'; sleep 2 ) \
  | timeout 10 nc -l $FAKE > /tmp/p4_fake_rx.txt 2>/dev/null &
NCP=$!
sleep 1
printf '' | timeout 8 ./client/run-trader 127.0.0.1 $FAKE alice > /tmp/p4_fake_out.txt 2>&1
wait $NCP 2>/dev/null
echo "-- stand-in server received from client --"; cat /tmp/p4_fake_rx.txt
echo "-- client printed --"; cat /tmp/p4_fake_out.txt

echo
echo "=== verdict ==="
grep -q 'LOGIN alice'       $SRV && echo "PASS  trader auto-LOGIN from argv"        || { echo "FAIL  auto-LOGIN"; ok=1; }
grep -q 'BUY JNST 100 238'  $SRV && grep -q 'CANCEL 1' $SRV \
                                 && echo "PASS  typed lines reach the server"       || { echo "FAIL  typed lines"; ok=1; }
grep -q 'SUBSCRIBE JNST'    $SRV && grep -q 'SUBSCRIBE NIFTY' $SRV \
                                 && echo "PASS  market-data auto-SUBSCRIBE (argv)"  || { echo "FAIL  auto-SUBSCRIBE"; ok=1; }
grep -q 'UNSUBSCRIBE JNST'  $SRV && echo "PASS  market-data typed lines"            || { echo "FAIL  md typed lines"; ok=1; }
grep -q 'QUIT'              $SRV && echo "PASS  QUIT sent"                          || { echo "FAIL  QUIT"; ok=1; }
grep -q '2 client(s) connected' $SRV && echo "PASS  two clients concurrent"         || { echo "FAIL  concurrency"; ok=1; }
grep -q 'LOGIN alice'  /tmp/p4_fake_rx.txt  && echo "PASS  client->server direction" || { echo "FAIL  client->server"; ok=1; }
grep -q 'WELCOME alice' /tmp/p4_fake_out.txt && grep -q 'TRADE JNST 60 238' /tmp/p4_fake_out.txt \
                                 && echo "PASS  client prints server lines"         || { echo "FAIL  server->client print"; ok=1; }
[ $ok -eq 0 ] && echo "PHASE 4 OK" || echo "PHASE 4 INCOMPLETE"
pkill nc 2>/dev/null
exit $ok
