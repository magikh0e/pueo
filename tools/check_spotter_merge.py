"""Checks the address-merging rules in Spotter's findOrAdd().

A camera that randomises its MAC gets a new row every time it rotates, and
48 rows do not last long against that. findOrAdd folds a new randomised
address into an existing row when the probe-request fingerprint matches.

That fold is the dangerous part. Two cameras of the same model have the same
element set, so a rule that merges on fingerprint alone would report one
device where there are two -- and under-reporting surveillance hardware is
the failure that matters here. The rule is therefore narrow: both addresses
must be locally administered, because a globally unique MAC is a real
identifier and a device using one is not hiding.

This transcribes the rule and pins it down. It is a model, not the compiled
C; what it checks is that the conditions say what they are meant to say.

    python tools/check_spotter_merge.py
"""
K_MAX_HITS = 48


class Hit:
    def __init__(self, mac, via_ble, fp):
        self.mac = bytes(mac)
        self.viaBle = via_ble
        self.fingerprint = fp
        self.addrChanges = 0


class Table:
    def __init__(self):
        self.hits = []

    def find_or_add(self, mac, via_ble, fp):
        mac = bytes(mac)
        for h in self.hits:
            if h.mac == mac:
                return h

        # the fold: both sides locally administered, same fingerprint, WiFi
        if fp != 0 and not via_ble and (mac[0] & 0x02):
            for h in self.hits:
                if not h.viaBle and h.fingerprint == fp and (h.mac[0] & 0x02):
                    h.mac = mac
                    if h.addrChanges < 255:
                        h.addrChanges += 1
                    return h

        if len(self.hits) >= K_MAX_HITS:
            return None
        h = Hit(mac, via_ble, fp)
        self.hits.append(h)
        return h


# This check does not read the firmware: it reimplements findOrAdd() below
# and tests the reimplementation. Dropping both locally-administered tests
# from the real one, which is the under-reporting bug this exists to
# prevent, left this printing "16 merge rules hold" and exiting 0.
#
# So the C is pinned. This cannot tell you the transcription is right; it
# stops it being wrong without anyone knowing. Re-pin only after reading the
# function and bringing the Python into line.
from transcript_guard import guard

guard("Spotter.cpp", "findOrAdd", "2795a552285984fd")


def rnd(n):
    """A locally administered address: bit 0x02 of the first octet set."""
    return bytes([0x02 | (n & 0xF0), 0x11, 0x22, 0x33, 0x44, n & 0xFF])


def glob(n):
    """A real vendor address. b4:1e:52 is Flock Safety's block."""
    return bytes([0xB4, 0x1E, 0x52, 0x00, 0x00, n & 0xFF])


FP_A = 0x25567F65
FP_B = 0x1234ABCD

checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


# one radio rotating: the case the fold exists for
t = Table()
for i in range(20):
    t.find_or_add(rnd(i), False, FP_A)
case("20 randomised addresses, one fingerprint -> one row", len(t.hits) == 1)
case("and 19 address changes counted", t.hits[0].addrChanges == 19)
case("row wears the most recent address", t.hits[0].mac == rnd(19))

# two real cameras of the same model must not be folded together
t = Table()
t.find_or_add(glob(1), False, FP_A)
t.find_or_add(glob(2), False, FP_A)
case("two globally unique MACs, same fingerprint -> two rows", len(t.hits) == 2)
case("and neither is marked as having changed address",
     all(h.addrChanges == 0 for h in t.hits))

# a randomised address must not be folded into a row holding a real one
t = Table()
t.find_or_add(glob(1), False, FP_A)
t.find_or_add(rnd(1), False, FP_A)
case("randomised MAC does not join a row with a real MAC", len(t.hits) == 2)

# nor the other way round
t = Table()
t.find_or_add(rnd(1), False, FP_A)
t.find_or_add(glob(1), False, FP_A)
case("real MAC does not join a row with a randomised MAC", len(t.hits) == 2)

# different element sets are different devices
t = Table()
t.find_or_add(rnd(1), False, FP_A)
t.find_or_add(rnd(2), False, FP_B)
case("different fingerprints -> different rows", len(t.hits) == 2)

# no fingerprint means no evidence to merge on
t = Table()
t.find_or_add(rnd(1), False, 0)
t.find_or_add(rnd(2), False, 0)
case("fingerprint 0 never merges", len(t.hits) == 2)

# BLE rows carry no fingerprint and must never be folded
t = Table()
t.find_or_add(rnd(1), True, FP_A)
t.find_or_add(rnd(2), True, FP_A)
case("BLE never merges", len(t.hits) == 2)

# a WiFi frame must not be folded into a BLE row
t = Table()
t.find_or_add(rnd(1), True, FP_A)
t.hits[0].fingerprint = FP_A
t.find_or_add(rnd(2), False, FP_A)
case("WiFi does not join a BLE row", len(t.hits) == 2)

# an address already known is found, not merged and not duplicated
t = Table()
a = t.find_or_add(rnd(1), False, FP_A)
b = t.find_or_add(rnd(1), False, FP_A)
case("a known address returns its own row", a is b and len(t.hits) == 1)
case("and is not counted as an address change", a.addrChanges == 0)

# the table still fills, and still refuses rather than evicting
t = Table()
for i in range(K_MAX_HITS):
    t.find_or_add(glob(i), False, 0)
case("table fills at kMaxHits", len(t.hits) == K_MAX_HITS)
case("and returns null rather than evicting a row in front of the operator",
     t.find_or_add(glob(200), False, 0) is None)

# the point of it all: rotation no longer exhausts the table
t = Table()
for i in range(500):
    t.find_or_add(rnd(i), False, FP_A)
case("500 rotations of one radio still leave 47 rows free", len(t.hits) == 1)

print("ok -- %d merge rules hold" % checks)
