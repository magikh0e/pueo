#!/usr/bin/env python3
"""The documentation reads like somebody wrote it.

Three house rules, none of them about taste for its own sake:

1. No dash standing in for a comma or a full stop. The corpus had 510 of
   them and they all went in one pass, which is the whole argument for a
   check: without one they come back a sentence at a time and nobody notices
   until there are five hundred again. It is also not only a style rule.
   TFT_eSPI's font 1 stops at 0x7E, so a dash that reaches the panel is three
   wrong glyphs, which is what check_ascii_strings.py is for.

2. Nothing about the conversation that produced the work. "Asked whether they
   could be checked properly, which was the right question to ask" is a line
   that was in the changelog. A reader wants to know what the code does, not
   who said what to whom while it was written, and a document that narrates
   its own authorship is telling them about the wrong thing.

3. None of the handful of phrases that read as filler. "It is worth noting"
   is always deletable, and whatever follows "let's dive into" would be
   better as the thing itself.

    python tools/check_prose.py

Reads source; needs no board. The site is checked when .publish.local says
where it is and skipped silently otherwise, which is the case inside
pueo-<version>-src.zip. That covers the seven pages and the site repo's own
README, which was missed for four releases because it lived here until the
site was split out and no glob followed it.

What is exempt, and why
-----------------------
The changelog. It ships inside every published pueo-<version>-src.zip, those
archives are verified byte for byte, and rewriting an old entry now would put
the repository at odds with every copy already downloaded.

Headings and titles. "Pueo - multi-radio field tool firmware" is a title and
its subtitle rather than a sentence, and rule 1 is about sentences.

Numeric ranges. "2402 - 2480 MHz" is a range however much space is around the
dash, and an aligned table puts plenty there.

Code: fenced blocks, inline spans, HTML tags, and anything in a <pre> that is
sample output rather than prose.
"""
import io
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
PUBLISH = ROOT / ".publish.local"

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


def targets():
    out = sorted((ROOT / "docs" / "pueo").glob("*.md"))
    out += [ROOT / n for n in ("README.md", "PUEO.md", "CONTRIBUTING.md")]
    d = site_dir()
    if d:
        out += sorted(d.glob("*.html"))
        # The site repo's own README, one level up from the published
        # directory. It was in this repo until the site was split out, so
        # none of the globs above reached it, and it kept ten dashes
        # through four releases while this check reported green.
        out.append(d.parent / "README.md")
    return [p for p in out if p.is_file()]


def label(p):
    """A name that says which file, when two share a basename.

    There are two README.md in the corpus now, this repo's and the site's,
    and a line number against the wrong one sends you to the wrong file.
    Anything outside this repo is shown with its parent directory.
    """
    try:
        return str(p.relative_to(ROOT))
    except ValueError:
        return "%s/%s" % (p.parent.name, p.name)


def blank(m):
    """Replace a match with spaces, keeping every newline where it was."""
    return re.sub(r"[^\n]", " ", m.group(0))


def prose_of(p):
    """The file with code, tags and headings blanked out.

    Offsets survive, so a line number still points at the right line.
    """
    t = io.open(p, encoding="utf-8", errors="replace").read()
    if p.suffix == ".html":
        t = re.sub(r"<style.*?</style>", blank, t, flags=re.S | re.I)
        t = re.sub(r"<script.*?</script>", blank, t, flags=re.S | re.I)
        t = re.sub(r"<!--.*?-->", blank, t, flags=re.S)
        t = re.sub(r"<(?:title|h1|h2|h3)\b.*?</(?:title|h1|h2|h3)>", blank,
                   t, flags=re.S | re.I)
        t = re.sub(r"<meta[^>]*>", blank, t)
        t = re.sub(r"<[^>]*>", blank, t)
    else:
        t = re.sub(r"```.*?```", blank, t, flags=re.S)
        t = re.sub(r"`[^`\n]*`", blank, t)
        # a heading is a title, and a title may carry a dash
        t = "\n".join(" " * len(l) if l.lstrip().startswith("#") else l
                      for l in t.split("\n"))
    return t


DASH = re.compile(r"—|–|&mdash;|&ndash;|(?<=\s)--(?=\s)")

