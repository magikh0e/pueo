#!/usr/bin/env python3
"""A handshake the screen reports is a handshake the file contains.

Packet Monitor's indicator is fed from the EAPOL tracker, which runs in the
promiscuous callback before the frame-size check and before the pcap queue.
That ordering is deliberate and right: whether a handshake is recognised
should not depend on whether it fits the capture. It also means the screen
can say HS 1/1 while every one of those frames was dropped, which is what
happened on this board with a three-slot pool. Measured, on hardware: the
tracker counted five key frames and the file it was writing held none.

A pcap missing a handshake is a bad afternoon. A pcap missing a handshake
that the device said it captured is worse, because the first is noticed.

    python tools/check_pcap_pool.py

Reads source; needs no board.

What it asserts
---------------
  the reserve     Slots a key frame can take when the general pool is
                  empty, and that nothing else can take. This is the
                  assertion that matters. Depth was the first attempt and
                  it failed on hardware: sixteen slots lost all four frames
                  of a handshake, because the drain runs in ptmLoop behind
                  delay(10) and a screen redraw, so capture throughput is
                  paced by the refresh and any pool empties during the
                  burst an association causes.

  routed home     A reserve slot is returned to the reserve, in the drain
                  and on close. Returning it to the general free list would
                  hand it to the next beacon and the reserve would bleed
                  away over a session, which is the kind of fault that
                  shows up only after an hour.

  pool depth      The general pool is still not tiny, because the reserve
                  is for the frames that matter rather than an excuse to
                  drop everything else.

  every drop      Each early return between the tracker and the write
                  accounts for a key frame. A new return added above the
                  write is the way this comes back, so they are counted
                  rather than inspected by eye.

  the report      The indicator and both serial lines carry the lost count.
                  A number nobody prints is a number nobody acts on.

  no false cue    The old comment claiming the frames are already in the
                  pcap is gone, and nothing has replaced it with the same
                  promise.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
WIFI = ROOT / "ESP32-DIV" / "wifi.cpp"
SHARED = ROOT / "ESP32-DIV" / "shared.h"

# Four frames in the exchange, and the burst is not alone on the channel:
# the association, its ack and the beacons continue through it.
HANDSHAKE_FRAMES = 4
MIN_POOL = 8        # not a guarantee, just not derisory

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        FAILED.append(name)
        print("  FAIL  %-46s %s" % (name, detail))


def callback_body(src):
    i = src.index("void wifi_promiscuous(")
    j = src.index("\n}", i)
    return src[i:j]


def main():
    wifi = WIFI.read_text(encoding="utf-8", errors="replace")
    shared = SHARED.read_text(encoding="utf-8", errors="replace")

    pools = [int(m) for m in
             re.findall(r"#define\s+ESP32DIV_PCAP_POOL_SIZE\s+(\d+)", shared)]
    ok("both board profiles define a pool size", len(pools) == 2,
       "found %d" % len(pools))
    for n in pools:
        ok("  general pool of %d is not tiny" % n, n >= MIN_POOL,
           "%d slots" % n)

    m = re.search(r"PCAP_KEY_RESERVE\s*=\s*(\d+)", wifi)
    ok("there is a reserve for key frames", m is not None,
       "no PCAP_KEY_RESERVE")
    reserve = int(m.group(1)) if m else 0
    ok("  it holds at least one whole exchange",
       reserve >= HANDSHAKE_FRAMES,
       "%d slots against a %d-frame handshake" % (reserve, HANDSHAKE_FRAMES))
    ok("  the total is the pool plus the reserve",
       re.search(r"PCAP_SLOTS_TOTAL\s*=\s*PCAP_POOL_SIZE\s*\+\s*"
                 r"PCAP_KEY_RESERVE", wifi) is not None,
       "the storage is not sized for both")

    ok("  only a key frame reaches it",
       re.search(r"em\s*!=\s*Eapol::Msg::None\s*&&\s*pcapKeyFreeQ",
                 wifi) is not None,
       "the reserve is taken without checking the frame is a key frame")

    # A reserve slot returned to the general list is a reserve that bleeds
    # away, which only shows up after a long session.
    returns = re.findall(
        r"slotIdx\s*>=\s*PCAP_POOL_SIZE[^;]*\n?[^;]*"
        r"xQueueSend\(pcapKeyFreeQ", wifi)
    ok("  reserve slots are returned to the reserve", len(returns) >= 2,
       "%d of the 2 return paths route by origin" % len(returns))

    ok("  the reserve has its own free list",
       "pcapKeyFreeQ = xQueueCreate(PCAP_KEY_RESERVE" in wifi,
       "the reserve queue is not sized to the reserve")

    body = callback_body(wifi)

    # The tracker's verdict has to outlive its own block, or the drops
    # cannot be attributed.
    ok("the tracker's verdict is visible to the writer",
       re.search(r"Eapol::Msg\s+em\s*=\s*Eapol::Msg::None", body) is not None,
       "em is scoped to the block that computes it")

    # Every early return after the tracker and before the queue send.
    tracker = body.index("Eapol::observe")
    try:
        send = body.index("xQueueSend(pcapWriteQ")
    except ValueError:
        send = len(body)
    middle = body[tracker:send]
    returns = re.findall(r"\breturn\s*;", middle)
    counted = re.findall(r"s_eapolLost\+\+", middle)
    # One return is the "nothing is being written" case, which is not a loss.
    ok("every drop between the tracker and the write counts a key frame",
       len(counted) >= len(returns) - 1,
       "%d early return(s), %d counted" % (len(returns), len(counted)))

    ok("the indicator shows the lost count",
       "s_eapolLost" in wifi.split("tft.print(\"HS \")")[0][-900:],
       "the HS indicator does not read s_eapolLost")

    stop = re.findall(r"PacketMonitor stopped[^\"]*keyframes_lost", wifi)
    ok("both stop lines report it", len(stop) == 2,
       "%d of 2 stop lines carry keyframes_lost" % len(stop))

    ok("the counter resets with the others",
       re.search(r"s_eapolUsable\s*=\s*0;\s*\n\s*s_eapolLost\s*=\s*0;",
                 wifi) is not None,
       "s_eapolLost survives a restart of the feature")

    ok("nothing claims the frames are already saved",
       "already in the pcap" not in wifi,
       "a comment still promises the pcap holds them")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed  (pools %s)"
          % (CHECKS, ", ".join(str(p) for p in pools)))
    return 0


if __name__ == "__main__":
    sys.exit(main())
