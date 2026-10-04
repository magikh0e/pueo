#!/usr/bin/env python3
"""No source file carries a control byte it did not mean to.

Four times in one session a bash double-quoted string ate a backslash
escape and left the byte it names in a file. `\\b` becomes 0x08, a
backspace, and nothing shows it: the editor draws nothing, the diff looks
right, and git is content. Only `cat -A` or a byte-level search finds it.

Three of the four were harmless. The fourth was not, and that is the reason
this exists:

    check_sig_counts.py   w + r"<BS>(?!\\s+and<BS>)"
    check_stealth.py      r"%s features<BS>" % w

Both are the stale-count half of their check. Each asserts two things, that
the site states the current count and that it no longer states an older one,
and the second could never fire, because a regex wanting a backspace
character cannot match HTML. A page could carry the right count and a stale
one together and both checks passed. That is the same shape as added.html
saying "Three settings" while the device had four, which is what the
stale-count halves exist to prevent.

The other two were a comment in build.sh describing the bug it had itself
been given, and a word boundary in transcript_guard.py that made every
constant read as undeclared.

    python tools/check_control_bytes.py

Reads source; needs no board. The site is checked when .publish.local says
where it is, and skipped otherwise, which is the case inside
pueo-<version>-src.zip.

What counts as a control byte
-----------------------------
Anything below 0x20 that is not tab, newline or carriage return, plus DEL.
Form feed is included: nothing in this tree uses it as a page separator, and
allowing it because somebody might would be weakening the rule in advance of
a need.

A file that legitimately needs one would have to say so here. None does.
"""
import io
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
PUBLISH = REPO / ".publish.local"

ALLOWED = {0x09, 0x0A, 0x0D}

REPO_GLOBS = [
    "tools/*.py", "tools/*.sh",
    "ESP32-DIV/*.cpp", "ESP32-DIV/*.h", "ESP32-DIV/*.ino",
    "PueoBeacon/*.ino", "PueoBeacon/*.h",
    "docs/pueo/*.md", "docs/pueo/*.svg", "docs/pueo/*.scad",
    "*.md", "*.txt", ".gitignore",
]

SITE_GLOBS = [
    "site/*.html", "site/assets/*.css", "tools/*.py", "tools/*.sh", "*.md",
]


def site_root():
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
    return d.parent if d.is_dir() else None


def offenders(root, globs):
    out = []
    n = 0
    for g in globs:
        for p in sorted(root.glob(g)):
            if not p.is_file():
                continue
            n += 1
            raw = io.open(p, "rb").read()
            hits = {}
            for i, b in enumerate(raw):
                if (b < 0x20 and b not in ALLOWED) or b == 0x7F:
                    hits.setdefault(b, []).append(i)
            if hits:
                out.append((p, raw, hits))
    return out, n


def report(path, raw, hits, root):
    for b, places in sorted(hits.items()):
        for off in places[:4]:
            line = raw.count(b"\n", 0, off) + 1
            lo = raw.rfind(b"\n", 0, off) + 1
            hi = raw.find(b"\n", off)
            if hi < 0:
                hi = len(raw)
            text = "".join(
                chr(c) if 32 <= c < 127 else "<%02X>" % c
                for c in raw[lo:hi])
            print("    %s:%d  0x%02X" % (path.relative_to(root), line, b))
            print("        %s" % text.strip()[:76])
        if len(places) > 4:
            print("    %s  and %d more 0x%02X"
                  % (path.relative_to(root), len(places) - 4, b))


def main():
    bad = []
    scanned = 0

    found, n = offenders(REPO, REPO_GLOBS)
    scanned += n
    for p, raw, hits in found:
        bad.append((p, raw, hits, REPO))

    site = site_root()
    if site is not None:
        found, n = offenders(site, SITE_GLOBS)
        scanned += n
        for p, raw, hits in found:
            bad.append((p, raw, hits, site))

    if scanned == 0:
        print("FAIL: no files matched. The globs stopped matching and this")
        print("      is reporting green over nothing.")
        return 1

    print("files scanned: %d%s"
          % (scanned, "" if site is not None else "  (site not on this "
                                                 "machine; skipped)"))
    print()

    if not bad:
        print("none carries a control byte outside tab, newline and return.")
        return 0

    for p, raw, hits, root in bad:
        report(p, raw, hits, root)
    print()
    print("FAILED: %d file(s)." % len(bad))
    print()
    print("A control byte in source is almost always a backslash escape that")
    print("a shell ate: \\b becomes 0x08, \\t a real tab, \\a a bell. Nothing")
    print("shows it. Write the script to a file and run it by path rather")
    print("than passing it through a double-quoted shell string.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
