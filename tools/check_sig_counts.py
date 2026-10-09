#!/usr/bin/env python3
"""The documented signature count is the real one.

added.html said "one hundred and eight signatures over nine kinds" for
months after it stopped being true, and the user guide said nothing at all.
Nobody noticed, because a number in prose has nothing checking it and
reads as authoritative precisely because somebody once counted.

The same thing in a different shape as check_stealth.py's doc rules and
check_doc_versions.py's filenames: a fact copied out of the source, into a
place that does not compile.

    python tools/check_sig_counts.py

Reads source; needs no board.

The website
-----------
added.html lives in a separate repository, and this checks it when it can
find it and says nothing when it cannot. The path comes from
.publish.local, which make_release.sh already reads for the same reason, so
there is one place that knows where the site is.

Skipping silently is deliberate rather than lazy: this script ships inside
pueo-<version>-src.zip, where there is no site and no .publish.local, and a
release verification that failed because somebody else's website was not on
the disk would be a check about nothing.
"""
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SIGS = ROOT / "ESP32-DIV" / "SpotterSignatures.h"
GUIDE = ROOT / "docs" / "pueo" / "user-guide.md"
PUBLISH = ROOT / ".publish.local"

WORDS = {
    108: "one hundred and eight", 200: "two hundred",
    266: "two hundred and sixty-six", 267: "two hundred and sixty-seven",
    268: "two hundred and sixty-eight", 269: "two hundred and sixty-nine",
    270: "two hundred and seventy", 271: "two hundred and seventy-one",
    272: "two hundred and seventy-two", 273: "two hundred and seventy-three",
    274: "two hundred and seventy-four", 275: "two hundred and seventy-five",
    276: "two hundred and seventy-six", 277: "two hundred and seventy-seven",
    278: "two hundred and seventy-eight", 279: "two hundred and seventy-nine",
    280: "two hundred and eighty",
    281: "two hundred and eighty-one", 282: "two hundred and eighty-two",
    283: "two hundred and eighty-three", 284: "two hundred and eighty-four",
    285: "two hundred and eighty-five",
}

CHECKS = 0
FAILED = []


def spelling_re(word):
    """A spelling as a pattern, article and spacing allowed to vary.

    index.html carried "A hundred and eight signatures" for months after
    there were 283, with this script reading that page every release and
    passing. WORDS says "one hundred and eight", the page said "A", and a
    plain substring search went straight past it. One article was all it
    took, so a leading "one" matches "a" as well.

    Runs of whitespace match each other for the same kind of reason: a
    sentence in HTML breaks where the column runs out rather than where
    the words do, and this page split the old count across a line.
    """
    parts = word.split()
    if parts[0] == "one":
        parts[0] = "(?:one|a)"
    return r"\s+".join(parts)


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def count_rows():
    """Rows per table, comments and the table's own braces excluded."""
    src = SIGS.read_text(encoding="utf-8", errors="replace")
    out = {}
    for typ, name, body in re.findall(
            r"static const (\w+) (k\w+)\[\] = \{(.*?)\n\};", src, re.S):
        b = re.sub(r"/\*.*?\*/", "", body, flags=re.S)
        b = re.sub(r"//[^\n]*", "", b)
        out[name] = len(re.findall(r"^\s*\{", b, re.M))
    kinds = set(re.findall(r"Kind::(\w+)", src)) - {"Unknown"}
    return out, len(kinds)


