#!/usr/bin/env python3
"""Phase 7 check (GUIDE Phase 7 + handout 2.3/2.5/2.6). Delete before submitting."""
from _client_lib import *

server = start_server()
try:
    alice, bob, md, md2 = Client(), Client(), Client(), Client()
    alice.send("LOGIN alice"); bob.send("LOGIN bob")
    md.send("SUBSCRIBE JNST"); md2.send("SUBSCRIBE IMCT")
    for c in (alice, bob, md, md2): c.drain()

    print("--- GUIDE's end-to-end scenario ---")
    alice.send("BUY JNST 100 238")
    check("alice: ORDER_ACCEPTED first", alice.drain(), ["ORDER_ACCEPTED 0"])
    check("no premature TRADE",          md.drain(), [])
    bob.send("SELL JNST 60 238")
    check("bob: ACCEPTED then SOLD",     bob.drain(), ["ORDER_ACCEPTED 1", "SOLD JNST 60 238"])
    check("alice: BOUGHT (async push)",  alice.drain(), ["BOUGHT JNST 60 238"])
    check("md: TRADE (unrequested)",     md.drain(), ["TRADE JNST 60 238"])
    check("md2 (IMCT): no TRADE",        md2.drain(), [])
    bob.send("SELL JNST 40 238")
    check("bob: completes the rest",     bob.drain(), ["ORDER_ACCEPTED 2", "SOLD JNST 40 238"])
    check("alice: final BOUGHT",         alice.drain(), ["BOUGHT JNST 40 238"])
    check("md: final TRADE",             md.drain(), ["TRADE JNST 40 238"])

    print("--- one order sweeping several resting orders ---")
    alice.send("SELL JNST 10 500"); alice.drain()
    bob.send("SELL JNST 20 500");   bob.drain()
    md.drain()
    carol = Client(); carol.send("LOGIN carol"); carol.drain()
    carol.send("BUY JNST 30 500")
    check("carol: 2 fills, FIFO order", carol.drain(),
          ["ORDER_ACCEPTED 5", "BOUGHT JNST 10 500", "BOUGHT JNST 20 500"])
    check("md: one TRADE per fill",     md.drain(), ["TRADE JNST 10 500", "TRADE JNST 20 500"])

    print("--- traders never receive TRADE (2.5) ---")
    check("alice got only her SOLD",    alice.drain(), ["SOLD JNST 10 500"])
    check("bob got only his SOLD",      bob.drain(),   ["SOLD JNST 20 500"])

    print("--- no match: accepted, but nothing else ---")
    bob.send("BUY IMCT 5 111")
    check("bob: accepted only",         bob.drain(), ["ORDER_ACCEPTED 6"])
    check("md2 (IMCT): still no TRADE", md2.drain(), [])

    print("--- argument validation ---")
    for bad, why in [("BUY JNST 0 238",   "zero quantity"),
                     ("BUY JNST -5 238",  "negative quantity"),
                     ("BUY JNST 10 0",    "zero price"),
                     ("BUY JNST 10x 238", "non-numeric quantity"),
                     ("BUY AAPL 10 238",  "unknown instrument"),
                     ("BUY JNST 10",      "too few arguments")]:
        bob.send(bad)
        check_error(f"rejected: {why}", bob.drain())

    print("--- handout 2.6: orders SURVIVE their owner's disconnect ---")
    dave = Client(); dave.send("LOGIN dave"); dave.drain()
    dave.send("BUY JNST 50 777"); dave.drain()
    dave.close(); time.sleep(0.5); md.drain()
    bob.send("SELL JNST 50 777")
    check("counterparty still fills", bob.drain(), ["ORDER_ACCEPTED 8", "SOLD JNST 50 777"])
    check("md still sees the TRADE",  md.drain(),  ["TRADE JNST 50 777"])
finally:
    finish(server, 7)
