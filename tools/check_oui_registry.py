#!/usr/bin/env python3
"""Every OUI in the table belongs to the vendor the label names.

An OUI is a claim about who built a radio, and Conf::Strong turns that claim
into a finding reported on a screen somebody reads to decide whether they
are being followed. The claim has one authority, which is the IEEE registry,
and until this existed nothing here consulted it. The blocks added from
Fieldwatch's catalog were carried over on that catalog's word.

That is not a criticism of the catalog. It is that "Fieldwatch says 00:0E:A5
is BLIP Systems" and "00:0E:A5 is BLIP Systems" are different statements,
and only the second one is worth putting in front of somebody.

    python tools/check_oui_registry.py --fetch     download the registry
    python tools/check_oui_registry.py             check against the cache

The registry is 3.8 MB and changes slowly, so it is cached rather than
fetched on every run, and the cache is not committed: it is somebody else's
data and it is one command to get.

What a failure means
--------------------
Three kinds, and they are not equally bad.

  not in the registry   The block is unassigned, or it is an MA-M/MA-S
                        assignment where IEEE holds the first 24 bits for a
                        reseller. Either way the label is asserting
                        something the registry does not say.

  assigned to somebody  The worst one. The table says Arlo and the registry
  else                  says someone else, which means a device gets
                        reported as a camera because a different company
                        happens to own that block.

  a name that does not  Usually harmless and worth eyeballing: "Sierra
  obviously match       Wireless" against "Sierra Wireless, Inc" is fine,
                        "Uniview" against a holding company is a judgement.
"""
import argparse
import csv
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIGS = ROOT / "ESP32-DIV" / "SpotterSignatures.h"
CACHE = Path(__file__).resolve().parent / ".oui-cache.csv"
URL = "https://standards-oui.ieee.org/oui/oui.csv"

ENTRY = re.compile(
    r"\{\s*\{\s*0x([0-9A-Fa-f]{2})\s*,\s*0x([0-9A-Fa-f]{2})\s*,"
    r"\s*0x([0-9A-Fa-f]{2})\s*\}\s*,\s*Kind::(\w+)\s*,\s*Conf::(\w+)\s*,"
    r'\s*"([^"]*)"\s*\}')

# A label is a human phrase, not a company name: "Iteris (BlueTOAD)" is
# Iteris, "FS battery?" is a guess by shape and names no vendor at all. The
# match is therefore on words rather than on the whole string, and anything
# that cannot be matched that way is reported for a human to read rather
# than failed.
NOISE = {"the", "and", "inc", "llc", "ltd", "corp", "co", "gmbh", "technologies",
         "technology", "tech", "systems", "system", "electronics", "wireless",
         "international", "holding", "holdings", "group", "limited", "company",
         "shenzhen", "hangzhou", "zhejiang", "beijing", "shanghai", "digital",
         "communications", "communication", "network", "networks", "america",
         "usa", "global", "industrial", "industries", "solutions", "devices",
         "device", "security", "safety", "labs", "lab"}


def words(s):
    s = re.sub(r"\(.*?\)", " ", s or "")
    s = re.sub(r"[^A-Za-z0-9]+", " ", s).lower()
    return [w for w in s.split() if w and w not in NOISE and len(w) > 2]


def load_registry():
    if not CACHE.exists():
        print("no cached registry at %s" % CACHE, file=sys.stderr)
        print("run: python tools/check_oui_registry.py --fetch", file=sys.stderr)
        return None
    out = {}
    with io.open(CACHE, encoding="utf-8", errors="replace", newline="") as fh:
        for row in csv.DictReader(fh):
            a = (row.get("Assignment") or "").strip().upper()
            if len(a) == 6:
                out[a] = (row.get("Organization Name") or "").strip()
    return out


