"""Checks the Find My advertisement parser in AirTagSniffer.

Apple's offline-finding advertisement uses one type byte, 0x12, for two
different things, and the difference is what this feature is for:

  long form   status, then a 22-byte rotating public key and its two spare
              bits, then a hint. Length 0x19. Broadcast by a device that is
              SEPARATED from its owner -- a tag in someone else's bag.

  short form  status only. Length 0x02. Broadcast by every iPhone, iPad and
              Mac taking part in Find My, to say it is online.

Treating both as "an AirTag" produces a list that is mostly other people's
phones, which is what this parser used to do.

The status bit positions are from public research rather than from anything
Apple documents, and nothing here has met a real tag. This file pins down
what the code does; it cannot tell you the bit numbers are right.

    python tools/check_airtag_parse.py
"""
# This check does not read the firmware: it reimplements the function(s)
# below and tests the reimplementation, so a change to the C cannot fail it.
# Proved on check_spotter_merge.py, where removing the under-reporting guard
# its own docstring exists to protect left it passing.
#
# So the C is pinned. This cannot tell you the transcription is right; it
# stops it being wrong without anyone knowing. Re-pin only after reading the
# function and bringing the Python into line.
from transcript_guard import guard

guard("bluetooth.cpp", "parseAppleAdv", "8fd758d225166721")

HIT_FIND_MY = 0
HIT_NEW_AT = 1
HIT_NEARBY = 2

checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


def parse(data):
    """parseAppleAdv(), transcribed."""
    n = len(data)
    if data is None or n < 4:
        return None
    if data[0] != 0x4C or data[1] != 0x00:
        return None
    apple_type = data[2]
    if apple_type == 0x12:
        if n < 5:
            return None
        status = data[4]
        kind = HIT_FIND_MY if data[3] >= 0x19 else HIT_NEARBY
        return (kind, status)
    if apple_type == 0x07 and n >= 5 and data[4] == 0x05:
        return (HIT_NEW_AT, data[4])
    return None


def type_text(kind, status):
    """typeText(), transcribed."""
    if kind == HIT_NEARBY:
        return "Near"
    if kind == HIT_NEW_AT:
        return "NewAT"
    return "Sep %c%c" % ("FMLV"[(status >> 6) & 0x03],
                         "*" if (status & 0x04) else ".")


def separated(status=0x00, keylen=22):
    return bytes([0x4C, 0x00, 0x12, 0x19, status]) + b"\xAB" * keylen + b"\x00\x00"


def nearby(status=0x00):
    return bytes([0x4C, 0x00, 0x12, 0x02, status, 0x00])


# --- the split that is the point of all this -------------------------------
case("a separated advertisement is FindMy", parse(separated())[0] == HIT_FIND_MY)
case("an online Apple device is Nearby", parse(nearby())[0] == HIT_NEARBY)
case("they differ only in the length byte",
     separated()[2] == nearby()[2] and separated()[3] != nearby()[3])

# --- company ID ------------------------------------------------------------
case("a non-Apple company ID is ignored",
     parse(bytes([0x4C, 0x01, 0x12, 0x19, 0x00]) + b"\xAB" * 24) is None)
case("byte order matters: 0x004C little-endian",
     parse(bytes([0x00, 0x4C, 0x12, 0x19, 0x00]) + b"\xAB" * 24) is None)

# --- lengths ---------------------------------------------------------------
case("three bytes is too short to parse", parse(b"\x4C\x00\x12") is None)
case("four bytes has no status byte", parse(b"\x4C\x00\x12\x19") is None)
case("five bytes is enough", parse(b"\x4C\x00\x12\x19\x40") is not None)
for declared in range(0x00, 0x40):
    got = parse(bytes([0x4C, 0x00, 0x12, declared, 0x00]) + b"\xAB" * 30)
    want = HIT_FIND_MY if declared >= 0x19 else HIT_NEARBY
    case("declared length %#04x -> %s" % (declared, want), got[0] == want)

# --- the other type --------------------------------------------------------
case("Continuity 0x07 with prefix 0x05 is a new AirTag",
     parse(b"\x4C\x00\x07\x19\x05\x00")[0] == HIT_NEW_AT)
case("Continuity 0x07 with another prefix is not",
     parse(b"\x4C\x00\x07\x19\x01\x00") is None)
case("an unrelated Apple type is ignored",
     parse(b"\x4C\x00\x10\x05\x00\x00") is None)

# --- what the row says -----------------------------------------------------
case("full battery, maintained", type_text(HIT_FIND_MY, 0x04) == "Sep F*")
case("full battery, not maintained", type_text(HIT_FIND_MY, 0x00) == "Sep F.")
case("very low battery, not maintained",
     type_text(HIT_FIND_MY, 0xC0) == "Sep V.")
case("medium battery, maintained", type_text(HIT_FIND_MY, 0x44) == "Sep M*")
case("low battery", type_text(HIT_FIND_MY, 0x80) == "Sep L.")
case("nearby says nothing about a battery", type_text(HIT_NEARBY, 0xFF) == "Near")
case("a label always fits the field", all(
    len(type_text(k, st)) <= 6
    for k in (HIT_FIND_MY, HIT_NEW_AT, HIT_NEARBY)
    for st in range(256)))

# --- fuzz ------------------------------------------------------------------
import random
random.seed(20260919)
for _ in range(60000):
    n = random.randint(0, 40)
    data = bytes(random.getrandbits(8) for _ in range(n))
    got = parse(data)
    assert got is None or got[0] in (HIT_FIND_MY, HIT_NEW_AT, HIT_NEARBY), got
    if got is not None:
        t = type_text(got[0], got[1])
        assert 0 < len(t) <= 6, t
    checks += 1

print("ok -- %d checks" % checks)
print("0x12 splits on the length byte: >=0x19 separated, else an online device")