NARRATION = [
    (r"\basked whether\b", "what was asked"),
    (r"\bwhich was the right question\b", "praise for a question"),
    (r"\bthe (?:user|you) (?:asked|said|reported|noticed|pointed out)\b",
     "who said it"),
    # Not "wanted". "the row you wanted to read" is ordinary second person and
    # the guide is written in it throughout. Narration is about the
    # conversation that produced the work, not about the reader.
    (r"\byou (?:asked|reported|pointed out|noticed)\b", "who said it"),
    (r"\bI (?:said|replied|suggested|thought|realised|realized)\b",
     "who said it"),
    (r"\breported from the outside\b", "how it was found"),
    (r"\bgood (?:question|catch|point)\b", "praise"),
    (r"\bas (?:you|we) (?:asked|discussed|noted)\b", "the conversation"),
    (r"\bin (?:our|this) conversation\b", "the conversation"),
    (r"\bper your request\b", "the conversation"),
    (r"\bwhen (?:you|somebody) asked\b", "the conversation"),
]

FILLER = [
    (r"\bit (?:is|'s) worth noting\b", "delete it and keep the noting"),
    (r"\bit (?:is|'s) important to note\b", "same"),
    (r"\blet(?:'s| us) (?:dive|delve|explore|take a look)\b", "say the thing"),
    (r"\bdelve into\b", "say the thing"),
    (r"\bin today's (?:fast-paced|modern)\b", "no"),
    (r"\bat the end of the day\b", "no"),
    (r"\bneedless to say\b", "then do not say it"),
    (r"\bit goes without saying\b", "then do not say it"),
    (r"\bI hope (?:this|that) helps\b", "no"),
    (r"\bas an AI\b", "no"),
    (r"\bseamlessly\b", "marketing"),
    (r"\bleverage(?:s|d)? the\b", "use"),
    (r"\bgame[- ]chang(?:er|ing)\b", "marketing"),
]


def lines_of(t, pos):
    return t[:pos].count("\n") + 1


def main():
    files = targets()
    if not files:
        print("nothing to read")
        return 1

    d = site_dir()
    print("%d documents%s" % (len(files),
                              "" if d else "  (the site is not on this machine)"))
    print()

    # ---------------------------------------------------------------- rule 1
    hits = []
    for p in files:
        t = prose_of(p)
        for m in DASH.finditer(t):
            b, a = t[:m.start()].rstrip(), t[m.end():].lstrip()
            if not (b and a):
                continue
            if b[-1].isdigit() and a[0].isdigit():
                continue          # a range
            if b[-1] == "|" and a[0] == "|":
                continue          # an empty table cell
            hits.append((label(p), lines_of(t, m.start()),
                         " ".join(t[max(0, m.start() - 46):m.end() + 46].split())))
    for f, ln, c in hits[:20]:
        print("    %-22s:%-5d %s" % (f, ln, c[:94]))
    if len(hits) > 20:
        print("    ... and %d more" % (len(hits) - 20))
    ok("no dash doing a comma's job", not hits,
       "%d of them; commas, full stops and the occasional bracket" % len(hits))

    # ---------------------------------------------------------------- rule 2
    print()
    nar = []
    for p in files:
        t = prose_of(p)
        for pat, what in NARRATION:
            for m in re.finditer(pat, t, re.I):
                nar.append((label(p), lines_of(t, m.start()), m.group(0), what))
    for f, ln, s, what in nar[:20]:
        print("    %-22s:%-5d %-30r %s" % (f, ln, s, what))
    ok("nothing about the conversation that wrote it", not nar,
       "%d phrases narrate the chat rather than the code" % len(nar))

    # ---------------------------------------------------------------- rule 3
    print()
    fil = []
    for p in files:
        t = prose_of(p)
        for pat, what in FILLER:
            for m in re.finditer(pat, t, re.I):
                fil.append((label(p), lines_of(t, m.start()), m.group(0), what))
    for f, ln, s, what in fil[:20]:
        print("    %-22s:%-5d %-30r %s" % (f, ln, s, what))
    ok("and no filler", not fil, "%d phrases" % len(fil))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
