#!/usr/bin/env python3
"""Phase 8 check: CANCEL, QUIT, disconnects (handout 2.2.4/2.3/2.6/4.4)."""
from _client_lib import *

server = start_server()
try:
    alice, bob, md = Client(), Client(), Client()
    alice.send("LOGIN alice"); bob.send("LOGIN bob"); md.send("SUBSCRIBE JNST")
    for c in (alice, bob, md): c.drain()

    print("--- CANCEL (2.2.4 / 2.3) ---")
    alice.send("BUY JNST 100 238"); check("order accepted", alice.drain(), ["ORDER_ACCEPTED 0"])
    alice.send("CANCEL 0");         check("ORDER_CANCELLED, not OK", alice.drain(), ["ORDER_CANCELLED 0"])
    alice.send("CANCEL 0");         check_error("double cancel refused", alice.drain())
    bob.send("SELL JNST 100 238")
    check("cancelled order cannot match", bob.drain(), ["ORDER_ACCEPTED 1"])
    check("no TRADE from a cancelled order", md.drain(), [])

    print("--- cancel is owner-only ---")
    alice.send("BUY JNST 10 300"); alice.drain()
    bob.send("CANCEL 2");          check_error("cannot cancel another trader's order", bob.drain())
    alice.send("CANCEL 2");        check("owner can cancel it", alice.drain(), ["ORDER_CANCELLED 2"])

    print("--- id 0 is a real id; bad ids rejected ---")
    for bad in ("CANCEL -1", "CANCEL abc", "CANCEL", "CANCEL 1 2", "CANCEL 99"):
        alice.send(bad); check_error(f"rejected {bad!r}", alice.drain())

    print("--- a fully filled order can no longer be cancelled ---")
    alice.send("BUY JNST 5 400"); alice.drain()
    bob.send("SELL JNST 5 400"); bob.drain(); md.drain(); alice.drain()
    alice.send("CANCEL 3"); check_error("filled order not cancellable", alice.drain())

    print("--- handout 2.6: orders survive an ORDERLY disconnect (FIN) ---")
    dave = Client(); dave.send("LOGIN dave"); dave.drain()
    dave.send("BUY JNST 50 777"); dave.drain(); dave.close(); time.sleep(0.5); md.drain()
    bob.send("SELL JNST 50 777")
    check("counterparty still fills (FIN)", bob.drain(), ["ORDER_ACCEPTED 6", "SOLD JNST 50 777"])
    check("md still sees TRADE (FIN)",      md.drain(),  ["TRADE JNST 50 777"])

    print("--- ...and an ABRUPT one (RST), per 4.4 ---")
    eve = Client(); eve.send("LOGIN eve"); eve.drain()
    eve.send("BUY JNST 25 888"); eve.drain(); eve.rst(); time.sleep(0.5); md.drain()
    bob.send("SELL JNST 25 888")
    check("counterparty still fills (RST)", bob.drain(), ["ORDER_ACCEPTED 8", "SOLD JNST 25 888"])
    check("md still sees TRADE (RST)",      md.drain(),  ["TRADE JNST 25 888"])

    print("--- an order outliving its owner must not leak onto a RECYCLED fd ---")
    # frank leaves a resting order, then disconnects. The kernel hands his fd
    # number to the next client, who must NOT inherit his execution reports nor
    # be able to cancel his order. That is what ClientSession::session_seq is for.
    frank = Client(); frank.send("LOGIN frank"); frank.drain()
    frank.send("BUY JNST 15 555")
    check("frank's order accepted", frank.drain(), ["ORDER_ACCEPTED 9"])
    frank.close(); time.sleep(0.5)
    heir = Client(); heir.send("LOGIN heir"); heir.drain()   # likely inherits frank's fd
    heir.send("CANCEL 9")   # frank's live, resting order
    check_error("heir cannot cancel frank's order", heir.drain())
    md.drain()
    bob.send("SELL JNST 15 555")
    check("the trade still happens",   bob.drain(),  ["ORDER_ACCEPTED 10", "SOLD JNST 15 555"])
    check("heir gets NO stray BOUGHT", heir.drain(), [])
    check("md still sees the TRADE",   md.drain(),   ["TRADE JNST 15 555"])

    print("--- 4.4: the server survives all of it and still serves others ---")
    late = Client(); late.send("LOGIN late")
    check("new client still served", late.drain(), ["OK"])
finally:
    finish(server, 8)
