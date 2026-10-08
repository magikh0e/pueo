#!/usr/bin/env python3
"""Regenerate the surveillance-signatures data files from this header.

    python tools/extract_signatures.py --out ../surveillance-signatures
    python tools/extract_signatures.py --out ../surveillance-signatures --check

ESP32-DIV/SpotterSignatures.h is the source of truth. The published list at
magikh0e/surveillance-signatures says so, and its CONTRIBUTING.md tells
people not to send pull requests against the three data files because "they
will be overwritten by the next regeneration". That promise had nothing
behind it: no script in either repository performed the extraction, so the
snapshot was whatever had last been produced by hand, and the files could
drift from the header with nothing to notice.

What it regenerates, and what it leaves alone
---------------------------------------------
signatures.csv is pure data and is written whole.

signatures.json and README.md carry prose that is not derivable from the
header: each table's title and the sentence describing what it matches on,
the explanation of grading, the note about Mesh rows not being surveillance.
Those were written by a person and are not regenerable, so this rewrites the
rows inside them and leaves everything else exactly where it is. A generator
that flattened the prose would make the repository worse while claiming to
keep it current.

--check writes nothing and exits non-zero if the files are not what this
would write. That is what makes the drift visible.
"""
import argparse
import csv
import io
import json
import re
import sys
from collections import Counter, OrderedDict
from pathlib import Path

HDR = Path(__file__).resolve().parent.parent / "ESP32-DIV" / "SpotterSignatures.h"

# One entry per table: the C array name, and the CSV/JSON column names its
# rows carry. The order here is the order the published files use.
TABLES = OrderedDict([
    ("kMacSigs",     ("Address",)),
    ("kOuiSigs",     ("OUI",)),
    ("kSsidSigs",    ("Prefix", "Len")),
    ("kNameInSigs",  ("Needle", "Min")),
    ("kBleSigs",     ("Company", "Service")),
    ("kBleNameSigs", ("Prefix", "Len")),
    ("kBle128Sigs",  ("UUID",)),
    ("kMfgSigs",     ("Company", "Prefix")),
    ("kSvcDataSigs", ("Service", "Prefix")),
])

BLOCK = re.compile(r"/\*.*?\*/", re.S)
LINE = re.compile(r"//[^\n]*")


def strip_comments(text):
    return LINE.sub("", BLOCK.sub("", text))


def array_body(src, name):
    m = re.search(r"\b%s\[\]\s*=\s*\{" % re.escape(name), src)
    if not m:
        sys.exit("  !!  %s not found in the header" % name)
    i = m.end() - 1
    depth = 0
    while i < len(src):
        if src[i] == "{":
            depth += 1
        elif src[i] == "}":
            depth -= 1
            if depth == 0:
                return src[m.end():i]
        i += 1
    sys.exit("  !!  %s is not closed" % name)


def rows_of(body):
    """Each brace-balanced initialiser in the array, split at top level."""
    out, buf, depth, instr = [], "", 0, False
    i = 0
    while i < len(body):
        c = body[i]
        if instr:
            buf += c
            if c == "\\":
                buf += body[i + 1]
                i += 2
                continue
            if c == '"':
                instr = False
            i += 1
            continue
        if c == '"':
            instr = True
            buf += c
        elif c == "{":
            depth += 1
            if depth > 1:
                buf += c
        elif c == "}":
            depth -= 1
            if depth == 0:
                out.append(buf.strip())
                buf = ""
            else:
                buf += c
        elif depth >= 1:
            buf += c
        i += 1
    return [r for r in out if r.strip()]


def fields(row):
    """Top-level comma-separated fields of one initialiser."""
    out, buf, depth, instr = [], "", 0, False
    i = 0
    while i < len(row):
        c = row[i]
        if instr:
            buf += c
            if c == "\\":
                buf += row[i + 1]
                i += 2
                continue
            if c == '"':
                instr = False
            i += 1
            continue
        if c == '"':
            instr = True
            buf += c
        elif c in "{([":
            depth += 1
            buf += c
        elif c in "})]":
            depth -= 1
            buf += c
        elif c == "," and depth == 0:
            out.append(buf.strip())
            buf = ""
        else:
            buf += c
        i += 1
    if buf.strip():
        out.append(buf.strip())
    return out


