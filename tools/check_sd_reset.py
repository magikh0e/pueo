"""Checks the scope of the SD reset in ESP32-DIV/utils.cpp.

Reset SD removes files. There is no undo, the card is the owner's rather than
the project's, and a card that holds Pueo's captures can equally hold
somebody's photographs, a recovery image or the only copy of something. So
the question this check exists to answer is not whether the wipe works. It is
whether the wipe can ever reach outside PUEO_DIR.

sdTreeGuarded is the one place that decides, and both the measure and the
delete call it, so there is a single function to be sure of. That is the
reason it exists as a function at all: a scope test written at each call site
is a scope test that the next call site forgets.

Two further properties are asserted here because each silently turns a
refusal into a deletion:

    "/pueo-backup" must be refused. It starts with the five characters of
    "/pueo" and is a different directory. A prefix test written as
    startsWith(PUEO_DIR) alone accepts it, which is the specific bug this
    shape of guard invites, so the test is against PUEO_DIR "/" and the
    unslashed path is accepted only on exact equality.

    "/pueo/.." must be refused. It begins with "/pueo/" and names the card
    root, so a prefix test on its own accepts it. Nothing in the firmware
    builds such a path: the walkers descend using names from openNextFile,
    which does not return the "." and ".." entries a FAT directory holds on
    disk. That is the SD layer's behaviour rather than this code's, so the
    guard refuses the segments itself and these assertions hold it to that.

    python tools/check_sd_reset.py
"""
# Transcribed, not executed. See transcript_guard: the C is pinned by hash so
# a change to it fails this check and the transcription gets re-read, which is
# the most a Python model of C can honestly offer.
from transcript_guard import guard

guard("utils.cpp", "sdTreeGuarded", "561f26ec0d4f9d4f", __doc__)
guard("utils.cpp", "sdTreeRemove", "20f90b854ca89d86", __doc__)

PUEO_DIR = "/pueo"
MAX_DEPTH = 6


def guarded(path):
    """sdTreeGuarded(), transcribed."""
    p = path
    if len(p) > 1 and p.endswith("/"):
        p = p[:-1]
    if ".." in p:
        return False
    if p.endswith("/.") or p == ".":
        return False
    if "/./" in p:
        return False
    if p == PUEO_DIR:
        return True
    return p.startswith(PUEO_DIR + "/")


ACCEPT = [
    "/pueo",
    "/pueo/",
    "/pueo/captures",
    "/pueo/captures/",
    "/pueo/logs/2026-10-07.txt",
    "/pueo/subghz/gate.sub",
    "/pueo/config/settings.json",
    # Six levels down, which the depth limit stops rather than the guard.
    "/pueo/a/b/c/d/e/f",
]

REFUSE = [
    # The card itself, in every spelling that has ever been handed to a
    # delete by mistake.
    "/",
    "",
    "//",
    ".",
    "/.",
    # Siblings that share the prefix. The first is the one a bare
    # startsWith(PUEO_DIR) would delete.
    "/pueo-backup",
    "/pueo-backup/captures",
    "/pueo2",
    "/pueoX/thing",
    "/pueos",
    # Climbing out while keeping the prefix.
    "/pueo/..",
    "/pueo/../",
    "/pueo/../DCIM",
    "/pueo/./captures",
    "/pueo/captures/../../DCIM",
    "/pueo/../../",
    # Everything else on a real card.
    "/DCIM",
    "/DCIM/100CANON/IMG_0001.JPG",
    "/System Volume Information",
    "/firmware.bin",
    "/ssids.txt",
    # Relative, and the case-different spellings FAT would resolve but this
    # guard does not: refusing them is correct, since nothing builds them.
    "pueo",
    "pueo/captures",
    "/PUEO",
    "/Pueo/captures",
    # A name that merely contains it.
    "/backup/pueo",
    "/backup/pueo/captures",
]

for p in ACCEPT:
    if not guarded(p):
        raise SystemExit("  !!  refused a path it must accept: %r" % p)

for p in REFUSE:
    if guarded(p):
        raise SystemExit("  !!  ACCEPTED A PATH OUTSIDE %s: %r" % (PUEO_DIR, p))

# The prefix test must be against the separator, not the bare name. Stated as
# its own assertion because it is the one that deletes the wrong directory.
if guarded(PUEO_DIR + "-backup"):
    raise SystemExit("  !!  prefix test is missing its separator")

# Exact equality has to be a separate branch: PUEO_DIR itself does not match
# PUEO_DIR "/", and it is the path the wipe is actually called with.
if not guarded(PUEO_DIR):
    raise SystemExit("  !!  the wipe's own root is refused")


# ── what the caller passes ──────────────────────────────────────────────────
# The guard being right is worth nothing if the wipe is called with something
# else, so the call is read out of the firmware rather than assumed.
import io
import re
from pathlib import Path

SRC = io.open(Path(__file__).resolve().parent.parent / "ESP32-DIV" / "utils.cpp",
              encoding="utf-8", errors="replace").read()

calls = [c.strip() for c in re.findall(r"sdTreeRemove\s*\(\s*([^,]+),", SRC)]
# The definition and the declaration both match the pattern; a call does not
# name a type. What is left is the recursive descent, which passes the child
# path openNextFile gave it, and the top-level calls, which are the ones whose
# argument this check is about.
calls = [c for c in calls if "String&" not in c]
roots = [c for c in calls if "child" not in c]
if not roots:
    raise SystemExit("  !!  found no top-level sdTreeRemove call to check")
for c in roots:
    if "PUEO_DIR" not in c:
        raise SystemExit("  !!  sdTreeRemove called with %r, not PUEO_DIR" % c)

# And the recursion is bounded, so a directory cycle on a damaged FAT volume
# is a refusal rather than a stack overflow.
if not re.search(r"kSdTreeMaxDepth\s*=\s*%d\b" % MAX_DEPTH, SRC):
    raise SystemExit("  !!  kSdTreeMaxDepth is not %d" % MAX_DEPTH)
for fn in ("sdTreeStat", "sdTreeRemove"):
    body = SRC[SRC.index("%s(const String& path" % fn):]
    body = body[:body.index("\n}\n") + 3]
    if "kSdTreeMaxDepth" not in body:
        raise SystemExit("  !!  %s does not check the depth limit" % fn)
    if "sdTreeGuarded" not in body:
        raise SystemExit("  !!  %s does not call the guard" % fn)

print("%d paths accepted, %d refused, both walkers guarded and depth-bounded"
      % (len(ACCEPT), len(REFUSE)))
