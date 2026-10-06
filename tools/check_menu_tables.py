"""Check that every menu's label and icon tables are fully initialised.

The menus are parallel arrays: a `..._items[N]` of labels beside a
`..._icons[N]` of bitmap pointers, walked by the same index. Add an entry to
one and forget the other and C++ says nothing -- an array with fewer
initialisers than its size is legal, and the missing elements are null.
Neither -Wall nor -Wextra warns.

What happens next is not a blank row. displaySubmenu() hands the pointer
straight to TFT_eSPI's drawBitmap, which dereferences it, so the device
faults the moment that menu page is opened.

This is not hypothetical. Raising BT_PAGE1_FEATURES from 2 to 3 for the Fast
Pair entry left bluetooth_page1_icons with two initialisers, which would
have crashed on entry to the second Bluetooth page. It compiled clean.

    python tools/check_menu_tables.py
"""
import io
import os
import re
import sys

HERE = os.path.dirname(os.path.abspath(__file__))
SRC = os.path.join(os.path.dirname(HERE), "ESP32-DIV", "ESP32-DIV.ino")

# `const char *foo[BAR] = { ... };` for labels, `const unsigned char *foo[BAR]`
# for icons. Both are matched the same way.
TABLE = re.compile(
    r"const\s+(?:unsigned\s+char|char)\s*\*\s*(\w+)\s*\[\s*(\w+)\s*\]\s*=\s*\{(.*?)\}\s*;",
    re.S)
SIZE = re.compile(
    r"(?:const\s+int|static\s+constexpr\s+int)\s+(\w+)\s*=\s*(\d+)\s*;")


def count_initialisers(body):
    """Entries in a brace-initialiser list, ignoring comments and any
    trailing comma."""
    body = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
    body = re.sub(r"//[^\n]*", "", body)
    return len([p for p in body.split(",") if p.strip()])


def main():
    src = io.open(SRC, encoding="utf-8", newline="").read()
    sizes = {k: int(v) for k, v in SIZE.findall(src)}

    tables = {}
    for m in TABLE.finditer(src):
        name, dim, body = m.group(1), m.group(2), m.group(3)
        if dim not in sizes:
            continue                      # a literal or unknown bound
        tables[name] = (dim, sizes[dim], count_initialisers(body))

    if not tables:
        print("found no menu tables - has the file moved?", file=sys.stderr)
        return 1

    bad = []
    for name, (dim, want, got) in sorted(tables.items()):
        if got != want:
            bad.append("%s[%s] is %d but has %d initialiser%s"
                       % (name, dim, want, got, "" if got == 1 else "s"))

    # labels and icons for one menu must agree on their bound, or the two
    # tables are walked with different lengths
    pairs = 0
    for name, (dim, want, got) in sorted(tables.items()):
        if not name.endswith("_items"):
            continue
        for icons in (name[:-6] + "_icons", name[:-6] + "icons"):
            if icons in tables:
                pairs += 1
                if tables[icons][0] != dim:
                    bad.append("%s is sized %s but %s is sized %s"
                               % (name, dim, icons, tables[icons][0]))
                break

    for name, (dim, want, got) in sorted(tables.items()):
        mark = "ok " if got == want else "BAD"
        print("  %s %-26s %-28s %2d/%-2d" % (mark, name, dim, got, want))

    # ── and the labels have to fit the tiles they are drawn in ──────────
    #
    # The grid menus centre a label in a fixed tile and TFT_eSPI does not
    # clip: a label wider than its tile is drawn over whatever is beside it.
    # The 2.8" tile is 100 px and "Drone Detector" measures 95, so there is
    # one character of headroom on a panel nobody has booted -- which is
    # exactly the kind of margin that gets spent without anyone noticing.
    #
    # Measured against TFT_eSPI's own font table rather than an average
    # character width. An 8 px/char estimate made these look 17 px wider
    # than they are and would have had somebody shortening labels that fit.
    try:
        sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
        import render_screens
        fw, _ = render_screens.load_font16()
    except Exception as e:                      # no toolchain, no font table
        print()
        print("  skipped the label-width check: %s" % e)
        fw = None

    if fw is not None:
        def f2_width(t):
            return sum(fw[ord(c) - 32] for c in t if 32 <= ord(c) < 128)

        print()
        print("grid labels against their tile:")
        GRIDS = [("other_submenu_items", {"3.5\"": 145, "2.8\"": 100})]
        for table, tiles in GRIDS:
            m = re.search(table + r"\[[^\]]*\]\s*=\s*\{(.*?)\};", src, re.S)
            if not m:
                bad.append("%s: not found" % table)
                continue
            for label in re.findall(r'"([^"]+)"', m.group(1)):
                w = f2_width(label)
                worst = min(tiles.values())
                panel = [k for k, v in tiles.items() if v == worst][0]
                fits = w <= worst
                print("  %s %-16s %3d px   %s tile %d" %
                      ("ok " if fits else "BAD", label, w, panel, worst))
                if not fits:
                    bad.append("%r is %d px in a %d px tile on the %s panel"
                               % (label, w, worst, panel))

    # ── which entry is Back, derived rather than written down ──────────
    #
    # A handler that compares current_submenu_index against a literal is
    # correct until an entry is inserted above Back. The SubGHz handler said
    # 5, Back moved to 6, and the literal then named the new feature: that
    # entry went to the main menu and Back did nothing, because the switch
    # had no case for 6. Both from one constant, and it compiled.
    lines = src.split("\n")
    for i, ln in enumerate(lines):
        m = re.search(r"current_submenu_index == (\d+)\)", ln)
        if not m:
            continue
        # The Back branch is the one that leaves for the main menu.
        if "displayMenu()" not in "\n".join(lines[i:i + 8]):
            continue
        fn = "?"
        for j in range(i, -1, -1):
            g = re.match(r"(?:static )?void (handle\w+SubmenuButtons)\(\)",
                         lines[j])
            if g:
                fn = g.group(1)
                break
        bad.append("%s line %d compares Back against the literal %s; use "
                   "<menu>_NUM_SUBMENU_ITEMS - 1, or inserting an entry "
                   "above Back silently reassigns both"
                   % (fn, i + 1, m.group(1)))

    print()
    if bad:
        print("FAILED:", file=sys.stderr)
        for b in bad:
            print("  " + b, file=sys.stderr)
        return 1

    print("ok -- %d tables fully initialised, %d label/icon pairs agree"
          % (len(tables), pairs))
    print("a short icon table is a null pointer handed to drawBitmap, and "
          "nothing else catches it")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