def value(tok, kind):
    """A C initialiser field as the published files spell it."""
    tok = tok.strip()
    if tok.startswith('"'):
        return tok[1:-1]
    if tok.startswith("{"):                      # a byte array: an address
        bs = re.findall(r"0x([0-9A-Fa-f]{2})", tok)
        return ":".join(b.lower() for b in bs)
    if re.fullmatch(r"0x[0-9A-Fa-f]+", tok):     # a 16-bit id
        n = int(tok, 16)
        return "" if n == 0 and kind == "id" else "0x%04X" % n
    if re.fullmatch(r"\d+", tok):                # a length or minimum
        return "" if tok == "0" else tok
    return tok


# The two tables whose C row is id, prefix[N], prefixLen rather than one
# field per published column.
PREFIXED = ("kMfgSigs", "kSvcDataSigs")


def uuid128(arr):
    """A 16-byte little-endian UUID array, written the way people write it."""
    toks = [t.strip() for t in arr.strip("{} ").split(",") if t.strip()]
    vals = [int(t, 16) if t.lower().startswith("0x") else int(t) for t in toks]
    if len(vals) != 16:
        sys.exit("  !!  a 128-bit UUID with %d bytes: %r" % (len(vals), arr[:60]))
    h = "".join("%02x" % v for v in reversed(vals))
    return "-".join((h[:8], h[8:12], h[12:16], h[16:20], h[20:]))


def hex_prefix(arr, nbytes):
    """The significant bytes of a prefix[] array, as the files spell them."""
    toks = [t.strip() for t in arr.strip("{} ").split(",") if t.strip()]
    vals = [int(t, 16) if t.lower().startswith("0x") else int(t) for t in toks]
    return "".join("%02X" % v for v in vals[:nbytes])


def parse():
    src = strip_comments(io.open(HDR, encoding="utf-8", errors="replace").read())
    out = OrderedDict()
    for name, cols in TABLES.items():
        rows = []
        want = 6 if name in PREFIXED else len(cols) + 3
        for raw in rows_of(array_body(src, name)):
            f = fields(raw)
            if len(f) != want:
                sys.exit("  !!  %s: expected %d fields, got %d in %r"
                         % (name, want, len(f), raw[:70]))
            rec = OrderedDict()
            if name == "kBle128Sigs":
                # A 128-bit UUID is stored as the sixteen bytes BLE puts on
                # the air, which is least significant first. Printing them
                # in that order gives the UUID backwards, so they are
                # reversed and grouped 8-4-4-4-12 the way everyone writes
                # one. Byte order is the whole content of this field: a
                # reversed UUID matches nothing and looks fine.
                rec[cols[0]] = uuid128(f[0])
            elif name in PREFIXED:
                # id, prefix[N], prefixLen, kind, conf, label. The published
                # column is the significant part of the prefix alone:
                # printing all N bytes would publish the zero padding as
                # though it were part of the signature.
                rec[cols[0]] = value(f[0], "id")
                rec[cols[1]] = hex_prefix(f[1], int(f[2]))
            else:
                for col, tok in zip(cols, f):
                    rec[col] = value(
                        tok, "id" if col in ("Company", "Service") else "")
            rec["Kind"] = f[-3].split("::")[-1]
            rec["Conf"] = f[-2].split("::")[-1]
            rec["Label"] = value(f[-1], "")
            rows.append(rec)
        out[name] = rows
    return out


def build_csv(tables):
    buf = io.StringIO(newline="")
    w = csv.writer(buf, lineterminator="\n")
    w.writerow(["table", "match_on", "value", "qualifier", "kind",
                "confidence", "label"])
    for name, rows in tables.items():
        cols = TABLES[name]
        for r in rows:
            w.writerow([name, cols[0], r[cols[0]],
                        r[cols[1]] if len(cols) > 1 else "",
                        r["Kind"], r["Conf"], r["Label"]])
    return buf.getvalue()


def build_json(tables, old):
    doc = json.loads(old, object_pairs_hook=OrderedDict)
    doc["total"] = sum(len(r) for r in tables.values())
    kinds = OrderedDict(doc["kinds"])
    seen = {r["Kind"] for rows in tables.values() for r in rows}
    missing = seen - set(kinds)
    if missing:
        sys.exit("  !!  no description for kind(s) %s. Add them to the\n"
                 "      'kinds' object in signatures.json first: a kind with\n"
                 "      no sentence explaining it is not publishable."
                 % ", ".join(sorted(missing)))
    for t in doc["tables"]:
        t["rows"] = [OrderedDict(r) for r in tables[t["name"]]]
    return json.dumps(doc, indent=2, ensure_ascii=False) + "\n"


