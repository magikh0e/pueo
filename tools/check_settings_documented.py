#!/usr/bin/env python3
"""Every Settings row is named somewhere on the website.

check_site_menu.py holds the firmware's menu tables to the page's listing,
and says in its own docstring that the Settings rows are outside it: it
reads the feature menus, and Settings is drawn from its own switch tables.

That gap cost twelve releases. JSON logging arrived at 0.4.19 and reached
neither the site nor docs/, while added.html went on saying "Three settings
the device did not have" and the device had four. Nothing could have
noticed: the one check that reads the site reads menu tables, and this is
not one.

So the row labels are read out of utils.cpp's switch tables, and each has to
appear somewhere in the site's pages. Not which page, and not in what words
around it: a label on the device that appears nowhere on the site is the
failure this exists for.

The reverse is not checked, for the reason check_site_menu.py gives about
its own direction. A page may say more than the screen does.

    python tools/check_settings_documented.py

Reads source; needs no board. Skips silently when the site is not on this
machine, which is the case inside pueo-<version>-src.zip.

What it reads
-------------
`{"Label", &AppSettings::field, ...}` out of the SwitchRow tables, plus the
two rows rowLabel() returns as literals because they open a page rather
than toggle a field. Read rather than listed here: a list written into this
file would have to be kept in step with the thing it is checking, which is
the failure it exists to catch.
"""
import io
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"
PUBLISH = REPO / ".publish.local"
SOURCE = SKETCH / "utils.cpp"

# {"Stealth Mode", &AppSettings::stealthMode, nullptr},
SWITCH = re.compile(r'\{\s*"([^"]+)"\s*,\s*&AppSettings::')

# The rows rowLabel() hands back directly: they open a page rather than
# toggle a field, so they are not in a SwitchRow table.
LITERAL = re.compile(r'return\s+"([^"]+)"\s*;')

# Not settings in the sense that wants documenting.
SKIP = {"Back", "Back to Main Menu"}


def site_dir():
    if not PUBLISH.is_file():
        return None
    m = re.search(r"PUEO_PUBLISH_DIR\s*=\s*['\"]?([^'\"\n]+)",
                  PUBLISH.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return None
    raw = m.group(1).strip()
    if re.match(r"^/[a-zA-Z]/", raw):
        raw = raw[1] + ":" + raw[2:]
    d = Path(raw)
    return d if d.is_dir() else None


def rows():
    src = io.open(SOURCE, encoding="utf-8", errors="replace").read()
    out = []
    for label in SWITCH.findall(src):
        if label not in SKIP and label not in out:
            out.append(label)
    m = re.search(r"static const char\* rowLabel\(int i\)\s*\{(.*?)\n\}",
                  src, re.S)
    if m:
        for label in LITERAL.findall(m.group(1)):
            if label not in SKIP and label not in out:
                out.append(label)
    return out


def main():
    labels = rows()
    if not labels:
        print("FAIL: no Settings rows found in %s." % SOURCE.name)
        print("      The SwitchRow tables stopped matching, or the screen is")
        print("      built some other way now. This is asserting nothing.")
        return 1

    site = site_dir()
    if site is None:
        print("site not on this machine; skipped (%d row(s) would be checked)"
              % len(labels))
        return 0

    pages = sorted(site.glob("*.html"))
    if not pages:
        print("FAIL: no pages under %s." % site)
        return 1
    text = "\n".join(
        io.open(p, encoding="utf-8", errors="replace").read() for p in pages
    ).lower()

    missing = [l for l in labels if l.lower() not in text]

    print("Settings rows on the device: %d, across %d page(s)"
          % (len(labels), len(pages)))
    for l in labels:
        print("  %s  %s" % ("ok  " if l not in missing else "FAIL", l))
    print()

    if missing:
        print("FAILED: %d row(s) the device has and the site never names:"
              % len(missing))
        for l in missing:
            print("    %s" % l)
        print()
        print("A setting nobody can read about is one nobody will find.")
        print("added.html is where the others are described, and the count in")
        print("that section's heading wants checking at the same time.")
        return 1

    print("every Settings row is named somewhere on the site.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
