#!/usr/bin/env python3
"""The panel's dimensions are stated once, and everything follows them.

Three times now the same shape of bug has shipped: one fact, restated in
several places, corrected in some of them. The bus map. The pin map. Then
the screen dimensions -- 43 literals across four files, of which the menu
grid was fixed at bring-up and the other 42 were not, which is why every
feature drew into the top-left 240x320 of a 320x480 panel.

This asserts the fact now has one home, that both panels reach it, and that
the three files which must agree about the panel actually do. It reads
source; it does not need a board.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"
USER_SETUP = REPO / "Libraries" / "User_Setup cyd.h"

# Every file that draws. The sweep's first pass named only the big four and
# left Spotter and the Fast Pair scanner still 240 px wide, so this list is
# the whole set rather than the ones that seemed likely.
UI_FILES = ["wifi.cpp", "bluetooth.cpp", "subghz.cpp", "utils.cpp",
            "ducky.cpp", "Touchscreen.h", "menu.cpp", "KeyboardUI.cpp",
            "Spotter.cpp", "FastPairScan.cpp", "TrackerFollow.cpp",
            "TrackerHunt.cpp", "rfid.cpp", "gps.cpp",
            # The sketch draws too -- the About page, and the menu grid.
            # It was missing from this list, which is how the About page
            # kept a 216 px rule and a back hint at y=300 through an entire
            # sweep that existed to find exactly that.
            "ESP32-DIV.ino"]

# What the 43 looked like. Any of these coming back means a screen has gone
# back to believing it is 240x320.
BARE = [
    (re.compile(r"#\s*define\s+SCREENHEIGHT\s+\d"), "#define SCREENHEIGHT <literal>"),
    (re.compile(r"#\s*define\s+SCREEN_HEIGHT\s+\d"), "#define SCREEN_HEIGHT <literal>"),
    (re.compile(r"#\s*define\s+SCREEN_WIDTH\s+\d"), "#define SCREEN_WIDTH <literal>"),
    (re.compile(r"#\s*define\s+DISPLAY_WIDTH\s+\d"), "#define DISPLAY_WIDTH <literal>"),
    (re.compile(r"#\s*define\s+DISPLAY_HEIGHT\s+\d"), "#define DISPLAY_HEIGHT <literal>"),
    (re.compile(r"#\s*define\s+TFT_WIDTH\s+\d"), "#define TFT_WIDTH <literal>"),
    (re.compile(r"#\s*define\s+TFT_HEIGHT\s+\d"), "#define TFT_HEIGHT <literal>"),
    (re.compile(r"#\s*define\s+MAX_Y\s+\d"), "#define MAX_Y <literal>"),
    (re.compile(r"constexpr\s+int\s+SCREEN_WIDTH\s*=\s*\d"),
     "constexpr int SCREEN_WIDTH = <literal>"),
    # A settings screen carried `static const int SCREEN_W = 240` past two
    # sweeps: the patterns above name SCREEN_WIDTH and this is SCREEN_W, and
    # they name #define and constexpr and this is static const int. Both
    # halves of the name and both halves of the form. Match the shape.
    (re.compile(r"(?:static\s+)?(?:const|constexpr)\s+int\s+"
                r"SCREEN_(?:WIDTH|HEIGHT|W|H)\b\s*=\s*\d"),
     "SCREEN_W/H/WIDTH/HEIGHT declared as a literal"),
]

# A status-bar icon row anchored to a fixed x instead of to the right edge.
ICON_ROW = re.compile(r"static int iconX\[\w+\] = \{([^}]*)\}")


class Pre:
    """Enough preprocessor to evaluate the panel branches. Mirrors the one in
    check_pinmap.py deliberately: both answer 'what does a build with
    -DPUEO_PANEL_35=<n> actually see', and a shared helper that drifted would
    let both checks agree with each other while disagreeing with the
    compiler."""

    DIRECTIVE = re.compile(r"^\s*#\s*(\w+)\s*(.*)$")

    def __init__(self, seed):
        self.macros = dict(seed)

    def truth(self, expr):
        e = re.sub(r"defined\s*\(?\s*(\w+)\s*\)?",
                   lambda m: "1" if m.group(1) in self.macros else "0", expr)
        e = re.sub(r"\b[A-Za-z_]\w*\b",
                   lambda m: str(self.macros.get(m.group(0), 0))
                   if isinstance(self.macros.get(m.group(0)), int) else "0", e)
        e = e.replace("&&", " and ").replace("||", " or ").replace("!", " not ")
        # Every identifier above has been rewritten to a digit, so what is
        # left should be arithmetic and comparisons and nothing else. The
        # fullmatch is a whitelist, not a blacklist: anything containing a
        # name, a call, a string or a dot fails it and the branch is treated
        # as false rather than evaluated. That plus an empty __builtins__ is
        # why the eval below is safe on input that is, in any case, only ever
        # this repo's own headers.
        if not re.fullmatch(r"[\d\s()+\-*/%<>=!&|^~]*"
                            r"(?:\b(?:and|or|not)\b[\d\s()+\-*/%<>=!&|^~]*)*", e):
            return False
        try:
            return bool(eval(e, {"__builtins__": {}}, {}))
        except Exception:
            return False

    def run(self, path):
        stack = []
        for raw in path.read_text(encoding="utf-8", errors="replace").splitlines():
            m = self.DIRECTIVE.match(raw)
            if not m:
                continue
            d, rest = m.group(1), m.group(2).strip()
            live = all(f[0] for f in stack)
            if d in ("if", "ifdef", "ifndef"):
                if d == "ifdef":
                    cond = bool(rest) and rest.split()[0] in self.macros
                elif d == "ifndef":
                    cond = bool(rest) and rest.split()[0] not in self.macros
                else:
                    cond = self.truth(rest)
                stack.append([live and cond, cond, live])
            elif d == "elif" and stack:
                f = stack[-1]
                cond = (not f[1]) and self.truth(rest)
                f[0], f[1] = f[2] and cond, f[1] or cond
            elif d == "else" and stack:
                f = stack[-1]
                f[0], f[1] = f[2] and not f[1], True
            elif d == "endif" and stack:
                stack.pop()
            elif d == "define" and live:
                name, _, val = rest.partition(" ")
                if "(" in name:
                    continue
                val = re.sub(r"/\*.*?\*/", "", val).split("//")[0].strip()
                try:
                    self.macros[name] = int(val, 0)
                except ValueError:
                    self.macros[name] = val if val else True
            elif d == "undef" and live and rest:
                self.macros.pop(rest.split()[0], None)
            elif d == "include" and live:
                im = re.match(r'"([^"]+)"', rest)
                if im and (path.parent / im.group(1)).exists():
                    self.run(path.parent / im.group(1))

    def value(self, name):
        v = self.macros.get(name)
        for _ in range(10):
            if isinstance(v, int) and not isinstance(v, bool):
                return v
            if not isinstance(v, str):
                return None
            v = self.macros.get(v, v)
            try:
                return int(v, 0)
            except (ValueError, TypeError):
                if v not in self.macros:
                    return None
        return None


CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print(f"  ok    {name}")
    else:
        print(f"  FAIL  {name}{('  -- ' + detail) if detail else ''}")
        FAILED.append(name)


def main():
    print("one home for the dimensions:")

    shared = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
    ok("shared.h defines PUEO_SCREEN_W and PUEO_SCREEN_H",
       "PUEO_SCREEN_W" in shared and "PUEO_SCREEN_H" in shared)
    ok("and states them once, not per panel",
       "PUEO_PANEL_35" not in shared,
       "a panel branch is back in shared.h")

    for name in UI_FILES:
        path = SKETCH / name
        if not path.exists():
            continue
        src = path.read_text(encoding="utf-8", errors="replace")
        hits = []
        for pattern, label in BARE:
            for i, line in enumerate(src.splitlines(), 1):
                if pattern.search(line):
                    hits.append(f"{name}:{i} {label}")
        ok(f"{name} states no dimension as a literal",
           not hits, "; ".join(hits[:3]))

    print("\nthe sketch says 320x480:")
    pre = Pre({})
    pre.run(SKETCH / "shared.h")
    w, h = pre.value("PUEO_SCREEN_W"), pre.value("PUEO_SCREEN_H")
    ok("PUEO_SCREEN_W/H are 320x480", (w, h) == (320, 480), f"got {w}x{h}")
    ok("touch shares the display bus",
       pre.value("TOUCH_SHARES_TFT_SPI") == 1,
       "the XPT2046 is behind TOUCH_CS on this panel")

    # This was a cross-panel check and it is still worth running, because the
    # thing it caught was never really "two panels disagree". It was two files
    # disagreeing: shared.h computes the layout, TFT_eSPI drives the glass, and
    # nothing but this makes them say the same number.
    print("\nTFT_eSPI agrees with the sketch:")
    pre = Pre({})
    pre.run(USER_SETUP)
    tw, th = pre.value("TFT_WIDTH"), pre.value("TFT_HEIGHT")
    bl = pre.value("TFT_BL")
    driver = ("ST7796_DRIVER" if "ST7796_DRIVER" in pre.macros else
              "ILI9341_2_DRIVER" if "ILI9341_2_DRIVER" in pre.macros else "?")
    ok("User_Setup is ST7796_DRIVER at 320x480",
       (tw, th, driver) == (320, 480, "ST7796_DRIVER"),
       f"got {driver} at {tw}x{th}")
    ok("and it is the same size the sketch lays out for", (tw, th) == (w, h),
       f"User_Setup {tw}x{th} against shared.h {w}x{h}")
    ok("backlight is GPIO 27", bl == 27, f"got {bl}")
    # Getting this backwards renders every colour as its complement. The other
    # panel wanted it ON, which is how the value was ever in question.
    ok("inversion is OFF", "TFT_INVERSION_ON" not in pre.macros)

    # The copy the compiler actually reads.
    #
    # TFT_eSPI takes its configuration from User_Setup.h inside the installed
    # library, not from the file in this repo; build.sh copies one to the
    # other. Every check above reads the repo's copy, so all of them can pass
    # against an installed copy that says something else. That is not
    # hypothetical: back when this tree built two panels, every check above
    # passed while the installed file was still the old unconditional ST7796,
    # and 0.3.4's first cut shipped a 2.8" image carrying the 3.5"'s driver,
    # size and backlight pin. A check that reads only the source cannot see
    # that, which is the whole reason this one exists.
    #
    # Skipped when there is no install, so this still runs anywhere.
    installed = (REPO / ".arduino" / "user" / "libraries" / "TFT_eSPI"
                 / "User_Setup.h")
    if installed.exists():
        print("\nand the compiler reads the same file:")
        ok("the installed User_Setup.h matches this repo's",
           installed.read_bytes() == USER_SETUP.read_bytes(),
           "run tools/build.sh to re-sync it")

    print("\nstatus-bar icons follow the right edge:")
    stray = []
    for name in UI_FILES:
        path = SKETCH / name
        if not path.exists():
            continue
        for i, line in enumerate(
                path.read_text(encoding="utf-8", errors="replace").splitlines(), 1):
            m = ICON_ROW.search(line)
            if not m:
                continue
            for tok in m.group(1).split(","):
                tok = tok.strip()
                # 10 is the back arrow, anchored left. Anything else that is
                # a bare number is measured against a 240 px panel.
                if tok.isdigit() and tok != "10":
                    stray.append(f"{name}:{i} x={tok}")
    ok("no icon row is anchored to a fixed x", not stray, "; ".join(stray[:4]))

    # ── the satellite panel's no-sprite fallback ────────────────────────
    #
    # When no sprite allocates, the panel renders straight to the TFT inside
    # a viewport. That is from cifertech/ESP32-DIV#263. A viewport is the
    # panel geometry temporarily redefined, so it belongs in this file.
    gps = (SKETCH / "gps.cpp").read_text(
        encoding="utf-8", errors="replace")

    sets = re.findall(r"\btft\.setViewport\s*\(([^;]*)\)\s*;", gps)
    resets = re.findall(r"\btft\.resetViewport\s*\(\s*\)\s*;", gps)
    ok("every viewport is reset", len(sets) == len(resets),
       "%d setViewport, %d resetViewport: an unreset viewport puts the "
       "status bar inside the panel" % (len(sets), len(resets)))

    # The fifth argument defaults to true and that default is the whole
    # mechanism: it is what makes width() and height() report the viewport,
    # so renderPanelGx's panel-relative coordinates mean what it thinks.
    for a in sets:
        args = [x.strip() for x in a.split(",")]
        ok("  it takes the viewport datum (%d args)" % len(args),
           len(args) == 4 or (len(args) == 5 and args[4] == "true"),
           "a fifth argument of false makes width() the screen again")

    ok("  and the panel still draws without a sprite",
       "renderPanelGx(tft)" in gps,
       "the no-sprite path no longer renders the panel, so low memory "
       "leaves the screen unusable")

    print()
    if FAILED:
        print(f"FAILED: {len(FAILED)} of {CHECKS}")
        return 1
    print(f"{CHECKS} checks passed")
    return 0


if __name__ == "__main__":
    sys.exit(main())
