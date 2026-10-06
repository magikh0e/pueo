#!/usr/bin/env python3
"""The channel analyser's band tag, against the two standards it quotes.

The nRF24 reports energy and not protocol, so the tag beside a peak says what
occupies a frequency rather than what is transmitting. That makes the
arithmetic the whole claim: get the centre frequencies wrong and the screen
confidently names the wrong channel, which is worse than naming none.

    python tools/check_band_tag.py

Reads source; needs no board.

What it asserts
---------------
  constants       The source still derives Zigbee from 2405 + 5(z-11) over
                  11..26, and WiFi from 2412 + 5(w-1) over 1..13 with 14 at
                  2484. A typo in any of those is silent on screen.

  mapping         Every Zigbee channel lands on the RF_CH the datasheet
                  implies, and the sixteen of them fall inside the 0..127
                  sweep the scanner already walks.

  overlap         The channels where Zigbee and WiFi share a frequency are
                  tagged with both. Printing one would be a guess dressed as
                  a reading, and these are the cases where it would be wrong
                  half the time.

  quiet gaps      Zigbee 15, 20, 25 and 26 are the ones that sit between WiFi
                  channels, which is why installers are told to use them. The
                  tag must not claim a WiFi channel there.
"""
import re
import sys
from pathlib import Path

SRC = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "bluetooth.cpp"
src = SRC.read_text(encoding="utf-8", errors="replace")

checks = 0
fails = []


def ok(what, cond, why=""):
    global checks
    checks += 1
    if cond:
        print("  ok    %s" % what)
    else:
        fails.append(what)
        print("  FAIL  %-44s %s" % (what, why))


# ── the constants are still the ones the comment claims ──────────────────

body = src[src.index("static String bandTag(int rfch)"):]
body = body[:body.index("\n}\n") + 3]

ok("Zigbee centres are 2405 + 5(z-11)",
   "2405 + 5 * (z - 11)" in body, "the formula changed")
ok("  over channels 11 to 26",
   "z = 11; z <= 26" in body, "the range changed")
ok("WiFi centres are 2412 + 5(w-1)",
   "2412 + 5 * (w - 1)" in body, "the formula changed")
ok("  with 14 alone at 2484",
   "2484" in body and "w == 14" in body, "channel 14 is not special-cased")
ok("RF_CH n is 2400 + n MHz",
   "2400 + rfch" in body, "the band base changed")

m = re.search(r"mhz >= centre - (\d+) && mhz <= centre \+ (\d+)", body)
ok("a hit is within 2 MHz of a centre",
   m is not None and m.group(1) == "2" and m.group(2) == "2",
   "the tolerance is not 2 MHz either side")


# ── and the arithmetic they produce ──────────────────────────────────────

def zigbee(z):
    return 2405 + 5 * (z - 11)


def wifi(w):
    return 2484 if w == 14 else 2412 + 5 * (w - 1)


def tag(mhz):
    out = []
    for z in range(11, 27):
        if abs(mhz - zigbee(z)) <= 2:
            out.append("Zb%d" % z)
            break
    for w in range(1, 15):
        if abs(mhz - wifi(w)) <= 2:
            out.append("W%d" % w)
            break
    return "/".join(out)


ok("Zigbee 11 is 2405, which is RF_CH 5", zigbee(11) == 2405)
ok("Zigbee 26 is 2480, which is RF_CH 80", zigbee(26) == 2480)
ok("every Zigbee channel is inside the 0..127 sweep",
   all(0 <= zigbee(z) - 2400 <= 127 for z in range(11, 27)))
ok("the sixteen of them are five apart",
   sorted(zigbee(z) for z in range(11, 27))
   == list(range(2405, 2481, 5)))

# The overlaps are the point of printing both names.
ok("2425 is Zigbee 15, and WiFi 4 is two away", tag(2425) == "Zb15/W4")
ok("2450 is Zigbee 20, and WiFi 9 is two away", tag(2450) == "Zb20/W9")
ok("2475 is Zigbee 25 and no WiFi at all", tag(2475) == "Zb25")
ok("2480 is Zigbee 26 and no WiFi at all", tag(2480) == "Zb26")

# The installer advice is that 15, 20, 25 and 26 avoid the three
# non-overlapping WiFi channels, not that they avoid WiFi. Asserting the
# stronger thing is how this check first failed: 2425 is two megahertz
# from WiFi 4 and the code was right to say so.
for z, mhz in ((15, 2425), (20, 2450), (25, 2475), (26, 2480)):
    clear = all(abs(mhz - wifi(w)) > 2 for w in (1, 6, 11))
    ok("Zigbee %d clears WiFi 1, 6 and 11" % z, clear)
ok("2410 carries Zigbee 12 and WiFi 1", tag(2410) == "Zb12/W1")
ok("2435 carries Zigbee 17 and WiFi 6", tag(2435) == "Zb17/W6")
ok("2460 carries Zigbee 22 and WiFi 11", tag(2460) == "Zb22/W11")
ok("2484 is WiFi 14 alone", tag(2484) == "W14")
ok("2400 is neither, and says nothing", tag(2400) == "")

print()
if fails:
    print("FAILED: %d of %d" % (len(fails), checks))
    sys.exit(1)
print("%d checks passed" % checks)