def fetch():
    import urllib.request
    print("fetching %s" % URL)
    # A browser User-Agent, because the default one stopped working.
    # standards-oui.ieee.org sits behind a filter that answers urllib's
    # "Python-urllib/3.x" with HTTP 418, and curl with a rejection page that
    # is served as 200, so the failure arrives either as an odd status or as
    # 245 bytes of HTML where 3.8 MB of CSV should be. Both leave this check
    # unable to run, which is the state it was found in: it is the one thing
    # that consults the registry, and it had quietly become unusable.
    req = urllib.request.Request(URL, headers={
        "User-Agent": "Mozilla/5.0 (Windows NT 10.0; Win64; x64)",
        "Accept": "text/csv,*/*",
    })
    with urllib.request.urlopen(req, timeout=300) as r:
        data = r.read()
    # A filter page is small and starts with markup. Refuse it rather than
    # caching it: a cache full of HTML fails every row for the wrong reason.
    if len(data) < 1_000_000 or data.lstrip()[:1] == b"<":
        raise SystemExit(
            "the registry did not come back: %d bytes starting %r.\n"
            "That is a filter page, not the CSV. Fetch it in a browser and "
            "save it to %s if this persists."
            % (len(data), data.lstrip()[:60], CACHE))
    CACHE.write_bytes(data)
    print("cached %d bytes at %s" % (len(data), CACHE))


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--fetch", action="store_true")
    ap.add_argument("--all", action="store_true",
                    help="check Weak and Likely rows too, not just Strong")
    args = ap.parse_args()

    if args.fetch:
        fetch()
        return 0

    reg = load_registry()
    if reg is None:
        return 1
    print("registry: %d assignments" % len(reg))

    src = SIGS.read_text(encoding="utf-8", errors="replace")
    body = re.search(r"kOuiSigs\[\] = \{(.*?)\n\};", src, re.S)
    if not body:
        print("FAIL  kOuiSigs not found")
        return 1

    missing, mismatch, unmatched, ok_n, skipped, laa = [], [], [], 0, 0, 0
    for a, b, c, kind, conf, label in ENTRY.findall(body.group(1)):
        oui = (a + b + c).upper()
        pretty = "%s:%s:%s" % (a.upper(), b.upper(), c.upper())

        # Weak rows are deliberately "this hardware is also in other things",
        # so a label that does not match the registry owner is the point.
        if conf != "Strong" and not args.all:
            skipped += 1
            continue

        # Bit 1 of the first octet is the locally-administered flag. Such an
        # address is made up by whoever is transmitting, so it is not in the
        # registry and never will be, and several rows here exist precisely
        # because a tool ships a made-up default: Hak5's 02:C0:CA, the
        # Pwnagotchi's DE:AD:BE. Reporting those as unassigned would be the
        # check misunderstanding the table.
        if int(a, 16) & 0x02:
            laa += 1
            continue

        org = reg.get(oui)
        if org is None:
            missing.append((pretty, label, conf))
            continue

        lw, ow = words(label), words(org)
        if not lw:
            unmatched.append((pretty, label, org, "label names no vendor"))
        elif any(w in ow for w in lw) or any(w in lw for w in ow):
            ok_n += 1
        else:
            mismatch.append((pretty, label, org))

    print()
    for pretty, label, org in mismatch:
        print("  FAIL  %-11s table says %-22s registry says %s"
              % (pretty, repr(label), org))
    for pretty, label, conf in missing:
        print("  FAIL  %-11s %-22s is not an assigned MA-L block" % (pretty, repr(label)))
    for pretty, label, org, why in unmatched:
        print("  --    %-11s %-22s registry says %s   (%s)"
              % (pretty, repr(label), org, why))

    print()
    print("%d checked against the registry, %d agree" % (ok_n + len(mismatch) + len(missing), ok_n))
    if laa:
        print("%d locally-administered rows skipped. A made-up address is "
              "not in the registry by definition, and several of these rows "
              "exist because a tool ships one." % laa)
    if skipped:
        print("%d Weak/Likely rows skipped; --all includes them" % skipped)
    if unmatched:
        print("%d could not be matched by name and are shown above for reading"
              % len(unmatched))

    if args.all and (mismatch or missing):
        print()
        print("--all is informational: a Weak row names what the hardware is")
        print("probably in, not who made it, so disagreeing with the registry")
        print("is what those rows are for. Only Strong rows fail this check.")
        return 0

    if mismatch or missing:
        print()
        print("FAILED: %d block(s) do not belong to the vendor named."
              % (len(mismatch) + len(missing)))
        print()
        print("A Strong OUI is reported as a finding. If the registry")
        print("disagrees, the row is naming the wrong company or the block")
        print("is not assigned at all, and the fix is to correct the label,")
        print("grade it down, or drop it.")
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
