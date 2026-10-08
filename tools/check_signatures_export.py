#!/usr/bin/env python3
"""The published signature list matches this header.

    python tools/check_signatures_export.py

Reads source; needs no board and no network.

magikh0e/surveillance-signatures publishes the contents of
ESP32-DIV/SpotterSignatures.h as CSV, JSON and a README. Its own
CONTRIBUTING.md tells contributors not to send pull requests against those
three files because "they will be overwritten by the next regeneration".

Nothing enforced that. There was no extraction script in either repository,
so the published snapshot was whatever had last been produced by hand, and
a row edited here reached the published list only if somebody remembered.
One had already drifted: 82:6B:F2 reads "LAA, not a vendor" in the header
and was published with an empty label.

So this runs tools/extract_signatures.py in --check mode and fails if the
three files are not what it would write. That turns the sentence in
CONTRIBUTING.md into something true.

Silent when the sibling repository is not there. This script ships inside
pueo-<version>-src.zip, where ../surveillance-signatures does not exist, and
the same is true of any clone that did not also clone the list. A check that
failed in that situation would be reporting on the checkout rather than on
the code, which is what check_sig_counts.py already does for the website.
"""
import subprocess
import sys
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPO = HERE.parent
# Beside the firmware checkout, which is where it is cloned in practice.
LIST = REPO.parent / "surveillance-signatures"


def main():
    if not (LIST / "signatures.csv").is_file():
        print("no surveillance-signatures tree beside this one (%s)" % LIST)
        print("nothing to check, which is not a failure: the published list "
              "is a separate repository.")
        return 0

    extractor = HERE / "extract_signatures.py"
    if not extractor.is_file():
        print("missing %s, so the published list cannot be checked against "
              "the header" % extractor, file=sys.stderr)
        return 1

    r = subprocess.run([sys.executable, str(extractor),
                        "--out", str(LIST), "--check"],
                       capture_output=True, text=True)
    sys.stdout.write(r.stdout)
    if r.stderr:
        sys.stderr.write(r.stderr)
    if r.returncode != 0:
        print("\nThe published list no longer matches this header. Run:")
        print("    python tools/extract_signatures.py --out %s" % LIST)
        return 1
    print("\nthe published list matches SpotterSignatures.h")
    return 0


if __name__ == "__main__":
    sys.exit(main())
