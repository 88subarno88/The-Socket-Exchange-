#!/bin/sh
# Run every phase check (1-9). Inside the VM:  sh ~/A2_ROLL1_ROLL2/run_all.sh
cd "$(dirname "$0")" || exit 1
pkill -x exchange_server 2>/dev/null; pkill nc 2>/dev/null; sleep 1
make > /tmp/build.log 2>&1 || { echo "BUILD FAILED"; tail -20 /tmp/build.log; exit 1; }
echo "build OK"; echo
pass=0; fail=0
run() {
    printf '=== Phase %s: %s\n' "$1" "$2"
    if sh -c "$3" > /tmp/phase$1.log 2>&1; then
        echo "    PASS"; pass=$((pass+1))
    else
        echo "    FAIL  (see /tmp/phase$1.log)"; tail -6 /tmp/phase$1.log | sed 's/^/    /'; fail=$((fail+1))
    fi
    pkill -x exchange_server 2>/dev/null; sleep 1
}
run 1 "protocol helpers"      "c++ -std=c++17 -Wall -Wextra t1.cpp src/common/protocol.cpp -o /tmp/t1 && /tmp/t1"
run 2 "LineBuffer framing"    "c++ -std=c++17 -Wall -Wextra t2.cpp src/common/net_utils.cpp -o /tmp/t2 && /tmp/t2"
run 3 "sockets + kqueue loop" "sh t3.sh"
run 4 "the two clients"       "sh t4.sh"
run 5 "order book matching"   "c++ -std=c++17 -Wall -Wextra t5.cpp src/server/order_book.cpp -o /tmp/t5 && /tmp/t5"
run 6 "LOGIN/SUBSCRIBE/roles" "python3 t6.py"
run 7 "BUY/SELL/notifications" "python3 t7.py"
run 8 "CANCEL/QUIT/disconnect" "python3 t8.py"
run 9 "backpressure"          "python3 t9.py"
echo
echo "=================================="
echo "  $pass passed, $fail failed"
echo "=================================="
[ $fail -eq 0 ]
