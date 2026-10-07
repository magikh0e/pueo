#!/usr/bin/env python3
"""The website's menu listing is the menu.

firmware.html opens that block with "Every entry in the menus, as of
<version>", which is a claim about completeness, and nothing was checking it.
It was wrong in two ways at once: the heading said Tools, which the menu
stopped being when Settings and About were folded into System, and File
Transfer was missing from a list introduced as every entry.

That is the same failure as "one hundred and eight signatures" and for the
same reason: a fact copied out of the source into a place that does not
compile, with nothing to notice when the source moves.

    python tools/check_site_menu.py

Reads source; needs no board. Skips silently when the site is not on this
machine, which is the case inside pueo-<version>-src.zip.

What it does and does not assert
--------------------------------
Every label in the firmware's menu tables appears in the page's block for
that menu. Not the reverse, and not the order.

Not the reverse, because the page carries entries the tables do not: the
Settings rows are built by utils.cpp rather than by a label array, and the
page lists them for the reader's benefit. Flagging those would be flagging
the page for being more useful than the tables.

Not the order, because the page groups by what a reader wants next and the
tables are in screen order, and forcing those to agree would make a layout
decision into a build failure.

The "Back to Main Menu" rows are excluded: they are navigation, they appear
in every table, and no documentation should list them.
"""
import io
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
INO = ROOT / "ESP32-DIV" / "ESP32-DIV.ino"
PUBLISH = ROOT / ".publish.local"

# firmware table -> the heading the page groups it under. Several tables map
# to one heading: the paged WiFi and Bluetooth menus are two arrays each and
# one block on the page.
BLOCKS = {
    "WiFi": ["wifi_page0_items", "wifi_page1_items"],
    "Bluetooth": ["bluetooth_page0_items", "bluetooth_page1_items"],
    "Detect": ["other_submenu_items"],
    "System": ["tools_submenu_items"],
    # The radio menus were outside this check, and the page went stale in
    # exactly the gap: three screens were added to SubGHz while the page
    # kept claiming it listed every entry in the menus.
    "Sub-GHz: CC1101": ["subghz_submenu_items"],
    "2.4&nbsp;GHz: NRF24": ["nrf_submenu_items"],
    "NFC: PN532": ["rfid_submenu_items"],
}

# Rows that are navigation rather than features.
SKIP = {"Back to Main Menu", "Main Menu", "Back"}

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


def ino_at(version):
    """ESP32-DIV.ino as of tag v<version>, or None.

    None means "could not ask", not "does not match": no git, no repository,
    or no such tag. Every one of those is a reason to fall back and say so,
    never a reason to fail.
    """
    if not version:
        return None
    try:
        r = subprocess.run(
            ["git", "show", "v%s:ESP32-DIV/ESP32-DIV.ino" % version],
            cwd=str(ROOT), capture_output=True, text=True, timeout=20)
    except (OSError, subprocess.SubprocessError):
        return None
    if r.returncode != 0 or not r.stdout:
        return None
    return r.stdout


def parse_tables(src):
    out = {}
    for name, body in re.findall(
            r"const char \*(\w+)\[\w+\] = \{(.*?)\};", src, re.S):
        b = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        b = re.sub(r"//[^\n]*", "", b)
        out[name] = [x for x in re.findall(r'"([^"]*)"', b) if x not in SKIP]
    return out


def tables():
    """The tree's tables. Kept for the fallback paths."""
    return parse_tables(INO.read_text(encoding="utf-8", errors="replace"))