def build_readme(tables, old, kind_desc):
    """Rewrite the data rows and the counts; leave every sentence alone."""
    total = sum(len(r) for r in tables.values())
    counts = Counter(r["Kind"] for rows in tables.values() for r in rows)
    text = old

    # The kinds table: counts refreshed in place, order left as it is.
    seen_in_table = []

    def kind_row(m):
        kind = m.group(1)
        seen_in_table.append(kind)
        return "| `%s` | %s | %d |" % (kind, m.group(2).strip(),
                                       counts.get(kind, 0))
    text = re.sub(r"\| `(\w+)` \| ([^|]+)\| *\d+ *\|", kind_row, text)

    # A kind that exists in the header but has no row yet is appended rather
    # than inserted, so the established order does not shuffle under a
    # reader who knows it. Appending is also the honest place: it is new.
    for kind in (k for k in counts if k not in seen_in_table):
        if kind not in kind_desc:
            sys.exit("  !!  no description for kind %r" % kind)
        last = None
        for m in re.finditer(r"\| `(\w+)` \| [^|]+\| *\d+ *\|\n", text):
            if m.group(1) in seen_in_table:
                last = m
        if last is None:
            sys.exit("  !!  README: no kinds table to add %r to" % kind)
        row = "| `%s` | %s | %d |\n" % (kind, kind_desc[kind], counts[kind])
        text = text[:last.end()] + row + text[last.end():]
        seen_in_table.append(kind)

    # The data tables, found by their header row's first column.
    for name, rows in tables.items():
        cols = TABLES[name]
        head = "| " + " | ".join("`%s`" % c if False else c
                                 for c in list(cols) + ["Kind", "Conf", "Label"])
        pat = re.compile(
            r"(\|\s*" + r"\s*\|\s*".join(list(cols) + ["Kind", "Conf", "Label"])
            + r"\s*\|\n\|[\s|:-]+\|\n)((?:\|.*\n)*)", re.M)
        body = "".join(
            "| " + " | ".join(
                ["`%s`" % r[c] if r[c] else "" for c in cols]
                + [r["Kind"], r["Conf"], r["Label"]]) + " |\n"
            for r in rows)
        new, n = pat.subn(lambda m: m.group(1) + body, text, count=1)
        if n != 1:
            sys.exit("  !!  README: could not find the table for %s (%s)"
                     % (name, head))
        text = new

    text = re.sub(r"\*\*\d+ rows over (\w+) tables\*\*",
                  "**%d rows over \\1 tables**" % total, text)
    return text


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", required=True, help="the surveillance-signatures tree")
    ap.add_argument("--check", action="store_true",
                    help="write nothing; fail if the files are stale")
    a = ap.parse_args()
    out = Path(a.out)
    if not (out / "signatures.csv").is_file():
        sys.exit("not a surveillance-signatures tree: %s" % out)

    tables = parse()
    total = sum(len(r) for r in tables.values())
    print("%d signatures over %d tables, %d kinds"
          % (total, len(tables),
             len({r["Kind"] for rows in tables.values() for r in rows})))
    for name, rows in tables.items():
        print("  %-14s %3d" % (name, len(rows)))

    new_json = build_json(
        tables, io.open(out / "signatures.json", encoding="utf-8").read())
    want = {
        "signatures.csv": build_csv(tables),
        "signatures.json": new_json,
        "README.md": build_readme(
            tables,
            io.open(out / "README.md", encoding="utf-8",
                    newline="").read().replace("\r\n", "\n"),
            json.loads(new_json)["kinds"]),
    }

    stale = []
    for name, body in want.items():
        p = out / name
        have = io.open(p, encoding="utf-8", newline="").read().replace("\r\n", "\n")
        if have == body:
            print("  ok    %s is current" % name)
            continue
        stale.append(name)
        if a.check:
            print("  STALE %s" % name)
        else:
            # Each file keeps the line endings it already has: the CSV is
            # CRLF on disk because csv.writer put it there, the other two
            # are LF. Normalising either would turn the next review into a
            # whole-file diff that hides the rows that actually changed.
            io.open(p, "w", encoding="utf-8",
                    newline="\r\n" if name.endswith(".csv") else "\n"
                    ).write(body)
            print("  wrote %s" % name)

    if a.check and stale:
        print("\n%s differ from the header. Run without --check to "
              "regenerate." % ", ".join(stale))
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