def site_pages():
    """Every published page, if .publish.local says where the site is.

    Every page, not added.html alone. The first version checked that one
    because that is where the signature section lives, and firmware.html
    went on saying "108 signatures over nine kinds" with nothing looking at
    it. A number copied onto one page is usually copied onto two.
    """
    if not PUBLISH.is_file():
        return None
    m = re.search(r"PUEO_PUBLISH_DIR\s*=\s*[\"']?([^\"'\n]+)",
                  PUBLISH.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return None
    raw = m.group(1).strip()
    # .publish.local is read by a bash script and holds an MSYS path.
    if re.match(r"^/[a-zA-Z]/", raw):
        raw = raw[1] + ":" + raw[2:]
    d = Path(raw)
    return sorted(d.glob("*.html")) if d.is_dir() else None


def main():
    tables, nkinds = count_rows()
    total = sum(tables.values())

    print("the tables:")
    for name, n in sorted(tables.items(), key=lambda kv: -kv[1]):
        print("    %-16s %3d" % (name, n))
    print("    %-16s %3d rows over %d kinds" % ("TOTAL", total, nkinds))
    print()

    ok("the count has a spelling", total in WORDS,
       "%d is not in WORDS; add it" % total)

    guide = GUIDE.read_text(encoding="utf-8", errors="replace")
    ok("the user guide states the count",
       re.search(r"\b%d signatures\b" % total, guide) is not None,
       "it does not say '%d signatures'" % total)
    ok("  and the number of kinds",
       re.search(r"\b%d kinds\b" % nkinds, guide) is not None,
       "it does not say '%d kinds'" % nkinds)

    # Every match type the guide's table claims. A new table in the header
    # with no row here is a kind of question the docs do not mention, which
    # is the half of this that matters: a reader cannot tell whether a thing
    # was missed or was never looked for.
    print()
    print("every table is described in the guide:")
    DESCRIBED = {
        "kMacSigs": "whole MAC address",
        "kOuiSigs": "first three bytes of a MAC",
        # Both anchored-name tables are one row in the guide: kSsidSigs is
        # WiFi and kBleNameSigs is BLE, and from the reader's side they are
        # the same question asked of two radios.
        "kSsidSigs": "from the start",
        "kBleNameSigs": "from the start",
        "kNameInSigs": "anywhere in it",
        "kBleSigs": "BLE company or service ID",
        "kBle128Sigs": "128-bit service UUID",
        "kMfgSigs": "manufacturer data",
        "kSvcDataSigs": "service data",
    }
    # Only the rows of the guide's own table, not the whole guide. "the
    # manufacturer data" also appears in the BLE Scanner section, so a
    # search over the file passed with the table row deleted -- the check
    # finding the phrase somewhere else entirely and calling it covered.
    m = re.search(r"#### What Surveillance is looking at(.*?)^####", guide,
                  re.S | re.M)
    rows = [l for l in (m.group(1) if m else "").splitlines()
            if l.startswith("|")]
    rows = "\n".join(rows)
    ok("the guide's table was found", bool(rows),
       "no markdown table under the Surveillance heading")

    # The guide says how many questions the table asks, which is not the same
    # as how many tables there are: kSsidSigs and kBleNameSigs both match a
    # name from its first character, so they are two tables and one question.
    # The paragraph said "9 kinds" above an eight-row table and used the same
    # word for both, so a reader who counted got a different number from the
    # one they had just read. Now it says eight, and this is what stops that
    # number drifting the next time a table is added.
    body = [l for l in rows.splitlines()
            if l.startswith("|") and not re.match(r"^\|[\s|:-]+\|?$", l)]
    nq = max(0, len(body) - 1)  # less the header row
    qwords = {2: "Two", 3: "Three", 4: "Four", 5: "Five", 6: "Six",
              7: "Seven", 8: "Eight", 9: "Nine", 10: "Ten", 11: "Eleven"}
    ok("the guide's count of questions matches its table",
       re.search(r"\b%s questions over the %s tables\b"
                 % (qwords.get(nq, str(nq)),
                    {9: "nine", 10: "ten", 11: "eleven"}.get(nkinds, str(nkinds))),
                 guide, re.I) is not None,
       "the table has %d rows over %d tables; the sentence above it does not "
       "say so" % (nq, nkinds))

    for name in sorted(tables):
        phrase = DESCRIBED.get(name)
        ok("  %-14s" % name, phrase is not None and phrase in rows,
           "no row in the guide's table for it" if phrase is None
           else "no table row says %r" % phrase)

    # Every doc in the repository, not the guide alone. README.md said "108
    # signatures across nine kinds" for several releases while this check
    # passed, because the repo half read user-guide.md and nothing else. The
    # site half already knows better -- see site_pages(), and the comment
    # there saying a number copied onto one page is usually copied onto two.
    # It is just as true of a number copied into one file.
    #
    # Strict, with no allowance for a historical figure, because no doc here
    # states one: the changelog is the place for "it was 108 before", and the
    # changelog is not in this list. A doc that needs to say an old number
    # should fail this and be given a reason to be an exception, rather than
    # the rule being loosened in advance for a case that does not exist.
    print()
    print("no other doc states a count of its own:")
    docs = sorted((ROOT / "docs" / "pueo").glob("*.md"))
    docs += [ROOT / n for n in ("README.md", "PUEO.md", "CONTRIBUTING.md")]
    for d in docs:
        if not d.is_file() or d == GUIDE:
            continue
        flat = " ".join(d.read_text(encoding="utf-8", errors="replace").split())
        said = [m for m in re.finditer(r"\b(\d+) signatures\b", flat)]
        bad = [m.group(0) for m in said if int(m.group(1)) != total]
        if said or bad:
            ok("  %-22s" % d.name, not bad,
               "it says %s and there are %d" % (", ".join(bad), total))
        # "kinds" is an ordinary English word, so a count in front of it is
        # only this count when the sentence is about signatures. ble-sniffer.md
        # says Sniffer "raises two kinds of alert", which is correct and has
        # nothing to do with the tables. Requiring the word nearby also means
        # a sentence like the guide's "toggle any of the nine kinds" is not
        # read, which is the right trade: the guide's own count is checked
        # above by name, and a check that cries wolf is one that gets ignored.
        kinds = [m for m in re.finditer(
            r"\b(\d+|one|two|three|four|five|six|seven|eight|nine|ten)"
            r" kinds\b", flat)
            if "signature" in flat[max(0, m.start() - 200):m.end() + 60].lower()]
        spelling = {str(nkinds), {1: "one", 2: "two", 3: "three", 4: "four",
                                  5: "five", 6: "six", 7: "seven", 8: "eight",
                                  9: "nine", 10: "ten"}.get(nkinds, "")}
        wrongk = [m.group(0) for m in kinds if m.group(1).lower() not in spelling]
        if kinds:
            ok("  %-22s kinds" % d.name, not wrongk,
               "it says %s and there are %d" % (", ".join(wrongk), nkinds))

    print()
    pages = site_pages()
    if not pages:
        print("the site is not reachable; skipping it "
              "(no .publish.local, or no site there)")
    else:
        word = WORDS.get(total, "")
        html = "".join(p.read_text(encoding="utf-8", errors="replace")
                       for p in pages)
        print("checking %d published pages" % len(pages))
        ok("the site states the count",
           word and re.search(spelling_re(word), html, re.I) is not None,
           "no page says %r" % word)

        # And the digits, which is the spelling firmware.html uses. A number
        # written one way on one page and another way on another is still
        # one number, and both go stale together.
        for pg in pages:
            t = pg.read_text(encoding="utf-8", errors="replace")
            m2 = re.search(r"(\d+) signatures over", t)
            if m2:
                ok("  %s states %s" % (pg.name, total),
                   int(m2.group(1)) == total,
                   "it says %s signatures" % m2.group(1))
        # "two hundred" is a prefix of "two hundred and sixty-six", so a
        # plain substring search finds the shorter spelling inside the
        # correct one and reports the page as carrying two counts. The
        # lookahead is what makes a spelling match only when it is the whole
        # number rather than the start of a longer one.
        #
        # The hyphen belongs in it as much as the " and" does, and was
        # missing until 274 arrived: "two hundred and seventy" is a prefix
        # of "two hundred and seventy-four", and a word boundary sits
        # happily before a hyphen, so the check reported a page as carrying
        # two counts when it carried one.
        stale = [w for n, w in WORDS.items() if n != total
                 and re.search(spelling_re(w) + r"\b(?!\s+and\b|-)",
                               html, re.I)]
        ok("  and no longer says an older one", not stale,
           "it also says %s, so one of the two is wrong" % stale)

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
