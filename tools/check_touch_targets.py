#!/usr/bin/env python3
"""Every tap target is on the panel, and big enough for a finger.

Two failures, both of which shipped.

A hit box reads like a coordinate and behaves like a dimension, so it
survives a panel change that every drawing call notices. The tile grids were
caught once: they used 100x60, which is the 2.8" tile, and on the 3.5" two
thirds of every tile did not answer a tap. The list menus had the same thing
and were not caught. Five of them tested x <= 220 on a panel 320 across, so
the right 100 px of every row was dead.

And a target can be on the panel and still be unusable. This one is 165 ppi,
so a pixel is 0.154 mm and the 30 px rows are 4.6 mm tall against a 7 mm
minimum for a finger. That is not a bug in the sense that something is
mispositioned; it is a decision, and this prints it rather than failing on
it, because changing a row pitch is a design change and not a fix.

    python tools/check_touch_targets.py

Reads source; needs no board.

What is checked
---------------
Any `button_x2` or `button_y2` that exceeds the panel, or that stops short of
it by more than a plausible margin, plus the submenu grid's own extent
computed from the GRID_* constants. Short of it is the interesting
direction: too wide is clipped by the touch driver and costs nothing, while
too narrow is a strip of screen that looks live and is not.

The third failure, which was this check's own
---------------------------------------------
It used to require a literal integer, and it spent a release matching
nothing. The list menus it was written for became tile grids with a shared
gridHit(), `button_x2 = 220` became `button_x2 = x_position + TILE_W`, and
the regex stopped seeing anything. Both its assertions are of the form
`not bad_x`, which is true of an empty list, so it printed "every row hit box
spans the panel" and "2 checks passed" having examined zero sites.

Two things follow, and the second is the one worth copying into other checks.

It resolves expressions now, against the integer constants the sketch
declares, because that is how the code is written.

And it counts what it measured, prints that count, and fails below a floor.
Not a zero test: renaming `button_x2` away took it from three horizontal
boxes to one, and one is not zero, so a zero test would have passed that
too. Losing most of the subject is the same failure as losing all of it and
harder to see. EXPECT_X and EXPECT_Y are deliberate numbers; when a menu
changes shape they want changing, after looking at what replaced it.
"""
import io
import ast
import operator
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SKETCH = ROOT / "ESP32-DIV"

_shared = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
W = int(re.search(r"#define\s+PUEO_SCREEN_W\s+(\d+)", _shared).group(1))
H = int(re.search(r"#define\s+PUEO_SCREEN_H\s+(\d+)", _shared).group(1))

# 3.5in diagonal at 320x480 is 165 ppi, so a pixel is 0.154 mm.
MM_PER_PX = 25.4 / (H / (3.5 / (1 + (W / float(H)) ** 2) ** 0.5))
FINGER_MM = 7.0

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


def strip_comments(src):
    out, i, n = [], 0, len(src)
    while i < n:
        if src.startswith("//", i):
            j = src.find("\n", i)
            j = n if j < 0 else j
            out.append(" " * (j - i))
            i = j
        elif src.startswith("/*", i):
            j = src.find("*/", i + 2)
            j = n if j < 0 else j + 2
            out.append(re.sub(r"[^\n]", " ", src[i:j]))
            i = j
        else:
            out.append(src[i])
            i += 1
    return "".join(out)


# Named integer constants the sketch declares, so a hit box written as an
# expression can be evaluated. `const int TILE_W = 145;` and
# `static constexpr int GRID_TILE_W = (PUEO_SCREEN_W - ...) / GRID_COLS;`
# are both this.
DECL = re.compile(
    r"^\s*(?:static\s+)?(?:const|constexpr)\s+(?:const\s+)?int\s+"
    r"(\w+)\s*=\s*([^;]+);", re.M)

ARITH = {ast.Add: operator.add, ast.Sub: operator.sub,
         ast.Mult: operator.mul, ast.FloorDiv: operator.floordiv,
         ast.Div: operator.floordiv}


