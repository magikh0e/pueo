#!/usr/bin/env python3
"""A doc that quotes a check script's assertion count quotes the real one.

Several docs say how much work a check does, because the number is the point:
"50,293 checks" is the difference between a parser somebody eyeballed and a
parser somebody held to account. Two of those numbers had gone stale.
enclosure-logo.md said check_logo_scale.py runs 14 and it runs 15.
eapol-capture.md said check_eapol.py runs 120,301 and it runs 124,315.

Both drifted the same way: an assertion was added to the script, the script
still passed, and nothing was reading the sentence that described it. That is
the same failure as check_sig_counts.py's signature total and
check_doc_versions.py's filenames, in the one place nobody thought to look,
which is the documentation of the checks themselves.

    python tools/check_doc_counts.py

Reads source and runs the scripts it finds quoted; needs no board.

This one is slow, around 40 seconds, because the only honest way to know what
a script asserts is to run it. check_logo_scale.py is most of that: it
renders the artwork three times. The alternative is to recompute each total
here, which means a second copy of the logic that goes wrong independently,
so the slow version is the correct one.

A claim about a script that no longer exists is left alone. eapol-capture.md
quotes 80,134 for tools/check_eapol_locate.py, and says two paragraphs later
that the script became check_eapol.py. That is a progress log describing what
was true at that step, not a claim about the tree as it stands.
"""
import io
import re
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TOOLS = ROOT / "tools"
PUBLISH = ROOT / ".publish.local"

DOCS = sorted((ROOT / "docs" / "pueo").glob("*.md"))
DOCS += [ROOT / n for n in ("README.md", "PUEO.md", "CONTRIBUTING.md")]
DOCS = [d for d in DOCS if d.is_file()]

# Counts that are not a script's assertion total. "80,000 random frames" is
# not one of these because it does not say "checks", but a doc may yet talk
# about checks in a sentence that is not quoting a total.
IGNORE = ()

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



def site_index():
    """index.html, if the site is on this machine.

    Same lookup as check_sig_counts.py and make_release.sh, so there is
    one place that knows where the site is. None when it is not here,
    which is the case inside pueo-<version>-src.zip.
    """
    if not PUBLISH.is_file():
        return None
    m = re.search(r"PUEO_PUBLISH_DIR\s*=\s*['\"]?([^'\"\n]+)",
                  PUBLISH.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return None
    raw = m.group(1).strip()
    if re.match(r"^/[a-zA-Z]/", raw):
        raw = raw[1] + ":" + raw[2:]
    p = Path(raw) / "index.html"
    return p if p.is_file() else None


def claims():
    """[(doc, script_or_None, claimed, quoted)] for every count in the docs.

    Every count, attributed rather than filtered. The first version took
    counts within a fixed distance of a script's name, which silently dropped
    two: spoofers.md names check_ble_adv.py four lines before saying 1095, and
    tracker-follow.md puts its total at the end of a section. A check that
    quietly covers six of eight claims is worse than no check, because it
    reports success over the two it could not see.

    So a count is attributed to the nearest script named before it, at any
    distance, and a count with nothing named before it comes back with None
    and fails. That turns "I could not tell what this is about" into a result
    somebody reads instead of a silence.

    Markdown wraps, so "runs 80,111\\nchecks" is one claim across two lines
    and a line-oriented search misses it. The text is flattened first.
    """
    out = []
    for d in DOCS:
        t = io.open(d, encoding="utf-8", errors="replace").read()
        flat = " ".join(t.split())
        named = [(m.start(), m.group(1)) for m in
                 re.finditer(r"(?:tools/)?(check_\w+\.py)", flat)]
        for c in re.finditer(r"\b(\d[\d,]*)\s+checks\b", flat):
            if c.group(0) in IGNORE:
                continue
            before = [s for pos, s in named if pos < c.start()]
            out.append((d, before[-1] if before else None,
                        int(c.group(1).replace(",", "")), c.group(0)))
    return out


def reported(script):
    """What the script says its own total is, or None.

    Scripts say it two ways: "N checks passed" at the end of a suite, and
    "ok -- N checks" from a single fuzz pass. The last such number in the
    output is the total either way.
    """
    p = TOOLS / script
    try:
        r = subprocess.run([sys.executable, str(p)], cwd=ROOT,
                           capture_output=True, text=True, timeout=600)
    except subprocess.TimeoutExpired:
        return None
    nums = re.findall(r"(\d[\d,]*)\s+checks", r.stdout + r.stderr)
    if not nums:
        return None
    return int(nums[-1].replace(",", ""))


def main():
    found = claims()
    if not found:
        print("no doc quotes a check script's count; nothing to check")
        return 0

    # One run per script however many docs quote it.
    wanted = sorted({s for _, s, _, _ in found if s})
    live = [s for s in wanted if (TOOLS / s).is_file()]
    gone = [s for s in wanted if s not in live]

    print("%d counts quoted across %d docs, naming %d scripts"
          % (len(found), len({d for d, _, _, _ in found}), len(wanted)))
    for s in gone:
        print("    skipping %s: no such script, so the claim is history" % s)
    print()

    # A count nothing can be pinned to. Not skipped: the point of the check
    # is that every number in prose has something reading it.
    for d, s, claimed, quoted in found:
        if s is None:
            ok("%s: %s" % (d.name, quoted), False,
               "no check script is named before it, so nothing can verify it")
    print()

    print("running them, which is the slow part:")
    totals = {}
    for s in live:
        totals[s] = reported(s)
        print("    %-26s %s" % (s, "%d checks" % totals[s]
                                if totals[s] is not None else "no total found"))
    print()

    for d, s, claimed, quoted in found:
        if s is None or s in gone:
            continue
        name = "%s: %s runs %s" % (d.name, s, quoted)
        got = totals.get(s)
        if got is None:
            ok(name, False, "the script printed no total, so nothing to compare")
            continue
        ok(name, got == claimed, "it runs %d" % got)

    print()
    page = site_index()
    if page is None:
        print("the site is not reachable; skipping its script count")
    else:
        n = len(sorted(TOOLS.glob("check_*.py")))
        html = page.read_text(encoding="utf-8", errors="replace")
        m = re.search(r">(\d+)</span>\s*host check scripts", html)
        if m is None:
            ok("index.html states the script count", False,
               "no sentence of the form 'N host check scripts' on the page")
        else:
            ok("index.html says %d host check scripts" % n,
               int(m.group(1)) == n, "it says %s" % m.group(1))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        print()
        print("A number in prose reads as authoritative because somebody")
        print("once counted. Update the sentence, or say why the old number")
        print("is the one that belongs there.")
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
