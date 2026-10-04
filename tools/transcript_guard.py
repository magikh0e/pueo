#!/usr/bin/env python3
"""A tripwire for checks that reimplement firmware logic in Python.

Six of the checks in this directory do not read the firmware at all. They
transcribe an algorithm into Python and test the transcription: the Find My
parser, the EAPOL payload walk, the Fast Pair request and response, Spotter's
address merging, the .sub parser and Hunt's follow heuristic.

That is worth having. It is a written-down spec, it runs in milliseconds, and
it catches a regression in the algorithm as described. It is not what the
other checks claim, and the difference is not small:

    dropping both locally-administered tests from Spotter's findOrAdd, which
    is the under-reporting bug check_spotter_merge.py's own docstring calls
    "the failure that matters here", left it printing "16 merge rules hold"
    and exiting 0.

The project has met this before. check_deploy_pueo_prune once "transcribed
the arithmetic instead and passed cleanly against a script whose sort -V had
been replaced with sort, which is to say it was asserting its own
transcription", and that was treated as a fault.

Executing the C is not on the table here. What is: notice when the C moves,
and say so, so the transcription gets re-read rather than silently drifting.
A pinned hash of the function body does that. It cannot tell you the
transcription is correct. It can stop it being wrong without anyone knowing.

    from transcript_guard import guard
    guard("Spotter.cpp", "findOrAdd", "a1b2c3d4", __doc__)

Normalised before hashing: comments stripped, whitespace collapsed. A
reformat or a comment edit does not trip it, because a tripwire that fires
on noise is one people learn to re-pin without looking, which is worse than
not having it.
"""
import hashlib
import io
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

BLOCK = re.compile(r"/\*.*?\*/", re.S)
LINE = re.compile(r"//[^\n]*")


def _normalise(text):
    text = BLOCK.sub(" ", text)
    text = LINE.sub(" ", text)
    return " ".join(text.split())


def extract(filename, func):
    """The body of `func` in `filename`, braces matched. None if absent."""
    path = SKETCH / filename
    if not path.is_file():
        return None
    src = io.open(path, encoding="utf-8", errors="replace").read()

    # a definition, not a call or a declaration: name, args, then an open
    # brace before the next semicolon
    for m in re.finditer(r"\b%s\s*\(" % re.escape(func), src):
        # Walk to the opening brace. A semicolon first means this was a
        # declaration or a call, not a definition.
        i = m.end() - 1
        while i < len(src) and src[i] not in "{;":
            i += 1
        if i >= len(src) or src[i] != "{":
            continue
        start = m.start()
        depth = 0
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    return src[start:i + 1]
            i += 1
    return None


def digest(filename, func):
    body = extract(filename, func)
    if body is None:
        return None
    return hashlib.sha256(_normalise(body).encode("utf-8")).hexdigest()[:16]


def guard(filename, func, pinned, what=""):
    """Fail the calling check when the C it transcribes has changed.

    Returns on agreement. Exits 1 otherwise, because a transcription whose
    subject has moved is not a check, it is a note about the past.
    """
    got = digest(filename, func)
    if got is None:
        print("FAIL: %s() is not in %s any more." % (func, filename))
        print("      This check transcribes it into Python and tests the")
        print("      transcription, so without it the check is about nothing.")
        sys.exit(1)
    if got != pinned:
        print("FAIL: %s() in %s has changed since this was written."
              % (func, filename))
        print("      pinned %s, now %s" % (pinned, got))
        print()
        print("      This check does not read the firmware: it reimplements")
        print("      that function and tests the reimplementation. The C has")
        print("      moved, so re-read it, bring the Python into line if it")
        print("      needs it, and then re-pin the hash. Re-pinning without")
        print("      reading is how a check comes to describe code that no")
        print("      longer exists.")
        sys.exit(1)


def main():
    """Print the current digest of each argument pair, for pinning."""
    args = sys.argv[1:]
    if len(args) < 2 or len(args) % 2:
        print(__doc__)
        print("usage: transcript_guard.py <file.cpp> <func> [<file> <func>...]")
        return 2
    for i in range(0, len(args), 2):
        f, fn = args[i], args[i + 1]
        d = digest(f, fn)
        print("  %-22s %-22s %s" % (f, fn, d or "NOT FOUND"))
    return 0


if __name__ == "__main__":
    sys.exit(main())
