"""Checks the Flipper `.sub` parser in ESP32-DIV/SubFile.cpp.

A `.sub` comes off somebody else's SD card, so it is input in the same sense
a probe request is, and it gets the same treatment: every field bounded, no
allocation, and a model here that is fuzzed rather than eyeballed.

The case that matters most is the one that is easy to get wrong quietly. A
Flipper RAW file has the same Filetype prefix and the same Frequency and
Preset lines as a key file, and then carries microsecond timings where the
key would be. Parsing one into a profile that holds a value and a bit count
produces something that looks valid and transmits nonsense, so RAW is
refused by name.

    python tools/check_sub_parse.py
"""
# This check does not read the firmware: it reimplements the code below and
# tests the reimplementation, so a change to the C cannot fail it. Proved on
# check_spotter_merge.py, where removing the under-reporting guard its own
# docstring exists to protect left it passing.
#
# The function bodies are pinned by hash and the transcribed constants are
# compared by value, which is the better report of the two: it can name the
# constant that moved. Neither can tell you the transcription is correct.
# Re-pin only after reading the C and bringing the Python into line.
from transcript_guard import guard, guard_consts

guard("SubFile.cpp", "parse", "d728c63cd9188ee9")
guard_consts("SubFile.cpp", {
    "kFreqMinHz": 280000000,
    "kFreqMaxHz": 960000000,
})

OK = "Ok"
NOT_SUB = "NotSubFile"
RAW = "RawUnsupported"
MISSING = "MissingField"
BAD = "BadField"
BITS = "TooManyBits"
FREQ = "FreqOutOfRange"

FREQ_MIN = 280000000
FREQ_MAX = 960000000
PROTOCOLS = {"Princeton": 1}


def parse(text):
    """SubFile::parse(), transcribed."""
    out = dict(frequency=0, value=0, bitLength=0, protocol=0, te=0,
               protocolName="", preset="")
    filetype = None
    freq = bits = None
    key = None
    saw_raw_data = False

    for line in text.split("\n"):
        if ":" not in line:
            continue
        name, _, val = line.partition(":")
        name = name.strip(" \t\r")
        val_stripped = val.strip(" \t\r")
        if name == "Filetype":
            filetype = val_stripped[:47]
        elif name == "Frequency":
            freq = _u32(val_stripped)
            if freq is None:
                return BAD, out
        elif name == "Bit":
            bits = _u32(val_stripped)
            if bits is None:
                return BAD, out
        elif name == "Key":
            key = _key(val_stripped)
            if key is None:
                return BAD, out
        elif name == "TE":
            te = _u32(val_stripped)
            if te is not None and te <= 0xFFFF:
                out["te"] = te
        elif name == "Protocol":
            out["protocolName"] = val_stripped[:23]
        elif name == "Preset":
            out["preset"] = val_stripped[:39]
        elif name == "RAW_Data":
            saw_raw_data = True

    if filetype is None or "flipper subghz" not in filetype.lower():
        return NOT_SUB, out
    if saw_raw_data or "raw" in filetype.lower() \
            or "raw" in out["protocolName"].lower():
        return RAW, out
    if freq is None or bits is None or key is None:
        return MISSING, out
    if freq < FREQ_MIN or freq > FREQ_MAX:
        return FREQ, out
    if bits == 0 or bits > 32:
        return BITS, out

    out["frequency"] = freq
    out["bitLength"] = bits
    out["value"] = key & 0xFFFFFFFF
    out["protocol"] = PROTOCOLS.get(out["protocolName"], 0)
    return OK, out


def _u32(s):
    s = s.strip(" \t\r")
    if not s or not all("0" <= c <= "9" for c in s):
        return None
    v = int(s)
    return v if v <= 0xFFFFFFFF else None


def _key(s):
    parts = s.split()
    if not parts or len(parts) > 8:
        return None
    acc = 0
    for p in parts:
        if len(p) != 2 or any(c not in "0123456789abcdefABCDEF" for c in p):
            return None
        acc = (acc << 8) | int(p, 16)
    return acc


checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


KEYFILE = """Filetype: Flipper SubGhz Key File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: Princeton
Bit: 24
Key: 00 00 00 00 00 12 34 56
TE: 403
"""

RAWFILE = """Filetype: Flipper SubGhz RAW File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: RAW
RAW_Data: 331 -179 337 -181 332 -180
"""

# --- the ordinary case -----------------------------------------------------
r, p = parse(KEYFILE)
case("a key file parses", r == OK)
case("frequency", p["frequency"] == 433920000)
case("bit count", p["bitLength"] == 24)
case("value is the low bits of Key", p["value"] == 0x123456)
case("Princeton maps to rc-switch 1", p["protocol"] == 1)
case("protocol name kept", p["protocolName"] == "Princeton")
case("TE kept", p["te"] == 403)
case("preset kept", p["preset"] == "FuriHalSubGhzPresetOok650Async")

