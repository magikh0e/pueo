"""Checks the request builder and response reader in FastPairProbe.cpp.

The probe itself needs a radio and a second device, so it cannot be checked
here. What can be checked is the part that decides what sixteen bytes go on
the wire, and that is worth checking, because the probe's whole logic rests
on one field: the Provider's own address at bytes 2..7.

That field is the security property being tested. A Provider decrypts the
request and compares those six bytes to its own address; if it cannot derive
the same AES key it gets noise there and must stay silent. If this builder
put the address in the wrong place, or byte-reversed it, a correct Provider
would also stay silent, and the probe would report every device as "no
response" while testing nothing at all. A probe that always says no is
indistinguishable from a fleet of secure devices.

    python tools/check_fastpair_probe.py
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

guard("FastPairProbe.cpp", "parseResponse", "1c54478f5429e68f")
guard("FastPairProbe.cpp", "buildRequest", "97d3cfdbf530d4b7")

MSG_REQUEST = 0x00
MSG_RESPONSE = 0x01

FLAG_DISCOVERABLE = 0x80
FLAG_INITIATE_BONDING = 0x40
FLAG_RETROACTIVE_WRITE = 0x10


def build_request(flags, provider_addr, seeker_addr, salt):
    """FastPairProbe::buildRequest(), transcribed."""
    out = bytearray(16)
    out[0] = MSG_REQUEST
    out[1] = flags
    if provider_addr is not None:
        out[2:8] = provider_addr
    if seeker_addr is not None:
        out[8:14] = seeker_addr
        if salt is not None:
            out[14:16] = salt[:2]
    elif salt is not None:
        out[8:16] = salt[:8]
    return bytes(out)


def parse_response(block, expect_addr):
    """FastPairProbe::parseResponse(), transcribed."""
    if block is None or block[0] != MSG_RESPONSE:
        return False, False
    matches = expect_addr is not None and block[1:7] == bytes(expect_addr)
    return True, matches


checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


PROVIDER = bytes([0xAA, 0xBB, 0xCC, 0xDD, 0xEE, 0xFF])
SEEKER = bytes([0x11, 0x22, 0x33, 0x44, 0x55, 0x66])
SALT = bytes([0xD1, 0xD2, 0xD3, 0xD4, 0xD5, 0xD6, 0xD7, 0xD8])

# --- the shape the probe actually sends ------------------------------------
r = build_request(FLAG_DISCOVERABLE, PROVIDER, None, SALT)
case("sixteen bytes exactly", len(r) == 16)
case("type byte is a request", r[0] == MSG_REQUEST)
case("flags are where they belong", r[1] == FLAG_DISCOVERABLE)
case("THE field: provider address at bytes 2..7", r[2:8] == PROVIDER)
case("address is not byte-reversed", r[2] == 0xAA and r[7] == 0xFF)
case("no seeker address means the tail is all salt", r[8:16] == SALT)

# --- with a seeker address -------------------------------------------------
r = build_request(FLAG_INITIATE_BONDING, PROVIDER, SEEKER, SALT)
case("provider address is unmoved by a seeker address", r[2:8] == PROVIDER)
case("seeker address at bytes 8..13", r[8:14] == SEEKER)
case("only two salt bytes survive", r[14:16] == SALT[:2])
case("and they are the first two", r[14] == 0xD1 and r[15] == 0xD2)

# --- the builder must not leave anything uninitialised ---------------------
r = build_request(0, PROVIDER, None, None)
case("no salt still yields sixteen bytes", len(r) == 16)
case("and the tail is zero, not stack noise", r[8:16] == bytes(8))
r = build_request(0, None, None, None)
case("no provider address zeroes the field", r[2:8] == bytes(6))

# --- flags are passed through, not reinterpreted ---------------------------
for f in (0x00, 0x10, 0x40, 0x80, 0xFF, 0x55):
    case("flags 0x%02X pass through" % f,
         build_request(f, PROVIDER, None, SALT)[1] == f)

# --- every address byte reaches the wire unchanged -------------------------
for i in range(6):
    addr = bytearray(6)
    addr[i] = 0xA5
    r = build_request(0, bytes(addr), None, SALT)
    case("address byte %d survives" % i, r[2 + i] == 0xA5)
    case("and disturbs nothing else" ,
         sum(r[2:8]) == 0xA5)

# --- reading a response ----------------------------------------------------
resp = bytes([MSG_RESPONSE]) + PROVIDER + bytes(9)
ok, matched = parse_response(resp, PROVIDER)
case("a well-formed response is recognised", ok)
case("and its address matches", matched)

ok, matched = parse_response(resp, SEEKER)
case("a response naming someone else is still well formed", ok)
case("but does not match", not matched)

ok, matched = parse_response(bytes([MSG_REQUEST]) + PROVIDER + bytes(9),
                             PROVIDER)
case("a request echoed back is not a response", not ok and not matched)

for t in range(256):
    if t == MSG_RESPONSE:
        continue
    ok, _ = parse_response(bytes([t]) + PROVIDER + bytes(9), PROVIDER)
    case("type 0x%02X is not a response" % t, not ok)

# --- noise must not decode as a match --------------------------------------
# This is the one that matters for false positives: a device that notifies
# garbage, or a block that decrypted to nothing meaningful, must not be
# reported as a matched response.
import random
random.seed(20260919)
spurious = 0
for _ in range(50000):
    block = bytes(random.getrandbits(8) for _ in range(16))
    ok, matched = parse_response(block, PROVIDER)
    if matched:
        spurious += 1
        # only legitimate when the noise really did name the address
        assert block[0] == MSG_RESPONSE and block[1:7] == PROVIDER
    checks += 1
case("random blocks essentially never match the address", spurious == 0)

# --- round trip: build, then read what a correct provider would send -------
r = build_request(FLAG_DISCOVERABLE, PROVIDER, None, SALT)
reply = bytes([MSG_RESPONSE]) + r[2:8] + bytes(9)
ok, matched = parse_response(reply, PROVIDER)
case("a provider echoing the address we sent it matches", ok and matched)

print("ok -- %d checks" % checks)
print("the provider address sits at bytes 2..7, unreversed; that field is "
      "the test")
