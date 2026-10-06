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

guard("SubFile.cpp", "parse", "f4470b226b8b9407")
guard("SubFile.cpp", "write", "08c12894878ed04a")
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
ROLLING = "RollingCode"

# Counter-based protocols, named rather than detected. Every one is longer
# than 32 bits, so each already failed on bit count; this changes the
# sentence, not the outcome. Mirrors kRollingCodes in SubFile.cpp.
ROLLING_CODES = (
    "KeeLoq", "Somfy Telis", "Somfy Keytis", "Star Line", "Security+ 2.0",
    "Security+ 1.0", "Nice Flor S", "CAME Atomo", "AN-Motors",
    "Alutech AT-4N", "Hormann BiSecur", "Faac SLH", "Centurion Nova",
)

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
    # Before the bit test, same as the C++.
    if out["protocolName"] in ROLLING_CODES:
        return ROLLING, out
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
#
# Fixed-code, so the capture is good: the frequency, key and bit count are
# real and the record is worth storing. What is missing is a timing mapping,
# which is a decoder somebody could add. Protocol 0 says so, and subghz.cpp
# refuses to transmit it rather than letting rc-switch substitute Princeton.
for name in ("CAME", "NICE FLO", "Holtek", "Linear", "SMC5326", "PT2260"):
    r, p = parse(KEYFILE.replace("Protocol: Princeton", "Protocol: " + name))
    case("%s parses but maps to no rc-switch number" % name,
         r == OK and p["protocol"] == 0 and p["protocolName"] == name)

# Rolling codes are a different answer, not a weaker one. No decoder fixes
# a counter: the receiver has moved past the captured value already.
for name in ("KeeLoq", "Somfy Telis"):
    r, _ = parse(KEYFILE.replace("Protocol: Princeton", "Protocol: " + name))
    case("%s is refused as a rolling code, not as unmapped" % name,
         r == ROLLING)

# --- fuzz ------------------------------------------------------------------
import random
random.seed(20260919)
ALPHABET = "abcdefABCDEF0123456789 :\n\r\t-Filetyp,.FlipperSubGhzKeyRAWData"
VALID = {OK, NOT_SUB, RAW, MISSING, BAD, BITS, FREQ, ROLLING}
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

# ── write() is the inverse, so prove it round-trips ───────────────────────
#
# A writer is easy to get subtly wrong in a way no reader of the code
# notices: a byte order, a field name, a bit count that disagrees with the
# key. The test for one is not reading it, it is feeding its output back
# through the parser and insisting on the same values.

PROTO_NAMES = {v: k for k, v in PROTOCOLS.items()}


def write(frequency, value, bit_length, protocol, te=0):
    """Mirrors SubFile::write. Returns None where it returns 0."""
    name = PROTO_NAMES.get(protocol)
    if name is None or protocol == 0:
        return None
    if bit_length == 0 or bit_length > 32:
        return None
    if frequency < FREQ_MIN or frequency > FREQ_MAX:
        return None
    key = [0, 0, 0, 0,
           (value >> 24) & 0xFF, (value >> 16) & 0xFF,
           (value >> 8) & 0xFF, value & 0xFF]
    out = (
        "Filetype: Flipper SubGhz Key File\n"
        "Version: 1\n"
        "Frequency: %d\n"
        "Preset: FuriHalSubGhzPresetOok650Async\n"
        "Protocol: %s\n"
        "Bit: %d\n"
        "Key: %s\n" % (frequency, name, bit_length,
                        " ".join("%02X" % b for b in key)))
    if te:
        out += "TE: %d\n" % te
    return out


case("write refuses an unmapped protocol", write(433920000, 1, 24, 0) is None)
case("  and a bit count the value cannot hold",
     write(433920000, 1, 33, 1) is None)
case("  and a frequency the radio cannot reach",
     write(100000000, 1, 24, 1) is None)

# The sample the import was verified against on hardware.
_s = write(433920000, 0x123456, 24, 1)
case("write produces a file the parser accepts", parse(_s)[0] == OK)
_r, _p = parse(_s)
case("  frequency survives", _p["frequency"] == 433920000)
case("  value survives", _p["value"] == 0x123456)
case("  bit count survives", _p["bitLength"] == 24)
case("  protocol survives", _p["protocol"] == 1)
case("  and the key is big-endian in the last three bytes",
     "Key: 00 00 00 00 00 12 34 56" in _s)

