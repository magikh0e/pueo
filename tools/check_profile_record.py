#!/usr/bin/env python3
"""The EEPROM profile record is described in one place.

subghz.cpp had three structs for the same twenty-eight bytes: SubGhzProfile
at file scope, and Profile declared again in namespace replayat and in
namespace SavedProfile. The export, sync and SD-import paths used the first,
saveProfile the second, the browser the third, and nothing kept them in
step.

They agreed, which is the only reason a record written by one was readable
by the others. Editing one would have reinterpreted every profile already in
EEPROM, and the symptom is the worst kind: a saved capture comes back wrong
rather than anything failing, so the first sign is a replay that does not
work and no reason given.

PROFILE_SIZE had the same shape, a constexpr at file scope and a macro in
each namespace shadowing it from there onward, each evaluating to the same
number only because each struct did.

This is the drift the anonymous namespace at the top of that file already
exists to stop. Its own comment says replayat and subjammer each carried a
copy of the frequency list and nothing kept them in step; the profile record
was that bug one layer down, and lasted longer because the two copies were
in different namespaces and never named the same thing.

    python tools/check_profile_record.py

Reads source; needs no board.

What it asserts
---------------
  one struct      Exactly one definition of the record. An alias is fine and
                  is how the other two namespaces reach it; a second struct
                  is not.

  one size        PROFILE_SIZE comes from that struct and is not redefined
                  by a macro, because a macro silently wins from the point
                  it appears.

  the layout      The field order and widths, pinned. The EEPROM holds
                  records written by earlier firmware, so changing this is
                  changing a file format, and that should be a decision
                  rather than an edit.

  the seam        saveProfile() was split so an import could reach the part
                  that stores a record without the part that reads globals
                  or draws. makeRoomForProfile() has to run before anything
                  prompts, because it is the step that can fail: the first
                  attempt at this split asked for a name and only then found
                  there was no room. storeProfile() must not prompt or draw,
                  or it is not reusable and the split bought nothing.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SUB = ROOT / "ESP32-DIV" / "subghz.cpp"

# What is already in EEPROM on every board that has saved a profile.
LAYOUT = [
    ("uint32_t", "frequency", ""),
    ("uint32_t", "value", ""),
    ("uint16_t", "bitLength", ""),
    ("uint16_t", "protocol", ""),
    ("char", "name", "16"),
]
RECORD_BYTES = 4 + 4 + 2 + 2 + 16

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


def main():
    src = SUB.read_text(encoding="utf-8", errors="replace")

    structs = re.findall(
        r"struct\s+__attribute__\(\(packed\)\)\s+(\w*Profile)\s*\{(.*?)\};",
        src, re.S)
    names = [n for n, _ in structs]
    ok("the record is one struct", len(structs) == 1,
       "%d definitions: %s" % (len(structs), ", ".join(names)))

    aliases = re.findall(r"using\s+Profile\s*=\s*(\w+)\s*;", src)
    ok("  and the other namespaces alias it",
       all(a == "SubGhzProfile" for a in aliases),
       "aliases point at %s" % (set(aliases) or "nothing"))

    macros = re.findall(r"#define\s+PROFILE_SIZE\b", src)
    ok("PROFILE_SIZE is not a macro", not macros,
       "%d macro definition(s): a macro wins from where it appears, so two "
       "paths can disagree about the record size" % len(macros))

    const = re.search(r"constexpr\s+uint16_t\s+PROFILE_SIZE\s*=\s*"
                      r"sizeof\((\w+)\)", src)
    ok("  and comes from the struct", const is not None
       and const.group(1) == (names[0] if names else None),
       "PROFILE_SIZE is not sizeof the one struct")

    if structs:
        body = structs[0][1]
        fields = [(t, n, (a or "")) for t, n, a in
                  re.findall(r"(uint\d+_t|char)\s+(\w+)(?:\[([^\]]+)\])?", body)]
        # The array length may be written as the constant; resolve it.
        m = re.search(r"MAX_NAME_LENGTH\s*=\s*(\d+)", src)
        namelen = m.group(1) if m else ""
        fields = [(t, n, (namelen if a == "MAX_NAME_LENGTH" else a))
                  for t, n, a in fields]
        ok("the layout is unchanged", fields == LAYOUT,
           "EEPROM holds records in the old shape: %r" % (fields,))
        ok("  which is %d bytes" % RECORD_BYTES,
           fields == LAYOUT, "the record size moved")

    # ── the seam saveProfile was split along ───────────────────────────
    def body(sig):
        i = src.index(sig)
        j = src.index("\n}\n", i)
        return src[i:j]

    save = body("void saveProfile() {")
    store = body("uint16_t storeProfile(")
    room = body("bool makeRoomForProfile(")

    ok("storeProfile does not prompt",
       "getUserInputName" not in store,
       "the write asks for a name, so an import cannot use it")
    ok("  and does not draw",
       "tft." not in store,
       "the write touches the screen, so an import cannot use it")
    ok("  and reads no capture globals",
       not any(g in store for g in ("receivedValue", "receivedBitLength",
                                    "receivedProtocol",
                                    "currentFrequencyIndex")),
       "the write still reads the globals it was split away from")

    ok("makeRoomForProfile owns the overflow",
       "exportProfilesToSD" in room and "clearProfilesInEeprom" in room,
       "the rotation is not in the step that can fail")
    ok("  and storeProfile does not repeat it",
       "exportProfilesToSD" not in store,
       "both halves export, so a full store would export twice")

    # The order is the behaviour: the step that can fail runs first, so a
    # name is never asked for and then thrown away.
    if "makeRoomForProfile" in save and "getUserInputName" in save:
        ok("room is made before the name is asked for",
           save.index("makeRoomForProfile") < save.index("getUserInputName"),
           "saveProfile prompts first, so a full device asks for a name and "
           "then refuses it")
    else:
        ok("room is made before the name is asked for", False,
           "saveProfile no longer calls both")

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed  (one %s, %d bytes)"
          % (CHECKS, names[0] if names else "?", RECORD_BYTES))
    return 0


if __name__ == "__main__":
    sys.exit(main())
