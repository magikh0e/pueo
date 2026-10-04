#!/usr/bin/env python3
"""No menu has more entries than the grid has slots, and every label fits one.

The submenu grid is three columns by four, so twelve tiles. The WiFi menu has
exactly twelve features. There is no spare slot, and a thirteenth would not
announce itself: drawMenuGrid() stops at GRID_SLOTS, so the feature would
simply not be on the screen and nothing would say so.

The grid was five rows and fifteen slots until the labels moved to the 16 px
font. Three lines of that need a 92 px tile, four rows is what fits, and
twelve is what four rows gives. The trade was deliberate and this is the part
that makes it safe.

    python tools/check_grid_capacity.py

Reads source; needs no board.

It also checks the labels. Every one has to wrap into at most GRID_LINES
lines of GRID_TEXT_W pixels, measured in the same font the panel uses, by the
same greedy rule the firmware runs. 'Probe Request Flood' is the one that
forced three lines: every other label in the firmware fits in two, and that
one needs 89 px in a 96 px tile as a pair. A label that cannot be wrapped is
drawn over its neighbours, because TFT_eSPI does not clip.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
sys.path.insert(0, HERE)

import render_screens as R  # noqa: E402

REPO = os.path.dirname(HERE)
INO = os.path.join(REPO, "ESP32-DIV", "ESP32-DIV.ino")

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def wrap(t, label, width, lines):
    """The firmware's gridWrap(), in Python."""
    words = label.split()
    out, cur = [], ""
    for w in words:
        trial = w if not cur else cur + " " + w
        if t.text_width(trial) <= width or not cur:
            cur = trial
        else:
            out.append(cur)
            cur = w
    if cur:
        out.append(cur)
    return out


def main():
    R.set_panel(35)
    fw, fg = R.load_font16()
    t = R.Tft(R.load_glcd(), fw, fg, R.load_bitmaps())

    src = open(INO, encoding="utf-8", errors="replace").read()

    def const(n):
        # The word boundary matters: OTHER_GRID_COLS contains GRID_COLS, and without it
        # this read the Detect grid's 2 columns as the submenu grid's 3.
        m = re.search(r"\bGRID_%s\b\s*=\s*(\d+)\s*;" % n, src)
        if not m:
            # Returning None here used to make `cols * rows` raise
            # TypeError, which says nothing about which constant went
            # missing or that the grid was renamed out from under this.
            print("FAIL: GRID_%s is not declared in the sketch." % n)
            print("      The grid constants moved or were renamed, and this")
            print("      check cannot measure a grid it cannot find.")
            sys.exit(1)
        return int(m.group(1))

    cols, rows = const("COLS"), const("ROWS")
    slots = cols * rows
    lines = const("LINES")
    width = R.GRID_TEXT_W
    print("grid %dx%d = %d slots, %d lines of %d px"
          % (cols, rows, slots, lines, width))
    print()

    # Every menu the grid draws, excluding the trailing Back entry.
    MENUS = {
        "WiFi": ["wifi_page0_items", "wifi_page1_items"],
        "Bluetooth": ["bluetooth_page0_items", "bluetooth_page1_items"],
        "NRF24": ["nrf_submenu_items"],
        "SubGHz": ["subghz_submenu_items"],
        "System": ["tools_submenu_items"],
        "RFID/NFC": ["rfid_submenu_items"],
        "GPS": ["gps_submenu_items"],
        "Detect": ["other_submenu_items"],
    }
    SKIP = {"Back to Main Menu", "Main Menu", "Back"}

    # A table that resolves to nothing satisfies "len(items) <= slots" and
    # every wrap test below, so a menu renamed or deleted would pass as a
    # menu that comfortably fits. Each table has to actually be there.
    missing = []
    for name, tables in sorted(MENUS.items()):
        for tb in tables:
            if not R.ino_table(tb):
                missing.append((name, tb))
    if missing:
        print("FAIL: %d menu table(s) resolved to nothing:" % len(missing))
        for name, tb in missing:
            print("    %-10s %s" % (name, tb))
        print()
        print("      An empty table passes every test below, because there")
        print("      is nothing in it to be too long or too wide. Either the")
        print("      table was renamed and this list needs updating, or the")
        print("      menu is gone and should come out of it.")
        sys.exit(1)

    worst = 0
    labels_seen = 0
    for name, tables in sorted(MENUS.items()):
        items = []
        for tb in tables:
            items += [x for x in R.ino_table(tb) if x not in SKIP]
        labels_seen += len(items)
        ok("%-10s %2d of %d slots" % (name, len(items), slots),
           len(items) <= slots,
           "%d entries would not be drawn" % (len(items) - slots))
        worst = max(worst, len(items))

    print()
    bad = []
    for name, tables in sorted(MENUS.items()):
        for tb in tables:
            for label in R.ino_table(tb):
                if label in SKIP:
                    continue
                ls = wrap(t, label, width, lines)
                over = [l for l in ls if t.text_width(l) > width]
                if len(ls) > lines or over:
                    bad.append((label, len(ls), max(t.text_width(l) for l in ls)))
    for label, n, w in bad:
        print("    %-24s %d lines, widest %d px" % (label, n, w))
    ok("every label wraps into the tile", not bad,
       "%d do not, and TFT_eSPI draws them over the neighbours" % len(bad))

    print()
    print("fullest menu uses %d of %d slots, over %d label(s) in %d menu(s)"
          % (worst, slots, labels_seen, len(MENUS)))
    if FAILED:
        print()
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
