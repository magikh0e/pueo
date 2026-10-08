"""There is one FFT scratch pair, and the features share it.

The packet monitor and the sub-GHz spectrum both transform
ESP32DIV_FFT_SAMPLES doubles per frame. They used to each keep their own
pair at file scope, which is 4 KB of static DRAM holding a second copy of
the same numbers, and static DRAM is the first thing this part runs out of:
two feature screens in a row have had to be cut down to find a few dozen
bytes of it.

Sharing is safe for one reason, which is worth stating because it is the
reason and not a property of the buffers: the dispatcher runs a single
feature's loop until it exits before returning to the menu, so the two
cannot be live at once. Each frame also fills every entry before
transforming and keeps nothing afterwards, so there is no state to be
clobbered even in principle.

What this check watches for is a second pair reappearing. That is an easy
thing to add by accident, because a new spectrum feature wants a buffer and
declaring one locally is the obvious move, and nothing about it fails: the
build simply gets 4 KB closer to the wall, and the next small feature pays
for it.

    python tools/check_fft_scratch.py
"""
import io
import re
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

DEFN = re.compile(r"^\s*(?:static\s+)?double\s+(\w+)\s*\[", re.M)
# A double array at file scope sized by the FFT sample count, under any name.
SIZED = re.compile(
    r"^\s*(?:static\s+)?double\s+(\w+)\s*\[\s*"
    r"(?:ESP32DIV_FFT_SAMPLES|samples|samplesSUB)\s*\]", re.M)

SHARED = ("pueoFftReal", "pueoFftImag")

defs = {}
users = {}
for path in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.h")):
    src = io.open(path, encoding="utf-8", errors="replace").read()
    for name in SIZED.findall(src):
        defs.setdefault(name, []).append(path.name)
    for name in SHARED:
        if re.search(r"\b%s\b" % name, src):
            users.setdefault(name, []).append(path.name)

# Exactly one definition of each half, and nothing else of that shape.
for name in SHARED:
    where = defs.get(name, [])
    if len(where) != 1:
        raise SystemExit("  !!  %s is defined %d times: %s"
                         % (name, len(where), ", ".join(where) or "nowhere"))

extra = sorted(n for n in defs if n not in SHARED)
if extra:
    raise SystemExit(
        "  !!  a second FFT scratch buffer is back: %s\n"
        "      Use pueoFftReal/pueoFftImag. Only one feature runs at a\n"
        "      time, and a private pair costs 4 KB of static DRAM that\n"
        "      the next small screen will need." % ", ".join(extra))

# The declaration is in shared.h, so both features see the same extent
# rather than agreeing by coincidence.
hdr = io.open(SKETCH / "shared.h", encoding="utf-8", errors="replace").read()
for name in SHARED:
    if not re.search(r"extern\s+double\s+%s\s*\[\s*ESP32DIV_FFT_SAMPLES\s*\]"
                     % name, hdr):
        raise SystemExit("  !!  shared.h does not declare %s at "
                         "ESP32DIV_FFT_SAMPLES" % name)

# Both features reach the pair. If one stops, the sharing has been undone
# somewhere and the saving is about to be spent without anyone noticing.
for feature in ("wifi.cpp", "subghz.cpp"):
    for name in SHARED:
        if feature not in users.get(name, []):
            raise SystemExit("  !!  %s no longer uses %s" % (feature, name))

# And the old private names stay gone, so two names for one buffer cannot
# come back as a way of keeping the diff small.
for dead in ("vRealSUB", "vImagSUB"):
    for path in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.h")):
        src = io.open(path, encoding="utf-8", errors="replace").read()
        if re.search(r"\b%s\b" % dead, src):
            raise SystemExit("  !!  %s is back in %s" % (dead, path.name))

owner = defs[SHARED[0]][0]
print("one %d-sample FFT scratch pair in %s, shared by wifi.cpp and subghz.cpp"
      % (int(re.search(r"ESP32DIV_FFT_SAMPLES\s+(\d+)", hdr).group(1)), owner))
