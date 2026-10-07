#!/usr/bin/env python3
"""The solder diagram agrees with the table it was drawn from.

`docs/pueo/solder-pads.svg` is a picture of the castellation table in
`build-guide.md`. A picture of a table is a copy of it, and a copy drifts.

It drifted immediately. The first version drew nineteen pads in a straight line
down the module's right edge, because that is how the table reads. The module
has fifteen a side and eight across the bottom, so the count turns the
bottom-right corner at 15 and SD1, SD0 and CLK are on the bottom edge. The six
targets were drawn correctly; three of the five hazards were not, and the
internal flash pins are the ones a drawing most needs to place right. Nothing
would have caught it. A photograph of the board did, a release later.

    python tools/check_solder_pads.py

Reads local files only; connects to nothing.

What it compares
----------------
Every numbered pad in the table, against every numbered pad in the drawing:
the same nineteen numbers, each carrying the same signal name, each classified
the same way. The drawing encodes the classification as colour, so that is what
is read: orange is a target, the bright foreground is a hazard, dim is neither.

It also checks the shape the corner implies: fifteen numbered pads down the
right edge, four along the bottom, and fifteen pad rectangles drawn on each
long edge with eight across the bottom. That last one is the assertion the
first version would have failed.
"""
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
GUIDE = os.path.join(REPO, "docs", "pueo", "build-guide.md")
SVG = os.path.join(REPO, "docs", "pueo", "solder-pads.svg")

# The signal names that appear in the table, as a shape rather than a list, so
# a new one does not silently fall out of the parse.
NAME = r"(?:IO\d+|GND|NC|TXD0|RXD0|SD\d|CLK)"
CELL = re.compile(r"\b(\d{1,2})\s+(%s)(?![\w])" % NAME)

RIGHT_X, BOTTOM_X, LEFT_X = '582', '628', '307'
TEXT = re.compile(r'<text x="(\d+)"[^>]*fill="([^"]*)"[^>]*>([^<]*)</text>')
RECT = re.compile(r'<rect x="([\d.]+)" y="([\d.]+)" width="(\d+)" height="(\d+)"')


def kind_of(fill):
    if "--orange" in fill:
        return "solder"
    if "--text-dim" in fill:
        return None
    if "--text," in fill or "--text)" in fill:
        return "hazard"
    return None


def read_table():
    """number -> (name, kind) from build-guide.md's castellation block."""
    txt = open(GUIDE, encoding="utf-8").read()
    i = txt.index("Counting castellations")
    block = txt[txt.index("```", i) + 3:]
    block = block[:block.index("```")]

    hits = list(CELL.finditer(block))
    out = {}
    for n, m in enumerate(hits):
        num = int(m.group(1))
        end = hits[n + 1].start() if n + 1 < len(hits) else len(block)
        span = block[m.end():end]
        kind = "solder" if "<-" in span else ("hazard" if "!!" in span else None)
        out[num] = (m.group(2), kind)
    return out


def read_svg():
    """number -> (name, kind) from the drawing, plus its geometry."""
    txt = open(SVG, encoding="utf-8").read()
    pads, right, bottom, left_named = {}, 0, 0, {}
    for x, fill, body in TEXT.findall(txt):
        m = re.match(r"\s*(\d{1,2})\s+(%s)\b" % NAME, body)
        if not m:
            continue
        num, name = int(m.group(1)), m.group(2)
        if x == RIGHT_X:
            pads[num] = (name, kind_of(fill)); right += 1
        elif x == BOTTOM_X:
            pads[num] = (name, kind_of(fill)); bottom += 1
        elif x == LEFT_X:
            left_named[num] = name

    geom = {"right": 0, "left": 0, "bottom": 0}
    for x, y, w, h in RECT.findall(txt):
        if w == "14" and h == "8":
            geom["right" if float(x) > 400 else "left"] += 1
        elif w == "8" and h == "14":
            geom["bottom"] += 1
    return pads, right, bottom, left_named, geom


def main():
    table = read_table()
    pads, n_right, n_bottom, left_named, geom = read_svg()
    problems = []

    missing = sorted(set(table) - set(pads))
    extra = sorted(set(pads) - set(table))
    if missing:
        problems.append("in the table but not the drawing: %s" % missing)
    if extra:
        problems.append("in the drawing but not the table: %s" % extra)

    for num in sorted(set(table) & set(pads)):
        tn, tk = table[num]
        sn, sk = pads[num]
        if tn != sn:
            problems.append("pad %d: table says %s, drawing says %s" % (num, tn, sn))
        elif tk != sk:
            problems.append("pad %d (%s): table says %s, drawing draws it %s"
                            % (num, tn, tk or "plain", sk or "plain"))

    # Numbers stop at the corner. One to fourteen is the same count under
    # either convention and holds every pad anyone solders to; past it the
    # drawing and the table name pads instead, because that is where the
    # two conventions disagree and where the flash pins are.
    if n_right != 14:
        problems.append("%d numbered pads on the right edge, expected 14"
                        % n_right)
    if n_bottom != 0:
        problems.append("%d numbered pads on the bottom edge, expected none: "
                        "past the corner they are named, not numbered"
                        % n_bottom)
    for side, want in (("right", 15), ("left", 15), ("bottom", 8)):
        if geom[side] != want:
            problems.append("%d pads drawn on the %s edge, expected %d"
                            % (geom[side], side, want))

    # The sixth target, the only one the guide records for the left edge.
    if left_named.get(10) != "IO25":
        problems.append("left pad 10 is %r in the drawing, expected IO25"
                        % left_named.get(10))

    if problems:
        for p in problems:
            print("  FAIL  " + p)
        print()
        print("FAILED: %d" % len(problems))
        return 1

    solder = sum(1 for _, k in table.values() if k == "solder")
    hazard = sum(1 for _, k in table.values() if k == "hazard")
    print("  ok    %d pads agree, %d targets and %d hazards, 15 a side and 8 "
          "across the bottom" % (len(table), solder + 1, hazard))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
