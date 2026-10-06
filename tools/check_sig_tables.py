#!/usr/bin/env python3
"""The per-table breakdown, not just the total.

check_sig_counts.py checks that the pages say 274. They did, and the
surveillance page still listed eight tables under a sentence promising nine,
with BLE advertised name missing altogether and the OUI row frozen at 118
against a table of 126. The total was right because two errors in the prose
happened to be invisible to a check that only ever added up one number.

So this one reads the breakdown. Every table named on a page has to carry
the row count the header actually has, every table in the header has to be
named, and the confidence split quoted for kOuiSigs has to be the real one.

    python tools/check_sig_tables.py

Reads source; needs no board. Like check_sig_counts.py it finds the website
through .publish.local and says nothing when there is none, because this
script ships inside pueo-<version>-src.zip where no site exists.

What it asserts
---------------
  per table       Each table's row count, wherever a page prints one, both
                  by its k-name on signatures.html and by its English label
                  on surveillance.html.

  completeness    A page claiming nine tables lists nine. This is the one
                  that would have caught the missing row, and the one a
                  total can never catch.

  confidence      The strong, likely and weak counts quoted for the OUI
                  table. Those move whenever a row is added at any grade,
                  and the row count can stay right while they go wrong.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIGS = ROOT / "ESP32-DIV" / "SpotterSignatures.h"
PUBLISH = ROOT / ".publish.local"

# The English name each page gives a table, against the array it describes.
LABELS = {
    "The whole address": "kMacSigs",
    "WiFi source MAC prefix": "kOuiSigs",
    "Network name, anchored": "kSsidSigs",
    "Network name, anywhere": "kNameInSigs",
    "BLE company or service": "kBleSigs",
    "BLE advertised name": "kBleNameSigs",
    "128-bit service UUID": "kBle128Sigs",
    "Manufacturer data": "kMfgSigs",
    "Service data": "kSvcDataSigs",
}

WORDS = {
    1: "one", 2: "two", 3: "three", 4: "four", 5: "five", 6: "six",
    7: "seven", 8: "eight", 9: "nine", 10: "ten", 11: "eleven",
    12: "twelve", 13: "thirteen", 14: "fourteen", 15: "fifteen",
    16: "sixteen", 17: "seventeen", 18: "eighteen", 19: "nineteen",
    20: "twenty",
}

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        FAILED.append(name)
        print("  FAIL  %-44s %s" % (name, detail))


def tables():
    """Rows and confidence split per table, comments excluded."""
    src = SIGS.read_text(encoding="utf-8", errors="replace")
    rows, conf = {}, {}
    for _typ, name, body in re.findall(
            r"static const (\w+) (k\w+)\[\] = \{(.*?)\n\};", src, re.S):
        b = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        b = re.sub(r"//[^\n]*", "", b)
        rows[name] = len(re.findall(r"^\s*\{", b, re.M))
        c = {}
        for g in re.findall(r"Conf::(\w+)", b):
            c[g] = c.get(g, 0) + 1
        conf[name] = c
    return rows, conf


def pages():
    if not PUBLISH.is_file():
        return None
    m = re.search(r"PUEO_PUBLISH_DIR\s*=\s*[\"']?([^\"'\n]+)",
                  PUBLISH.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return None
    raw = m.group(1).strip()
    if re.match(r"^/[a-zA-Z]/", raw):
        raw = raw[1] + ":" + raw[2:]
    d = Path(raw)
    return sorted(d.glob("*.html")) if d.is_dir() else None


def says(text, n):
    """Does the text give this number, as a numeral or as a word?"""
    if re.search(r"\b%d\b" % n, text):
        return True
    w = WORDS.get(n)
    return bool(w and re.search(r"\b%s\b" % w, text, re.I))


def main():
    rows, conf = tables()
    total = sum(rows.values())
    print("  %d rows over %d tables" % (total, len(rows)))
    print()

    found = pages()
    if found is None:
        print("  ...   no .publish.local, so no site to check")
        print()
        print("%d checks passed" % CHECKS)
        return 0

    for p in found:
        html = p.read_text(encoding="utf-8", errors="replace")
        flat = re.sub(r"<[^>]*>", "", html)

        # signatures.html names the arrays outright.
        byname = re.findall(r"\b(k\w+Sigs), (\d+) rows?\b", flat)
        for name, n in byname:
            ok("%s: %s says %s rows" % (p.name, name, n),
               name in rows and rows[name] == int(n),
               "the header has %s" % rows.get(name, "no such table"))

        # surveillance.html uses English labels in a fixed-width block.
        bylabel = [(lab, m.group(1)) for lab, arr in LABELS.items()
                   for m in [re.search(
                       re.escape(lab) + r"\s{2,}(\d+) rows?\.", flat)] if m]
        for lab, n in bylabel:
            ok("%s: %s says %s rows" % (p.name, lab, n),
               rows[LABELS[lab]] == int(n),
               "the header has %d" % rows[LABELS[lab]])

        # A page that counts the tables has to list that many.
        m = re.search(r"(?:are|over) (\w+) tables", flat)
        if m and (byname or bylabel):
            want = m.group(1)
            n = len(byname) + len(bylabel)
            claimed = next((k for k, v in WORDS.items() if v == want.lower()),
                           None)
            if claimed is None and want.isdigit():
                claimed = int(want)
            ok("%s: says %s tables and lists them all" % (p.name, want),
               claimed == n,
               "it lists %d" % n)

        # The confidence split for the OUI table, where a page quotes it.
        if re.search(r"\b(\d+) OUI rows\b", flat):
            c = conf["kOuiSigs"]
            for grade in ("Strong", "Likely", "Weak"):
                n = c.get(grade, 0)
                ok("%s: %s OUI rows are %s" % (p.name, n, grade.lower()),
                   says(flat, n),
                   "the page does not give %d anywhere" % n)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