_t = write(433920000, 0x123456, 24, 1, te=403)
case("TE is written when there is one", "TE: 403" in _t)
case("  and omitted when there is not", "TE:" not in _s)
case("  and a file with TE still parses", parse(_t)[1]["te"] == 403)

for _bits in range(1, 33):
    for _v in (0, 1, (1 << _bits) - 1, 0x123456 & ((1 << _bits) - 1)):
        for _hz in (280000000, 433920000, 868350000, 960000000):
            _w = write(_hz, _v, _bits, 1)
            assert _w is not None, (_hz, _v, _bits)
            _rr, _pp = parse(_w)
            assert _rr == OK, (_rr, _w)
            assert _pp["frequency"] == _hz
            assert _pp["bitLength"] == _bits
            assert _pp["value"] == _v, (_v, _pp["value"])
            assert _pp["protocol"] == 1
            checks += 1

# Rolling codes are named, and the name is what changes the message.
for _name in ROLLING_CODES:
    _f = KEYFILE.replace("Princeton", _name).replace("Bit: 24", "Bit: 66")
    _rr, _ = parse(_f)
    assert _rr == ROLLING, (_name, _rr)
    checks += 1
    # Short ones too: the refusal is about the protocol, not the length.
    _f2 = KEYFILE.replace("Princeton", _name)
    assert parse(_f2)[0] == ROLLING, _name
    checks += 1

# ── and the firmware keeps protocol 0 visible ─────────────────────────────
#
# The transcription above proves the parser hands back 0 for a name it will
# not guess at. What that is worth depends entirely on what subghz.cpp does
# next, and rc-switch does the unhelpful thing on its own: setProtocol()
# clamps anything below 1 up to 1, so an unguarded send transmits Princeton
# timing under another protocol's name and looks like it worked.

import io as _io
import os as _os

_SUB = _os.path.join(_os.path.dirname(_os.path.abspath(__file__)),
                     "..", "ESP32-DIV", "subghz.cpp")
_src = _io.open(_SUB, encoding="utf-8", errors="replace").read()


def _body(sig):
    i = _src.index(sig)
    j = _src.index("\n}\n", i)
    return _src[i:j]


_send = _body("void transmitProfile(int index) {")
assert "protocol == 0" in _send, \
    "transmitProfile does not check for an unmapped protocol; rc-switch " \
    "will clamp it to 1 and transmit Princeton timing instead"
checks += 1

# The guard has to come before the radio is told anything, not after.
assert _send.index("protocol == 0") < _send.index("setProtocol"), \
    "the unmapped-protocol guard runs after setProtocol, which is too late"
checks += 1

# And before the radio gates, not after them. A profile with no mapping
# cannot be sent by any radio, so refusing it with "fit a CC1101" sends
# somebody after hardware that will not help, and it buries the only
# refusal that needs no hardware behind one that does.
assert (_send.index("protocol == 0")
        < _send.index("cc1101ReadyForAction")), (
    "the unmapped-protocol refusal sits behind the CC1101 gate, so it asks "
    "for a radio that would not change the answer")
checks += 1
assert (_send.index("protocol == 0")
        < _send.index("Stealth::refuseAction")), (
    "the unmapped-protocol refusal sits behind the Stealth gate")
checks += 1

# The two radio gates still have to be there, just later.
assert "cc1101ReadyForAction" in _send, "the send no longer checks for a radio"
assert "Stealth::refuseAction" in _send, "the send no longer honours Stealth"
checks += 2
checks += 1

_imp = _body("void importSelected(")
assert "parsed.protocol == 0" in _imp, \
    "the import stores an unmapped protocol without saying so"
checks += 1
assert "protocolName" in _imp, \
    "the import does not name the protocol it cannot send, which is the " \
    "part that says which decoder is missing"
checks += 1

# A bare digit reads as an answer. 0 is the absence of one.
assert 'tft.print("none")' in _src, \
    "the browser prints protocol 0 as a number rather than as no mapping"
checks += 1

print("ok -- %d checks" % checks)
print("RAW is refused three ways; only Princeton claims an rc-switch number,")
print("and protocol 0 is loud at the import, the browser and the send")