def site_page():
    if not PUBLISH.is_file():
        return None
    m = re.search(r"PUEO_PUBLISH_DIR\s*=\s*['\"]?([^'\"\n]+)",
                  PUBLISH.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return None
    raw = m.group(1).strip()
    if re.match(r"^/[a-zA-Z]/", raw):
        raw = raw[1] + ":" + raw[2:]
    p = Path(raw) / "firmware.html"
    return p if p.is_file() else None


def blocks_on_page(html):
    """{heading: text} for each <strong class="term">Heading</strong> block."""
    # The class used to be [A-Za-z/ -]+, which quietly excluded every
    # heading with a colon or a digit in it: Sub-GHz: CC1101, 2.4 GHz:
    # NRF24, NFC: PN532. Those three menus were unreachable here whatever
    # BLOCKS said, and the page went stale in that gap.
    #
    # What separates a heading from an inline term is the newline after the
    # closing tag, not the spelling, so match on that instead.
    heads = [(m.start(), m.group(1)) for m in
             re.finditer(r'<strong class="term">([^<]+)</strong>\s*\n',
                         html)]
    out = {}
    for i, (pos, name) in enumerate(heads):
        end = heads[i + 1][0] if i + 1 < len(heads) else len(html)
        out[name] = html[pos:end]
    return out


def main():
    global CHECKS
    page = site_page()
    if page is None:
        print("firmware.html not reachable; nothing to check "
              "(no .publish.local, or no site there)")
        return 0

    html = page.read_text(encoding="utf-8", errors="replace")
    body = blocks_on_page(html)

    # The block is introduced as a complete list. If that sentence goes, the
    # check is asserting something the page no longer claims.
    ok("the page still claims the list is complete",
       "Every entry in the menus" in html,
       "the completeness claim is gone, so this check is about nothing")

    # ── which version the page is claiming about ──────────────────────
    #
    # "Every entry in the menus, as of 0.4.43" is a claim about a released
    # image, not about the working tree. Reading the tables out of the tree
    # compares the page to code nobody can download yet, which is how three
    # SubGHz screens came to be described on a page headed "as of 0.4.41"
    # while 0.4.41 contained none of them.
    m = re.search(r"Every entry in the menus, as of ([0-9]+(?:[.][0-9]+)*)", html)
    claimed = m.group(1) if m else None
    ok("the page names the version it is claiming about", claimed is not None,
       "no version after 'as of', so there is nothing to compare against")

    src = ino_at(claimed)
    unreleased = {}
    if src is not None:
        t = parse_tables(src)
        print("  ..    comparing against tag v%s" % claimed)
        # Entries the tree has and the tag does not: menu rows added since
        # the release the page names. The page must not list these, and
        # that is the only reverse assertion made here.
        tree = tables()
        for name, labels in tree.items():
            extra = [x for x in labels if x not in t.get(name, [])]
            if extra:
                unreleased[name] = extra
    else:
        t = tables()
        # Not a failure. Before the tag exists the tree is what the image
        # being cut will contain, and inside the source archive there is no
        # repository to ask. Both are worth saying out loud, because the
        # check is weaker in those runs than it looks.
        print("  ..    no tag v%s; comparing against the working tree, so "
              "this run cannot catch a page that describes unreleased work"
              % claimed)

    for heading, names in BLOCKS.items():
        want = []
        for n in names:
            if n not in t:
                FAILED.append("%s: table %s not found in the sketch" % (heading, n))
                print("  FAIL  table %s is missing from ESP32-DIV.ino" % n)
                continue
            want += t[n]
        if not want:
            continue

        text = body.get(heading)
        CHECKS += 1
        if text is None:
            print("  FAIL  no %r block on the page; the menu is called that "
                  "in the firmware" % heading)
            FAILED.append(heading)
            continue
        plain = re.sub(r"<[^>]*>", "", text)
        missing = [x for x in want if x not in plain]
        if missing:
            print("  FAIL  %-10s %d entr%s not on the page: %s"
                  % (heading, len(missing),
                     "y" if len(missing) == 1 else "ies", missing))
            FAILED.append(heading)
        else:
            print("  ok    %-10s all %d entries listed" % (heading, len(want)))

    # ── and nothing on the page that the named release does not have ──
    #
    # This is the direction that was missing. The forward check passes
    # happily when the page lists more than the firmware, which is exactly
    # what "the site describes unreleased features" looks like.
    for heading, names in BLOCKS.items():
        block = body.get(heading)
        if block is None:
            continue
        for tbl in names:
            for label in unreleased.get(tbl, []):
                ok("%-14s does not promise %s" % (heading, label),
                   label not in block,
                   "the page lists %s under a heading reading \"as of %s\", "
                   "and v%s does not contain it: somebody downloading that "
                   "release does not get this"
                   % (label, claimed, claimed))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("That block says it is every entry in the menus. A feature")
        print("missing from it is a feature somebody reading the site does")
        print("not know exists.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
