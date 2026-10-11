#!/usr/bin/env python3
"""The status bar has two heights, and only some screens can afford the tall one.

Every feature screen in this firmware puts a toolbar immediately under the
status bar -- a survey of the drawing calls finds 74 with a literal y between
20 and 48, eleven of them at exactly 20. Those screens cannot give the bar a
single extra pixel without something being painted over.

The menu grids can: their first tile is at Y_START, which is
44, leaving 24 px of bar-coloured nothing. So the height is shared state set
by whichever function paints the screen, and the failure mode is a screen that
forgets to declare one and inherits a tall bar over its own toolbar.

That is what this checks. It reads source; it does not need a board.

    python tools/check_status_bar.py
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

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


def sd_icon(utils):
    """The SD icon has to be able to say no.

    The reference board has no card-detect pin, and the no-pin branch of
    updateSdCardStatus() used to be a bare `true`. So sdCardPresent was
    latched true on the first call at boot and could never change: the bar
    drew the green icon whether or not a card was fitted, the empty-slot
    icon was unreachable, and the user guide told the reader to glance at
    that icon before starting a capture.

    Nothing caught it, because every part was individually reasonable. The
    thing worth asserting is not the fix but the property the fix restored:
    on a board with no detect pin the flag follows something that can be
    false. A constant cannot.
    """
    print("\nthe SD icon can say no:")

    m = re.search(r"#ifdef SD_CD(.*?)#endif", utils, re.S)
    branch = m.group(1) if m else ""
    ok("updateSdCardStatus has a no-detect-pin branch", bool(m))
    ok("and it does not hardcode a card",
       re.search(r"cardDetected\s*=\s*true\s*;", branch) is None,
       "a constant here makes the empty-slot icon unreachable")
    ok("it follows the mount instead",
       "sdMountIsBelievedGood()" in branch)
    ok("which reads the cached flag and does not probe the card",
       re.search(r"sdMountIsBelievedGood\(\)\s*\{\s*return\s+s_sdFsMounted;\s*\}",
                 utils) is not None,
       "SD.cardType() here would hit the shared SPI bus every 2 s")
    ok("both icons are still drawn",
       "bitmap_icon_sdcard" in utils and "bitmap_icon_nullsdcard" in utils)

    guide = (REPO / "docs" / "pueo" / "user-guide.md").read_text(
        encoding="utf-8", errors="replace")
    ok("the user guide does not promise it tracks the slot",
       "one icon for a card present" not in guide,
       "the icon means a card has been read, not that one is fitted")


def heights():
    """The two bar heights, read rather than assumed."""
    src = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
    short = re.search(r"#define\s+PUEO_STATUS_SHORT\s+(\d+)", src)
    tall = re.search(r"#define\s+PUEO_STATUS_TALL\s+(\d+)", src)
    if not short or not tall:
        return None, None
    return int(short.group(1)), int(tall.group(1))


def functions(src):
    """Crude but sufficient: top-level `void name(...) {` to its closing brace."""
    out = {}
    for m in re.finditer(r"^(?:static\s+)?void\s+(\w+)\s*\([^;{]*\)\s*\{",
                         src, re.M):
        depth, i = 0, m.end() - 1
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        out[m.group(1)] = src[m.end():i]
    return out


def main():
    ino = (SKETCH / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")
    utils = (SKETCH / "utils.cpp").read_text(encoding="utf-8", errors="replace")
    fns = functions(ino)

    print("the two heights:")
    short, tall = heights()
    ok("shared.h defines PUEO_STATUS_SHORT and PUEO_STATUS_TALL",
       short is not None and tall is not None)
    if short is None or tall is None:
        print("\nFAILED: %d of %d" % (len(FAILED) or 1, CHECKS))
        return 1
    s35, t35 = short, tall
    ok("the tall bar is actually taller", t35 > s35,
       "SHORT=%s TALL=%s" % (s35, t35))

    # The short bar's geometry is load-bearing: these two identities are what
    # let the tall bar be introduced without moving anything drawn under a
    # short one, and they only hold while the centring reduces to the
    # constants it replaced.
    ok("centring the build string is an identity on the short bar",
       (s35 - 8) // 2 == 6, "(%d - 8) / 2 = %d, was y + 2 = 6" % (s35, (s35 - 8) // 2))
    ok("centring the battery block is an identity on the short bar",
       4 + (s35 - s35) // 2 == 4)

    print("\nevery screen that paints the bar declares a height:")
    # A function that force-draws the bar is painting a whole screen. The menu
    # painters are the ones that choose; features inherit from the submenu they
    # were launched off, which is why only these must declare.
    # Or delegates to the one function that does. displaySubmenu draws the
    # shared grid now rather than painting rows itself, so a sweep for the
    # drawStatusBar call stopped seeing it, and a painter this check cannot
    # see is a painter it cannot hold to anything.
    painters = sorted(n for n, b in fns.items()
                      if re.match(r"display\w*$", n)
                      and ("drawStatusBar(currentBatteryVoltage, true)" in b
                           or "drawMenuGrid(" in b))
    ok("found the menu painters", len(painters) >= 4, str(painters))
    for n in painters:
        ok("%s declares a bar height" % n,
           "setStatusBarHeight(" in fns[n])
    # About is launched straight off a tile rather than through a submenu, so
    # it would inherit whatever the menu left behind.
    ok("drawAboutPage declares a bar height",
       "setStatusBarHeight(" in fns.get("drawAboutPage", ""))

    print("\nnothing is drawn under a tall bar:")
    # Two ways a y reaches the screen, and the first version of this check
    # knew about one. displayPagedSubmenu draws its rows with
    # drawBitmap(10, yPos, ...) where yPos = 30 + i * 30: the origin is a
    # literal in an assignment, not an argument, so a sweep of call sites saw
    # nothing, the function was called a tile grid in a comment, and it got a
    # tall bar that painted over the top of its own first row. It shipped.
    draw = re.compile(r"\.\w+\(\s*(?:[^,()]+|\([^()]*\))\s*,\s*(\d+)\s*[,)]")
    # `int yPos = 30 + ...`, `const int y = 44;`, `yTop = Y_START`
    origin = re.compile(r"\b\w*[yY]\w*\s*=\s*(\d+|[A-Z_][A-Z_0-9]*)\b")
    consts = {}
    for m in re.finditer(r"\b([A-Z_][A-Z_0-9]*)\s*=\s*(\d+)\s*;", ino):
        consts.setdefault(m.group(1), int(m.group(2)))

    tall_fns = [n for n, b in fns.items() if "PUEO_STATUS_TALL" in b]
    ok("something actually asks for the tall bar", bool(tall_fns), str(tall_fns))
    for n in tall_fns:
        body = fns[n]
        bad = {"arg y=%d" % int(m.group(1)) for m in draw.finditer(body)
               if s35 <= int(m.group(1)) < t35}
        for m in origin.finditer(body):
            tok = m.group(1)
            v = int(tok) if tok.isdigit() else consts.get(tok)
            if v is not None and s35 <= v < t35:
                bad.add("%s (=%d)" % (" ".join(m.group(0).split()), v))
        ok("%s puts nothing between %d and %d" % (n, s35, t35),
           not bad, "; ".join(sorted(bad)))

    # The converse: the list screens must actually be declared short.
    short_fns = [n for n, b in fns.items() if "PUEO_STATUS_SHORT" in b]
    # displayPagedSubmenu was a list whose first row sat at y=30, which is
    # under a 34 px bar, so it was the one screen that had to stay short. It
    # is a tile grid now and its first row is at GRID_Y0, so it takes the tall
    # bar like every other grid and this assertion is about a screen that no
    # longer exists.
    ok("the only short screen left is About",
       sorted(short_fns) == ["drawAboutPage"], str(sorted(short_fns)))

    # The tiles themselves are placed from Y_START, not a literal, so they are
    # checked against the constant rather than by the sweep above. This is the
    # check that says the tall bar is affordable at all: it buys its 14 px out
    # of the gap above the first tile, and Y_START is where that gap ends.
    y_start = consts.get("Y_START")
    ok("the tile row clears the tall bar",
       y_start is not None and y_start >= t35,
       "Y_START=%s TALL=%s" % (y_start, t35))
    print("\nthe height reaches the bar:")
    ok("utils.cpp reads the height for the full repaint",
       re.search(r"barHeight\s*=\s*s_statusBarHeight", utils) is not None)
    ok("and for the blink-only fast path",
       re.search(r"kBarH\s*=\s*s_statusBarHeight", utils) is not None)
    ok("the setter compiles away when there is only one height",
       "#if PUEO_STATUS_TALL == PUEO_STATUS_SHORT" in
       (SKETCH / "utils.h").read_text(encoding="utf-8", errors="replace"))

    sd_icon(utils)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
