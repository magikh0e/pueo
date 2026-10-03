#!/usr/bin/env python3
"""Centred text that is wider than the panel, where showLine draws it.

Scope, because the title used to claim more than this does: showLine is the
gauge's own line helper and SignalGauge.cpp is its only caller, 9 calls of
which 4 resolve. Every other centred string on the device goes through
tft.drawCentreString and is check_centre_fits.py's subject, 34 call sites
across four files in fonts 1, 2 and 4. Reading this file's old first line
and concluding that centred text was covered is how the About page came to
be drawn in the smallest font on the device with nothing measuring it.

TFT_eSPI does not clip. A string too wide for the screen is not truncated
and does not wrap -- it is drawn anyway, running off both edges, and what
survives is the middle. The gauge's readout spent two releases reading
"7 dBm    best -45    143 se" on the 3.5" and nothing said so, because the
firmware was perfectly happy and the screenshot on the website was drawn
from a renderer that still had the old size.

It arrived with 0.4.7's font change. Those lines are drawn in font 1 at
PUEO_BODY_SIZE, which is 2 on the 3.5" -- so every glyph doubled, a 31
character line became 372 px, and the panel is 320.

Font 1 advances 6 px per character, whatever the glyph, so the width is
just 6 * size * length. That is the whole measurement. What takes the work
is knowing the length: half these strings are snprintf'd, so the format is
what exists in the source and the widest thing it can produce is what has
to fit. Substitutions are listed in WORST below and printed with any
failure, because a check whose assumptions are invisible is one nobody can
argue with when it is wrong.

    python tools/check_text_fits.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

from check_text_pitch import calls, functions

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

# One panel, because there is one panel. shared.h defines 320x480
# unconditionally: the 2.8" was dropped on 2026-09-26, so a 240 px entry here
# could only ever fail a release over a board that cannot be built.
# check_text_margins.py dropped its own copy at the time and this one was
# missed, which left the two checks measuring different hardware.
PANELS = [('3.5"', 320, 2)]

FONT1_ADVANCE = 6

# What a conversion is assumed to widen to.
#
# A plain int is eleven characters if you bound it by the type, and nothing
# in this UI is ever eleven characters -- they are RSSIs, counts of rows and
# percentages. Bounding by the type flags text that fits and sends somebody
# off to shorten it, which is the failure the grid-label check already
# learned once by measuring with an 8 px/char estimate.
#
# So these are stated assumptions rather than type bounds, and they are
# printed with every failure. A value that can exceed one of them wants its
# own line regardless of what this says.
WORST = [
    (r"%%", 1),
    (r"%\.(\d+)s", None),        # a precision gives the answer outright
    (r"%0?\d*l[ux]", 11),        # unsigned long: bounded by the type
    (r"%0?\d*l[di]", 11),
    (r"%0?\d*X", 2),             # the hex pairs in a MAC
    (r"%0?\d*[uxdi]", 6),        # a UI integer: -99999 and no wider
    (r"%[-0-9.]*f", 12),
]


def fmt_width(fmt):
    """Longest string this format can produce, or None if unknowable."""
    n = 0
    i = 0
    while i < len(fmt):
        if fmt[i] != "%":
            n += 1
            i += 1
            continue
        for pat, w in WORST:
            m = re.compile(pat).match(fmt, i)
            if m:
                n += int(m.group(1)) if w is None else w
                i = m.end()
                break
        else:
            m = re.compile(r"%[-0-9.]*l?[a-zA-Z]").match(fmt, i)
            if not m:
                n += 1
                i += 1
                continue
            return None          # %s of unknown length, and the like
    return n


# What characters each conversion can put on screen, for charging it in a
# proportional font. Signed forms include the minus; the hex forms include
# their own letter case and nothing else.
CONV_CHARS = [
    (r"%%", "%"),
    (r"%\.(\d+)s", None),          # a precision, but of unknown characters
    (r"%0?\d*l?x", "0123456789abcdef"),
    (r"%0?\d*l?u", "0123456789"),
    (r"%0?\d*l?[di]", "0123456789-"),
    (r"%0?\d*l?X", "0123456789ABCDEF"),
    (r"%[-0-9.]*f", "0123456789-."),
]


def fmt_pixels(fmt, font, size=1):
    """Widest pixel width this format can draw in `font`, or None.

    Walks the format the way fmt_width() does, so the two agree about what
    is unknowable, but accumulates pixels: literal runs measured from the
    font's own table, conversions charged at their widest possible
    character.
    """
    import tft_fonts

    total = 0
    i = 0
    while i < len(fmt):
        if fmt[i] != "%":
            w = tft_fonts.width(fmt[i], font, size)
            if w is None:
                return None
            total += w
            i += 1
            continue

        for pat, chars in CONV_CHARS:
            m = re.compile(pat).match(fmt, i)
            if not m:
                continue
            if chars is None:
                return None      # %.Ns of characters this cannot know
            # how many characters this conversion can produce, from the
            # same table fmt_width() uses, so the two cannot disagree
            n = fmt_width(m.group(0))
            if n is None:
                return None
            per = tft_fonts.widest(chars, font, size)
            if per is None:
                return None
            total += n * per
            i = m.end()
            break
        else:
            m = re.compile(r"%[-0-9.]*l?[a-zA-Z]").match(fmt, i)
            if not m:
                w = tft_fonts.width(fmt[i], font, size)
                if w is None:
                    return None
                total += w
                i += 1
                continue
            return None          # %s of unknown length, and the like
    return total


def literal(arg):
    """The text of a C string literal, adjacent parts joined. Else None."""
    parts = re.findall(r'"((?:[^"\\]|\\.)*)"', arg.strip())
    if not parts or not re.fullmatch(r'\s*(?:"(?:[^"\\]|\\.)*"\s*)+',
                                     arg.strip() + " "):
        return None
    return "".join(parts).replace('\\"', '"').replace("\\\\", "\\")


def size_of(arg, body_size):
    arg = arg.strip()
    if arg == "PUEO_BODY_SIZE":
        return body_size
    return int(arg) if re.fullmatch(r"\d+", arg) else None


def snprintf_fmt(body, var):
    """The format string the last snprintf into `var` used, if it is plain."""
    out = None
    for _, args in calls(body, "snprintf"):
        if len(args) >= 3 and args[0].strip() == var:
            lit = literal(args[2])
            if lit is not None:
                out = lit
    return out


def main():
    problems = []
    measured = 0

    # showLine(dst, cap, text, cx, y, size, colour, force)
    # drawCentreString(text, cx, y, font)
    for path in sorted(SKETCH.glob("*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        if "showLine(" not in src:
            continue
        for fname, line0, body in functions(src):
            for off, args in calls(body, "showLine"):
                if len(args) < 6:
                    continue
                text, size_arg = args[2], args[5]
                line = line0 + body[:off].count("\n")

                for panel, width, body_size in PANELS:
                    size = size_of(size_arg, body_size)
                    if size is None:
                        continue
                    lit = literal(text)
                    if lit is not None:
                        n, how = len(lit), '"%s"' % lit
                    else:
                        fmt = snprintf_fmt(body[:off], text.strip())
                        if fmt is None:
                            continue
                        n = fmt_width(fmt)
                        if n is None:
                            continue
                        how = '"%s" at its widest' % fmt
                    measured += 1
                    px = n * FONT1_ADVANCE * size
                    if px > width:
                        problems.append(
                            ("%s:%s()" % (path.name, fname), line, panel,
                             px, width, how))

    print("centred lines measured: %d" % measured)
    if measured == 0:
        print()
        print("FAIL: none. showLine or the function scanner stopped matching,")
        print("      and this is reporting green over nothing.")
        return 1

    print()
    if not problems:
        print("every one of them fits the panel it is centred on.")
        return 0

    for where, line, panel, px, width, how in problems:
        print("  %-30s line %-5d %s  %d px on a %d px panel"
              % (where, line, panel, px, width))
        print("  %-30s %s" % ("", how))
    print()
    print("FAILED: %d line(s) wider than the screen." % len(problems))
    print()
    print("TFT_eSPI does not clip or wrap. These are drawn off both edges and")
    print("only the middle survives. Shorten the text or split it in two.")
    print()
    print("Widths assumed for conversions: %s"
          % ", ".join("%s -> %s" % (p, w) for p, w in WORST if w))
    return 1


if __name__ == "__main__":
    sys.exit(main())