def _arith(node, env):
    if isinstance(node, ast.Expression):
        return _arith(node.body, env)
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return node.value
    if isinstance(node, ast.Name):
        return env.get(node.id)
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        v = _arith(node.operand, env)
        return None if v is None else -v
    if isinstance(node, ast.BinOp) and type(node.op) in ARITH:
        a, b = _arith(node.left, env), _arith(node.right, env)
        if a is None or b is None:
            return None
        if type(node.op) in (ast.FloorDiv, ast.Div) and b == 0:
            return None
        return ARITH[type(node.op)](a, b)
    return None


def value_of(expr, env):
    """An integer, or None when something in it is not known."""
    try:
        tree = ast.parse(expr.strip().replace("/", "//"), mode="eval")
    except SyntaxError:
        return None
    return _arith(tree, env)


def constants(src, seed):
    """Resolve the sketch's int constants, repeating until nothing new."""
    env = dict(seed)
    pending = DECL.findall(src)
    for _ in range(8):
        moved = False
        for name, expr in pending:
            if name in env:
                continue
            v = value_of(expr, env)
            if v is not None:
                env[name] = v
                moved = True
        if not moved:
            break
    return env


def main():
    print("panel %dx%d, %.3f mm per pixel, finger minimum %.0f mm = %.0f px"
          % (W, H, MM_PER_PX, FINGER_MM, FINGER_MM / MM_PER_PX))
    print()

    bad_x, bad_y, narrow = [], [], []
    seen_x = seen_y = 0
    unresolved = []
    seed = {"PUEO_SCREEN_W": W, "PUEO_SCREEN_H": H}
    for p in sorted(SKETCH.glob("*.ino")) + sorted(SKETCH.glob("*.cpp")):
        src = strip_comments(p.read_text(encoding="utf-8", errors="replace"))
        env = constants(src, seed)
        # x_position and y_position are loop locals, and the widest value
        # either takes is the far column or the last row. Both are built
        # from constants, so the extremes are resolvable.
        env.setdefault("x_position", max(
            (v for k, v in env.items() if k.startswith("X_OFFSET")),
            default=None))
        # y_position is Y_START + row * Y_SPACING, and the row that can run
        # off the bottom is the last one. The item count and the column
        # count give it.
        if all(k in env for k in
               ("Y_START", "Y_SPACING", "other_NUM_SUBMENU_ITEMS",
                "OTHER_GRID_COLS")) and env["OTHER_GRID_COLS"]:
            rows = -(-env["other_NUM_SUBMENU_ITEMS"] //
                     env["OTHER_GRID_COLS"])
            env.setdefault("y_position",
                           env["Y_START"] + (rows - 1) * env["Y_SPACING"])
        for m in re.finditer(r"\bbutton_x2\s*=\s*([^;]+);", src):
            ln = src[:m.start()].count("\n") + 1
            v = value_of(m.group(1), env)
            if v is None:
                unresolved.append((p.name, ln, "button_x2",
                                   m.group(1).strip()))
                continue
            seen_x += 1
            if v > W:
                bad_x.append((p.name, ln, v, "past the right edge"))
            elif v < W - 40:
                bad_x.append((p.name, ln, v,
                              "%d px of every row is dead" % (W - v)))
        for m in re.finditer(r"\bbutton_y2\s*=\s*([^;]+);", src):
            ln = src[:m.start()].count("\n") + 1
            v = value_of(m.group(1), env)
            if v is None:
                unresolved.append((p.name, ln, "button_y2",
                                   m.group(1).strip()))
                continue
            seen_y += 1
            if v > H:
                bad_y.append((p.name, ln, v, "past the bottom edge"))
        # row pitch, where a list spaces its items by a literal
        for m in re.finditer(r"yPos\s*\+\s*(\d+)\s*;", src):
            v = int(m.group(1))
            ln = src[:m.start()].count("\n") + 1
            if v * MM_PER_PX < FINGER_MM:
                narrow.append((p.name, ln, v, v * MM_PER_PX))

    # The submenu grid, which is where a wrong extent would live now that
    # the list menus are gone. gridTileXY puts tile i at
    # GAP + col*(W+GAP), and the far column is the one that can fall short
    # of the panel or run past it.
    ino = strip_comments(
        (SKETCH / "ESP32-DIV.ino").read_text(encoding="utf-8",
                                             errors="replace"))
    g = constants(ino, seed)
    need = ("GRID_COLS", "GRID_ROWS", "GRID_TILE_W", "GRID_TILE_H",
            "GRID_GAP_X", "GRID_GAP_Y", "GRID_Y0")
    if all(k in g for k in need):
        right = g["GRID_GAP_X"] + (g["GRID_COLS"] - 1) * (
            g["GRID_TILE_W"] + g["GRID_GAP_X"]) + g["GRID_TILE_W"]
        bottom = g["GRID_Y0"] + (g["GRID_ROWS"] - 1) * (
            g["GRID_TILE_H"] + g["GRID_GAP_Y"]) + g["GRID_TILE_H"]
        seen_x += 1
        seen_y += 1
        if right > W:
            bad_x.append(("ESP32-DIV.ino", 0, right,
                          "the grid's far column runs past the panel"))
        elif right < W - 40:
            bad_x.append(("ESP32-DIV.ino", 0, right,
                          "%d px to the right of the grid is dead"
                          % (W - right)))
        if bottom > H:
            bad_y.append(("ESP32-DIV.ino", 0, bottom,
                          "the grid's last row runs off the bottom"))
        tile_mm = min(g["GRID_TILE_W"], g["GRID_TILE_H"]) * MM_PER_PX
        if tile_mm < FINGER_MM:
            narrow.append(("ESP32-DIV.ino", 0,
                           min(g["GRID_TILE_W"], g["GRID_TILE_H"]), tile_mm))
    else:
        unresolved.append(("ESP32-DIV.ino", 0, "the grid",
                           "GRID_* constants did not resolve"))

    print("  hit boxes measured: %d horizontal, %d vertical"
          % (seen_x, seen_y))
    for name, ln, what, expr in unresolved:
        print("    unresolved  %-18s:%-5d %s = %s" % (name, ln, what, expr))
    print()

    for f, ln, v, why in bad_x:
        print("    %-18s:%-5d button_x2 = %-5d %s" % (f, ln, v, why))
    ok("every row hit box spans the panel", not bad_x,
       "%d do not" % len(bad_x))

    for f, ln, v, why in bad_y:
        print("    %-18s:%-5d button_y2 = %-5d %s" % (f, ln, v, why))
    ok("and none runs off the bottom", not bad_y, "%d do" % len(bad_y))

    # Reported, not failed. A row pitch is a design decision.
    print()
    if narrow:
        print("tap targets below %.0f mm, which is a decision rather than a "
              "fault:" % FINGER_MM)
        seen = set()
        for f, ln, v, mm in narrow:
            if (f, v) in seen:
                continue
            seen.add((f, v))
            print("    %-18s %2d px = %.2f mm" % (f, v, mm))
        print("    (%d sites)" % len(narrow))
    else:
        print("every tap target is at least %.0f mm" % FINGER_MM)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("A hit box is a dimension wearing a coordinate's clothes. It")
        print("does not move when the panel does, and nothing on screen looks")
        print("wrong when it is stale.")
        return 1
    # A floor, not a zero test. Renaming button_x2 away took this from
    # three horizontal boxes to one and it still passed, because one is not
    # zero. Losing most of the subject is the same failure as losing all of
    # it, only harder to see. These numbers are deliberate: when a menu
    # changes shape, change them, having first looked at what replaced it.
    EXPECT_X, EXPECT_Y = 3, 3
    if seen_x < EXPECT_X or seen_y < EXPECT_Y:
        print()
        print("FAIL: this measured %d horizontal and %d vertical hit box(es),"
              % (seen_x, seen_y))
        print("      and expected at least %d and %d." % (EXPECT_X, EXPECT_Y))
        print("      Both assertions above are over an empty list, so they")
        print("      are true for want of anything to be false about.")
        print()
        print("      That is how this check spent a release reporting green:")
        print("      it wanted a literal, the hit boxes became expressions")
        print("      over TILE_W, and nothing said it had lost its subject.")
        print("      Whatever replaced them needs teaching to this scan.")
        return 1

    print("%d checks passed, over %d hit box(es)" % (CHECKS, seen_x + seen_y))
    return 0


if __name__ == "__main__":
    sys.exit(main())