case("CRLF line endings", parse(KEYFILE.replace("\n", "\r\n"))[0] == OK)
case("no trailing newline", parse(KEYFILE.rstrip("\n"))[0] == OK)
case("blank lines are ignored", parse(KEYFILE.replace("Version: 1",
                                                      "\n\nVersion: 1\n"))[0] == OK)
case("unknown lines are ignored",
     parse(KEYFILE + "Some_New_Field: whatever\n")[0] == OK)

# --- RAW, three ways of spotting it ----------------------------------------
case("RAW file refused", parse(RAWFILE)[0] == RAW)
case("RAW refused by Filetype alone",
     parse(RAWFILE.replace("Protocol: RAW", "Protocol: Princeton")
           .replace("RAW_Data:", "Nope:"))[0] == RAW)
case("RAW refused by Protocol alone",
     parse(RAWFILE.replace("RAW File", "Key File")
           .replace("RAW_Data:", "Nope:"))[0] == RAW)
case("RAW refused by RAW_Data alone",
     parse(RAWFILE.replace("RAW File", "Key File")
           .replace("Protocol: RAW", "Protocol: Princeton"))[0] == RAW)

# --- not one of ours -------------------------------------------------------
case("no Filetype", parse("Frequency: 433920000\nBit: 24\nKey: 01\n")[0] == NOT_SUB)
case("another Filetype",
     parse("Filetype: Flipper NFC device\nVersion: 3\n")[0] == NOT_SUB)
case("empty input", parse("")[0] == NOT_SUB)
case("binary rubbish", parse("\x00\x01\x02\xff")[0] == NOT_SUB)

# --- missing and malformed -------------------------------------------------
for drop in ("Frequency: 433920000\n", "Bit: 24\n",
             "Key: 00 00 00 00 00 12 34 56\n"):
    case("missing %s" % drop.split(":")[0],
         parse(KEYFILE.replace(drop, ""))[0] == MISSING)

case("non-numeric frequency",
     parse(KEYFILE.replace("433920000", "four thirty three"))[0] == BAD)
case("negative frequency is not a number",
     parse(KEYFILE.replace("433920000", "-433920000"))[0] == BAD)
case("frequency that overflows 32 bits",
     parse(KEYFILE.replace("433920000", "99999999999"))[0] == BAD)
case("odd hex digit in Key",
     parse(KEYFILE.replace("00 00 00 00 00 12 34 56", "00 0 12"))[0] == BAD)
case("non-hex in Key",
     parse(KEYFILE.replace("00 00 00 00 00 12 34 56", "00 ZZ"))[0] == BAD)
case("key longer than 64 bits",
     parse(KEYFILE.replace("00 00 00 00 00 12 34 56",
                           "00 " * 9))[0] == BAD)

# --- ranges ----------------------------------------------------------------
case("868 MHz is in range", parse(KEYFILE.replace("433920000", "868350000"))[0] == OK)
case("2.4 GHz is not", parse(KEYFILE.replace("433920000", "2400000000"))[0] == FREQ)
case("100 kHz is not", parse(KEYFILE.replace("433920000", "100000"))[0] == FREQ)
case("Bit 0 refused", parse(KEYFILE.replace("Bit: 24", "Bit: 0"))[0] == BITS)
case("Bit 33 refused", parse(KEYFILE.replace("Bit: 24", "Bit: 33"))[0] == BITS)
case("Bit 32 allowed", parse(KEYFILE.replace("Bit: 24", "Bit: 32"))[0] == OK)

# --- protocols we do not claim to know -------------------------------------
for name in ("CAME", "NICE FLO", "Holtek", "Linear", "KeeLoq", "Somfy Telis"):
    r, p = parse(KEYFILE.replace("Protocol: Princeton", "Protocol: " + name))
    case("%s parses but maps to no rc-switch number" % name,
         r == OK and p["protocol"] == 0 and p["protocolName"] == name)

# --- fuzz ------------------------------------------------------------------
import random
random.seed(20260919)
ALPHABET = "abcdefABCDEF0123456789 :\n\r\t-Filetyp,.FlipperSubGhzKeyRAWData"
VALID = {OK, NOT_SUB, RAW, MISSING, BAD, BITS, FREQ}
for _ in range(60000):
    n = random.randint(0, 200)
    text = "".join(random.choice(ALPHABET) for _ in range(n))
    r, p = parse(text)
    assert r in VALID, r
    if r == OK:
        assert 0 < p["bitLength"] <= 32
        assert FREQ_MIN <= p["frequency"] <= FREQ_MAX
        assert p["value"] <= 0xFFFFFFFF
    checks += 1

# mutate a real file one byte at a time and make sure nothing explodes
base = list(KEYFILE)
for i in range(len(base)):
    for repl in ("\x00", "\n", ":", "Z", "9"):
        m = base[:]
        m[i] = repl
        r, _ = parse("".join(m))
        assert r in VALID, (i, repl, r)
        checks += 1

print("ok -- %d checks" % checks)
print("RAW is refused three ways; only Princeton claims an rc-switch number")
