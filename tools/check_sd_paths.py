#!/usr/bin/env python3
"""Nothing Pueo writes to the card lands outside /pueo.

It used to be seven directories and two loose files in the root: /logs,
/captures, /config, /esb, /subghz, /captive_portal, /ducky, /ssids.txt and a
/wd_wigle_upload.csv. On a card that also holds somebody's photographs, or a
Flipper's dumps, those are nine things scattered among their files with
nothing saying which program they belong to.

Every path is built from PUEO_DIR now. The reason this is a check and not
just a constant is that a bare literal is the easy thing to write: SD.open
takes a string, "/newfeature.csv" compiles, works, and puts a file in
somebody's root, and the next person to add a logger will reach for exactly
that.

    python tools/check_sd_paths.py

Reads source; needs no board.

The three that are allowed in the root
--------------------------------------
ssids.txt, /ducky and firmware.bin are not Pueo's writes. They are files the
owner puts on the card for Pueo to read, so moving them silently would break
every card that already works: Beacon Spammer would quietly fall back to its
built-in list and the ducky menu would be empty. Each reads PUEO_DIR first
and the old root second.

settings.json is the same, with worse consequences. Reading only the new
location would reset every setting on the first boot after the move, with
nothing saying why, so it reads both and writes the new one.

So those four are exempt, and the exemption is the specific thing it is: a
*read* fallback. Each one is checked for having a PUEO_DIR spelling too, and
for being read rather than written, because an exemption that just means
"this literal is fine" would have let the original bug back in through any
of them.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

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


# The legacy spellings that may still appear, each with the macro that is
# the PUEO_DIR version of it. A root literal not in here is a new file in
# somebody's root.
LEGACY = {
    "/ssids.txt":              "kSsidFilePath",
    "/ducky":                  "DUCKY_DIR",
    "/firmware.bin":           "FIRMWARE_FILE",
    "/config/settings.json":   "SETTINGS_PATH",
}

# HTTP routes, not card paths. The captive portal and the firmware updater
# both register handlers that look exactly like filenames.
ROUTES = {
    "/", "/dl", "/login", "/login.html", "/generate_204",
    "/hotspot-detect.html", "/captive.apple.com", "/ncsi.txt",
    "/connecttest.txt", "/serverIndex", "/update", "/favicon.ico",
}

# A path-shaped literal: a leading slash and no spaces.
#
# The % matters. The first version of this pattern had no % in its character
# class, so "/spotter_%lu.jsonl" matched nothing at all and three loggers
# writing to the card root were invisible to a check whose entire job was
# finding exactly that: Surveillance's captures, and the wardriver's in both
# its foreground and background paths. All three had gone to the root since
# they were written. A filename built by snprintf is the ordinary way to
# write a log, which made it the worst possible thing to leave out.
LITERAL = re.compile(r'"(/[A-Za-z0-9_.%/-]*)"')

# An all-caps macro immediately before the quote means the literal is a
# suffix being concatenated onto a directory, not a path of its own:
#   LOG_DIR "/jamdet.jsonl"      ESB_DIR "/esb_"
# Six of those were the whole of this check's first run, and treating them
# as root paths would push every filename into a macro of its own for no
# reason.
CONCAT = re.compile(r'\b[A-Z][A-Z0-9_]*\s*$')

# Things that are plainly not card paths.
def interesting(lit):
    if lit in ROUTES:
        return False
    if lit.startswith("/%"):          # a format, not a path
        return False
    if lit.startswith("//"):          # a URL that lost its scheme in the regex
        return False
    if "*" in lit:
        return False
    return True


def main():
    print("the prefix exists and everything is built from it:")
    shared = (SKETCH / "shared.h").read_text(encoding="utf-8",
                                             errors="replace")
    m = re.search(r'#define PUEO_DIR\s+"(/\w+)"', shared)
    ok("PUEO_DIR is defined", m is not None)
    prefix = m.group(1) if m else "/pueo"
    if m:
        ok("  and it is a single folder off the root",
           re.fullmatch(r"/\w+", m.group(1)) is not None,
           "%r is not one folder" % m.group(1))

    for macro in ("LOG_DIR", "CAPTURE_DIR", "CONFIG_DIR", "CPORTAL_DIR",
                  "ESB_DIR", "SUBGHZ_SD_DIR"):
        ok("%-14s is built from PUEO_DIR" % macro,
           re.search(r"#define %s\s+PUEO_DIR\b" % macro, shared) is not None,
           "it is a literal again, so it is back in the root")
    ok("SETTINGS_PATH is built from CONFIG_DIR",
       re.search(r'#define SETTINGS_PATH\s+CONFIG_DIR\b', shared) is not None)

    print("\nno path-shaped literal outside PUEO_DIR, except the known reads:")
    stray = []
    legacy_seen = {}
    for path in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.h")):
        src = path.read_text(encoding="utf-8", errors="replace")
        for i, line in enumerate(src.splitlines(), 1):
            if line.lstrip().startswith(("*", "//")):
                continue              # a comment, including these docstrings
            for lm in LITERAL.finditer(line):
                lit = lm.group(1)
                if not interesting(lit):
                    continue
                # On a #define, the name before the quote is what is being
                # defined, not something being concatenated onto. Skipping
                # it hid both legacy macros and reported them as gone.
                before = line[:lm.start()]
                dm = re.match(r"\s*#\s*define\s+\w+\s*", before)
                if dm:
                    before = before[dm.end():]
                if CONCAT.search(before):
                    continue          # a suffix onto a directory macro
                # Already in the right place. That covers PUEO_DIR's own
                # definition and any path written out in full; the macro is
                # better style, but a literal under /pueo is not the bug
                # this is looking for.
                if lit == prefix or lit.startswith(prefix + "/"):
                    continue
                # Nothing but separators and dots is a relative-segment
                # spelling, not a destination: "/.", "/./", "..". These turn
                # up in sdTreeGuarded, which refuses them so the SD reset
                # cannot climb out of PUEO_DIR. A literal being matched
                # against is the opposite of a path being written to, and
                # reading one as a stray root path reported the guard as the
                # thing it exists to prevent.
                if lit and not lit.strip("/."):
                    continue
                if lit in LEGACY:
                    legacy_seen.setdefault(lit, []).append(
                        "%s:%d" % (path.name, i))
                    continue
                stray.append(("%s:%d" % (path.name, i), lit))

    ok("nothing new in the root", not stray,
       "; ".join("%s %s" % (w, l) for w, l in stray[:6]))

    print("\nand each legacy read has a PUEO_DIR spelling beside it:")
    allsrc = "".join(
        p.read_text(encoding="utf-8", errors="replace")
        for p in sorted(SKETCH.glob("*.cpp")) + sorted(SKETCH.glob("*.h")))
    for lit, macro in sorted(LEGACY.items()):
        where = legacy_seen.get(lit)
        if where is None:
            # Gone entirely is fine: it means the fallback was dropped on
            # purpose, not that the check should pass silently.
            print("  --    %-24s no longer appears" % lit)
            continue
        leaf = lit.rsplit("/", 1)[-1] or lit
        ok("%-24s has a PUEO_DIR form" % lit,
           re.search(r'PUEO_DIR\s+"[^"]*%s"' % re.escape(leaf), allsrc)
           is not None
           or re.search(r'CONFIG_DIR\s+"[^"]*%s"' % re.escape(leaf), allsrc)
           is not None,
           "only the root spelling exists, so this is not a fallback")

    print("\nthe legacy paths are read, never written:")
    # FILE_WRITE or FILE_APPEND on a line that also names the legacy
    # constant is a write to somebody's root.
    for path in sorted(SKETCH.glob("*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        for i, line in enumerate(src.splitlines(), 1):
            if not re.search(r"FILE_WRITE|FILE_APPEND|mkdir", line):
                continue
            for lit, macro in LEGACY.items():
                if '"%s"' % lit in line or ("LEGACY" in line
                                            and macro.split("_")[0] in line):
                    FAILED.append("%s:%d writes a legacy path" % (path.name, i))
                    print("  FAIL  %s:%d writes to %s" % (path.name, i, lit))
    ok("no write or mkdir touches a legacy path", True)

    print("\nnothing nested reaches a one-level mkdir:")
    # SD.mkdir does one level. mkdir("/pueo/config") fails when /pueo is not
    # there, and under PUEO_DIR every directory is nested by definition, so
    # moving the paths broke every one of them at once: no /pueo folder, and
    # settings that would not save.
    #
    # There were four copies of ensureDir -- SettingsStore, subghz, and two
    # in wifi.cpp -- and all four were one level short in the same way,
    # which is why the fix had to be made in four places and was made in
    # none. sdEnsureDir walks the path and they delegate to it.
    utils = (SKETCH / "utils.cpp").read_text(encoding="utf-8",
                                             errors="replace")
    ok("sdEnsureDir exists", "bool sdEnsureDir(const char* path)" in utils)
    ok("  and it walks the path rather than taking one level",
       "for (size_t i = 1; i <= end; i++)" in utils,
       "a non-recursive sdEnsureDir is the same bug with a better name")

    raw_mkdir = []
    for path in sorted(SKETCH.glob("*.cpp")):
        if path.name == "utils.cpp":
            continue              # sdEnsureDir itself, the one that may
        src = path.read_text(encoding="utf-8", errors="replace")
        for i, line in enumerate(src.splitlines(), 1):
            if line.lstrip().startswith(("*", "//")):
                continue
            if "SD.mkdir(" in line:
                raw_mkdir.append("%s:%d" % (path.name, i))
    ok("no feature calls SD.mkdir directly", not raw_mkdir,
       "; ".join(raw_mkdir))

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
