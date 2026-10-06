"""Checks ESP32-DIV/Eapol.cpp: locating, classifying and tracking.

Finding the 802.1X payload in an 802.11 data frame means computing the
header length from the frame control field, and every field involved is
chosen by whoever transmitted the frame. The two ways to get it wrong are
reading past a short frame and knowing only some of the header layouts;
ESP32Marauder's version does both, which is why this one exists.

This transcribes the C line for line, behind a buffer that refuses any read
outside the frame, and checks three things: that the offset is right for
every header layout, that nothing outside a frame is ever read, and that the
frames which should be rejected are.

It says nothing about the compiled C. What it checks is that the arithmetic
the C encodes is right.

    python tools/check_eapol.py

Synthetic frames can only ask about a layout somebody thought of: one absent
from the parser is absent from the test for the same reason, because the
same hand wrote both. So the last pass runs the same transcribed functions
over captures in tools/fixtures/, which is the part neither hand wrote. See
the README there for what may go in it.

    python tools/check_eapol.py --capture some.pcap

reads any classic pcap at link type 105 or 127 and prints what it found,
asserting nothing. That is for checking this checker against a capture you
trust and have no right to redistribute.
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

guard("Eapol.cpp", "classify", "da88fb833947342c")
guard("Eapol.cpp", "findPayload", "c1704ad9d80eb612")

TYPE_DATA = 0x02
SUBTYPE_QOS = 0x08
SUBTYPE_NULL = 0x04
TO_DS = 0x01
FROM_DS = 0x02
PROTECTED = 0x40
ORDER = 0x80
SNAP_LEN = 8


class Frame:
    """Bytes with a hard bound; any read outside [0, len) is a failure."""

    def __init__(self, data, sig_len):
        self.data = data
        self.len = sig_len
        self.max_read = -1

    def __getitem__(self, i):
        assert 0 <= i < self.len, "OUT OF BOUNDS READ at %d (len %d)" % (i, self.len)
        if i > self.max_read:
            self.max_read = i
        return self.data[i]


def header_length(f, length):
    if f is None or length < 2:
        return -1
    if ((f[0] >> 2) & 0x03) != TYPE_DATA:
        return -1
    subtype = (f[0] >> 4) & 0x0F
    if subtype & SUBTYPE_NULL:
        return -1
    flags = f[1]
    hdr = 24
    if (flags & (TO_DS | FROM_DS)) == (TO_DS | FROM_DS):
        hdr += 6
    if subtype & SUBTYPE_QOS:
        hdr += 2
        if flags & ORDER:
            hdr += 4
    return hdr


def find_payload(f, length):
    hdr = header_length(f, length)
    if hdr < 0:
        return -1
    if f[1] & PROTECTED:
        return -1
    if hdr + SNAP_LEN > length:
        return -1
    if f[hdr] != 0xAA or f[hdr + 1] != 0xAA or f[hdr + 2] != 0x03:
        return -1
    if f[hdr + 6] != 0x88 or f[hdr + 7] != 0x8E:
        return -1
    return hdr + SNAP_LEN


def locate(data, sig_len=None, want_frame=False):
    n = len(data) if sig_len is None else sig_len
    f = Frame(bytes(data), n)
    got = find_payload(f, n)
    return (got, f) if want_frame else got


def frame(qos=False, wds=False, order=False, protected=False, null=False,
          snap=True, ethertype=(0x88, 0x8E), body=16, mgmt=False):
    """Build a frame with the requested header shape."""
    subtype = 0
    if qos:
        subtype |= SUBTYPE_QOS
    if null:
        subtype |= SUBTYPE_NULL
    fc0 = (subtype << 4) | ((0x00 if mgmt else TYPE_DATA) << 2)
    fc1 = 0
    if wds:
        fc1 |= TO_DS | FROM_DS
    if order:
        fc1 |= ORDER
    if protected:
        fc1 |= PROTECTED

    hdr = 24 + (6 if wds else 0) + ((2 + (4 if order else 0)) if qos else 0)
    out = bytearray([fc0, fc1]) + bytearray(b"\x11" * (hdr - 2))
    if snap:
        out += bytes([0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00,
                      ethertype[0], ethertype[1]])
    else:
        out += bytes([0x00] * 8)
    out += b"\x22" * body
    return bytes(out), hdr


checks = 0


def case(name, cond):
    global checks
    assert cond, "FAILED: " + name
    checks += 1


# --- every header layout finds the payload in the right place --------------
for qos in (False, True):
    for wds in (False, True):
        for order in (False, True):
            data, hdr = frame(qos=qos, wds=wds, order=order)
            expect = hdr + SNAP_LEN
            got = locate(data)
            case("layout qos=%d wds=%d order=%d -> %d" % (qos, wds, order, expect),
                 got == expect)

# the two layouts Marauder knows, spelled out
case("3-address non-QoS payload begins at 32", locate(frame()[0]) == 32)
case("3-address QoS payload begins at 34", locate(frame(qos=True)[0]) == 34)
# and the two it does not
case("4-address non-QoS payload begins at 38", locate(frame(wds=True)[0]) == 38)
case("QoS with HT Control begins at 38", locate(frame(qos=True, order=True)[0]) == 38)

# Order without QoS is not HT Control and must not shift anything
case("Order on a non-QoS frame does not move the payload",
     locate(frame(order=True)[0]) == 32)

# --- frames that must be rejected -----------------------------------------
case("management frames rejected", locate(frame(mgmt=True)[0]) == -1)
case("null data rejected", locate(frame(null=True)[0]) == -1)
case("QoS null rejected", locate(frame(qos=True, null=True)[0]) == -1)
case("protected frames rejected", locate(frame(protected=True)[0]) == -1)
case("non-SNAP payload rejected", locate(frame(snap=False)[0]) == -1)
case("SNAP with another ethertype rejected",
     locate(frame(ethertype=(0x08, 0x00))[0]) == -1)

# --- truncation ------------------------------------------------------------
full, hdr = frame()
for n in range(0, len(full) + 1):
    got = locate(full, n)
    if n >= hdr + SNAP_LEN:
        case("len %d still locates" % n, got == hdr + SNAP_LEN)
    else:
        case("len %d rejected rather than read past" % n, got == -1)

# The 33-byte case, which is the one the fixed-offset version gets wrong.
# A 3-address frame this long does contain a whole SNAP header, so 32 is the
# right answer and the locator gives it. The point is what was NOT read:
# Marauder tests payload[32] and payload[33], and at this length index 33 is
# off the end. The checked buffer above would have caught that.
got33, f33 = locate(full, 33, want_frame=True)
case("a 33-byte frame locates at 32", got33 == 32)
case("and the highest byte it read was 31, not 33", f33.max_read == 31)

# --- fuzz ------------------------------------------------------------------
import random
random.seed(20260919)
for _ in range(80000):
    n = random.randint(0, 120)
    data = bytes(random.getrandbits(8) for _ in range(n))
    got = locate(data, n)
    assert got == -1 or (0 < got <= n), "nonsense offset %r for len %d" % (got, n)
    checks += 1

# frames that lie about being long, truncated at every length
for n in range(0, 64):
    data, _ = frame(qos=True, wds=True, order=True)
    locate(data, min(n, len(data)))
    checks += 1

# === classification and tracking continue below ===


# ═══ classification ═══════════════════════════════════════════════════════
PACKET_TYPE_KEY = 0x03
KEY_TYPE = 1 << 3          # set = pairwise, clear = group
KEY_ACK = 1 << 7
KEY_MIC = 1 << 8
SECURE = 1 << 9


def classify(f, length, off):
    if f is None or off < 0:
        return None
    if off + 5 + 1 >= length:
        return None
    if f[off + 1] != PACKET_TYPE_KEY:
        return None
    key_info = (f[off + 5] << 8) | f[off + 6]
    if not (key_info & KEY_TYPE):
        return None            # group rekey, not the pairwise handshake
    ack = bool(key_info & KEY_ACK)
    mic = bool(key_info & KEY_MIC)
    sec = bool(key_info & SECURE)
    if ack and not mic and not sec:
        return "M1"
    if not ack and mic and not sec:
        return "M2"
    if ack and mic and sec:
        return "M3"
    if not ack and mic and sec:
        return "M4"
    return None


def eapol_frame(msg=None, key_info=None, packet_type=PACKET_TYPE_KEY,
                body=95, **kw):
    """A data frame whose 802.1X payload is an EAPOL-Key of the given kind."""
    if key_info is None:
        key_info = KEY_TYPE | {"M1": KEY_ACK,
                               "M2": KEY_MIC,
                               "M3": KEY_ACK | KEY_MIC | SECURE,
                               "M4": KEY_MIC | SECURE}[msg]
    head, hdr = frame(body=0, **kw)
    payload = bytearray([0x02, packet_type, 0x00, 0x5F, 0x02])
    payload += bytes([(key_info >> 8) & 0xFF, key_info & 0xFF])
    payload += b"3" * body
    return bytes(head) + bytes(payload), hdr + SNAP_LEN


def classify_frame(data, sig_len=None):
    n = len(data) if sig_len is None else sig_len
    f = Frame(bytes(data), n)
    off = find_payload(f, n)
    return classify(f, n, off)


for m in ("M1", "M2", "M3", "M4"):
    data, off = eapol_frame(m)
    case("%s classifies as %s" % (m, m), classify_frame(data) == m)
    case("%s payload sits at %d" % (m, off), locate(data) == off)

# every header layout classifies the same message
for qos in (False, True):
    for wds in (False, True):
        data, _ = eapol_frame("M3", qos=qos, wds=wds)
        case("M3 over qos=%d wds=%d" % (qos, wds), classify_frame(data) == "M3")

case("an EAP packet is not a key frame",
     classify_frame(eapol_frame("M1", packet_type=0x00)[0]) is None)
case("an EAPOL-Start is not a key frame",
     classify_frame(eapol_frame("M1", packet_type=0x01)[0]) is None)
case("key info with neither ack nor mic classifies as nothing",
     classify_frame(eapol_frame(key_info=0x0000)[0]) is None)
case("a group rekey looks like M3 but is not pairwise, so it is rejected",
     classify_frame(eapol_frame(key_info=KEY_ACK | KEY_MIC | SECURE)[0]) is None)
case("and the same bits with Key Type set do classify as M3",
     classify_frame(eapol_frame(key_info=KEY_TYPE | KEY_ACK | KEY_MIC | SECURE)[0])
     == "M3")

# truncation through the key info field
data, off = eapol_frame("M2")
for n in range(0, len(data) + 1):
    got = classify_frame(data, n)
    if n > off + 6:
        case("len %d still classifies" % n, got == "M2")
    else:
        case("len %d gives up rather than reading past" % n, got is None)

# ═══ addresses ════════════════════════════════════════════════════════════
def addresses(f, length):
    if length < 22:
        return None
    flags = f[1]
    to_ds, from_ds = bool(flags & TO_DS), bool(flags & FROM_DS)
    if to_ds and from_ds:
        return None
    if from_ds:
        return (10, 4)
    if to_ds:
        return (4, 10)
    return (16, 10)


case("neither DS bit set is ad-hoc: bssid addr3, station addr2",
     addresses(Frame(frame()[0], 40), 40) == (16, 10))
f_from, _ = frame()
f_from = bytearray(f_from); f_from[1] |= FROM_DS
case("FromDS: bssid addr2, station addr1",
     addresses(Frame(bytes(f_from), 40), 40) == (10, 4))
f_to, _ = frame()
f_to = bytearray(f_to); f_to[1] |= TO_DS
case("ToDS: bssid addr1, station addr2",
     addresses(Frame(bytes(f_to), 40), 40) == (4, 10))
case("WDS has no single bssid", addresses(Frame(frame(wds=True)[0], 46), 46) is None)
case("a 20-byte frame has no three addresses",
     addresses(Frame(frame()[0], 20), 20) is None)

# ═══ the tracker ══════════════════════════════════════════════════════════
MAX_HANDSHAKES = 16
MASK = {"M1": 0x01, "M2": 0x02, "M3": 0x04, "M4": 0x08}


class Tracker:
    def __init__(self):
        self.rows = []

    def observe(self, bssid, station, msg, now=0):
        if msg is None:
            return
        for r in self.rows:
            if r["bssid"] == bssid:
                if r["station"] != station:
                    r["station"] = station
                    r["seen"] = 0
                    r["firstMs"] = now
                r["seen"] |= MASK[msg]
                r["lastMs"] = now
                return
        if len(self.rows) >= MAX_HANDSHAKES:
            return
        self.rows.append(dict(bssid=bssid, station=station, seen=MASK[msg],
                              firstMs=now, lastMs=now))

    def usable(self, r):
        return (r["seen"] & 0x06) == 0x06


t = Tracker()
for m in ("M1", "M2", "M3", "M4"):
    t.observe(b"AP", b"STA", m)
case("four messages from one pair make one row", len(t.rows) == 1)
case("and all four flags are set", t.rows[0]["seen"] == 0x0F)
case("which is usable", t.usable(t.rows[0]))

t = Tracker()
t.observe(b"AP", b"STA", "M1")
case("M1 alone is not usable", not t.usable(t.rows[0]))
t.observe(b"AP", b"STA", "M2")
case("M1 and M2 are not usable either", not t.usable(t.rows[0]))
t.observe(b"AP", b"STA", "M3")
case("M2 and M3 are", t.usable(t.rows[0]))

t = Tracker()
t.observe(b"AP", b"STA-A", "M2")
t.observe(b"AP", b"STA-B", "M3")
case("a second station on one AP does not fake a handshake",
     not t.usable(t.rows[0]) and t.rows[0]["seen"] == MASK["M3"])
case("and the row follows the newer station", t.rows[0]["station"] == b"STA-B")

t = Tracker()
for i in range(MAX_HANDSHAKES + 8):
    t.observe(bytes([i]), b"STA", "M2")
case("the table fills at %d" % MAX_HANDSHAKES, len(t.rows) == MAX_HANDSHAKES)
case("and drops rather than evicting", t.rows[0]["bssid"] == bytes([0]))

# ═══ fuzz the whole path ══════════════════════════════════════════════════
for _ in range(40000):
    n = random.randint(0, 140)
    data = bytes(random.getrandbits(8) for _ in range(n))
    got = classify_frame(data, n)
    assert got in (None, "M1", "M2", "M3", "M4"), got
    checks += 1

# ═══ the deauth assist ════════════════════════════════════════════════════
#
# This is the only part of Eapol that causes the radio to transmit, so its
# stop conditions are the thing worth proving. There are three: the target is
# captured, the burst cap is reached, or it was never armed. None of them may
# be reachable only by luck.
ASSIST_INTERVAL_MS = 3000
ASSIST_MAX_BURSTS = 6


class Assist:
    def __init__(self, tracker):
        self.t = tracker
        self.armed = False
        self.bursts = 0
        self.last = 0

    def arm(self, on):
        self.armed = on
        self.bursts = 0
        self.last = 0

    def due(self, now):
        if not self.armed:
            return -1
        if self.bursts >= ASSIST_MAX_BURSTS:
            self.armed = False
            return -1
        if self.bursts > 0 and (now - self.last) < ASSIST_INTERVAL_MS:
            return -1
        target = -1
        for i, r in enumerate(self.t.rows):
            if r["seen"] != 0 and not self.t.usable(r):
                target = i
                break
        if target < 0:
            if len(self.t.rows) > 0:
                self.armed = False
            return -1
        self.last = now
        self.bursts += 1
        return target


t = Tracker(); a = Assist(t)
t.observe(b"AP", b"STA", "M1")
case("disarmed sends nothing", a.due(0) == -1 and a.due(10**6) == -1)

a.arm(True)
case("armed with a partial handshake fires at once", a.due(1000) == 0)
case("and not again inside the interval", a.due(1000 + ASSIST_INTERVAL_MS - 1) == -1)
case("but does after it", a.due(1000 + ASSIST_INTERVAL_MS) == 0)

# the cap
t = Tracker(); a = Assist(t)
t.observe(b"AP", b"STA", "M1")
a.arm(True)
fired = 0
now = 0
for _ in range(500):
    now += ASSIST_INTERVAL_MS
    if a.due(now) >= 0:
        fired += 1
case("never more bursts than the cap", fired == ASSIST_MAX_BURSTS)
case("and it disarms itself at the cap", not a.armed)

# capture stops it
t = Tracker(); a = Assist(t)
t.observe(b"AP", b"STA", "M1")
a.arm(True)
case("fires while incomplete", a.due(0) == 0)
t.observe(b"AP", b"STA", "M2")
t.observe(b"AP", b"STA", "M3")
case("stops once that network has M2+M3", a.due(ASSIST_INTERVAL_MS) == -1)
case("and disarms rather than idling armed", not a.armed)

# nothing tracked yet: stay armed, send nothing
t = Tracker(); a = Assist(t)
a.arm(True)
case("nothing to target sends nothing", a.due(0) == -1)
case("but stays armed, since a handshake may yet appear", a.armed)

# re-arming resets the budget
t = Tracker(); a = Assist(t)
t.observe(b"AP", b"STA", "M1")
a.arm(True)
now = 0
for _ in range(20):
    now += ASSIST_INTERVAL_MS
    a.due(now)
case("budget spent", not a.armed)
a.arm(True)
case("re-arming is deliberate and gives a fresh budget",
     a.armed and a.bursts == 0 and a.due(now) == 0)

# fuzz: random time jumps and random handshake progress, many runs
import random as _r
for trial in range(4000):
    t = Tracker(); a = Assist(t)
    armed_at = None
    fired = 0
    now = 0
    for step in range(60):
        now += _r.randint(0, 5000)
        if _r.random() < 0.05:
            a.arm(True)
            fired = 0
        if _r.random() < 0.15:
            t.observe(bytes([_r.randint(0, 3)]), b"STA",
                      _r.choice(["M1", "M2", "M3", "M4"]), now)
        if a.due(now) >= 0:
            fired += 1
        assert fired <= ASSIST_MAX_BURSTS, "burst cap exceeded: %d" % fired
        if not a.armed:
            assert a.due(now) == -1, "fired while disarmed"
    checks += 1

# and the invariant stated plainly: frames per arming are bounded
case("an arming can cost at most %d frames" % (ASSIST_MAX_BURSTS * 2),
     ASSIST_MAX_BURSTS * 2 == 12)

# --- and the same parser, over frames nobody here wrote --------------------
#
# Everything above builds its own frames, so it can only ever ask about a
# layout somebody thought of. A capture does not have that property. These
# read a classic pcap, strip the radiotap header Pueo writes, and run the
# transcribed findPayload and classify over every frame in it.
#
# tools/fixtures/manifest.json says what each file should yield. A fixture
# swapped for one that does not exercise the same shapes fails rather than
# quietly testing less.

import json
import os
import struct
import sys

FIXTURES = os.path.join(os.path.dirname(os.path.abspath(__file__)), "fixtures")

PCAP_MAGICS = {
    b"\xd4\xc3\xb2\xa1": ("<", 1),
    b"\xa1\xb2\xc3\xd4": (">", 1),
    b"\x4d\x3c\xb2\xa1": ("<", 1000),
    b"\xa1\xb2\x3c\x4d": (">", 1000),
}

DLT_IEEE802_11 = 105
DLT_IEEE802_11_RADIOTAP = 127


def read_pcap(path):
    """(link type, [frame bytes]). Classic pcap only; pcapng is not read."""
    raw = open(path, "rb").read()
    if len(raw) < 24:
        raise ValueError("%s: too short to be a pcap" % path)
    if raw[:4] not in PCAP_MAGICS:
        raise ValueError("%s: not a classic pcap (magic %s)"
                         % (path, raw[:4].hex()))
    end = PCAP_MAGICS[raw[:4]][0]
    link = struct.unpack(end + "I", raw[20:24])[0]
    out, i = [], 24
    while i + 16 <= len(raw):
        _ts, _us, caplen, _orig = struct.unpack(end + "IIII", raw[i:i + 16])
        i += 16
        if caplen > len(raw) - i:
            raise ValueError("%s: record claims %d bytes, %d remain"
                             % (path, caplen, len(raw) - i))
        out.append(raw[i:i + caplen])
        i += caplen
    return link, out


def strip_radiotap(buf):
    """The 802.11 frame inside a radiotap-prefixed record, or None.

    it_len is little endian regardless of the pcap's own byte order, which
    is in the radiotap spec and is the kind of thing that is easy to get
    wrong once and never notice, because the common case is a capture whose
    byte order already agrees.
    """
    if len(buf) < 8:
        return None
    if buf[0] != 0:                       # it_version, always zero so far
        return None
    it_len = struct.unpack("<H", buf[2:4])[0]
    if it_len < 8 or it_len > len(buf):
        return None
    return buf[it_len:]


def survey(path):
    """What the transcribed parser makes of every frame in a capture."""
    link, records = read_pcap(path)
    if link not in (DLT_IEEE802_11, DLT_IEEE802_11_RADIOTAP):
        raise ValueError("%s: link type %d is not 802.11" % (path, link))

    seen = {"frames": len(records), "eapol": 0, "headers": {}, "msgs": {},
            "protected": 0, "short": 0}
    for rec in records:
        body = rec if link == DLT_IEEE802_11 else strip_radiotap(rec)
        if body is None or len(body) < 2:
            seen["short"] += 1
            continue
        n = len(body)
        fr = Frame(bytes(body), n)
        off = find_payload(fr, n)
        if off < 0:
            # Data frames only. The Protected bit on a management frame is
            # either 802.11w or a corrupt frame, and neither is the thing
            # findPayload refuses; counting those made the number a measure
            # of how much garbage the capture held.
            is_data = ((body[0] >> 2) & 0x03) == TYPE_DATA
            if is_data and n >= 2 and (body[1] & PROTECTED):
                seen["protected"] += 1
            continue
        seen["eapol"] += 1
        hdr = off - SNAP_LEN
        seen["headers"][hdr] = seen["headers"].get(hdr, 0) + 1
        msg = classify(fr, n, off)
        key = msg if msg else "not-a-key-frame"
        seen["msgs"][key] = seen["msgs"].get(key, 0) + 1
    return seen


def report(path, seen):
    print("  %s" % os.path.basename(path))
    print("    %d frame(s), %d carrying 802.1X"
          % (seen["frames"], seen["eapol"]))
    for hdr in sorted(seen["headers"]):
        print("      %2d-byte header   %4d" % (hdr, seen["headers"][hdr]))
    for k in sorted(seen["msgs"]):
        print("      %-16s %4d" % (k, seen["msgs"][k]))


# Run by hand against a capture that is not in the repository, to check this
# checker rather than the fixtures. Asserts nothing.
if "--capture" in sys.argv:
    for arg in sys.argv[sys.argv.index("--capture") + 1:]:
        report(arg, survey(arg))
    raise SystemExit(0)

manifest_path = os.path.join(FIXTURES, "manifest.json")
if not os.path.isfile(manifest_path):
    print("no tools/fixtures/manifest.json, so no capture is checked")
else:
    manifest = json.load(open(manifest_path, encoding="utf-8"))
    entries = manifest.get("fixtures", [])
    case("the manifest lists at least one capture", bool(entries))
    for entry in entries:
        name = entry["file"]
        path = os.path.join(FIXTURES, name)
        case("%s: every fixture names a source" % name,
             bool(entry.get("source")))
        case("%s: every fixture names a licence" % name,
             bool(entry.get("licence")))
        case("%s: the file is present" % name, os.path.isfile(path))
        if not os.path.isfile(path):
            continue
        seen = survey(path)
        want = entry["expect"]
        if "frames" in want:
            case("%s: %d frame(s) in the file" % (name, want["frames"]),
                 seen["frames"] == want["frames"])
        case("%s: %d frame(s) carry 802.1X" % (name, want["eapol"]),
             seen["eapol"] == want["eapol"])
        # Pinned because an anonymiser or a trim that dropped the encrypted
        # frames would leave the fixture passing while testing less: a
        # Protected frame is one findPayload has to refuse before it reads
        # anything, and a file without any stops asking that.
        if "protected" in want:
            case("%s: %d encrypted data frame(s) refused"
                 % (name, want["protected"]),
                 seen["protected"] == want["protected"])
        for hdr, n in sorted(want.get("headers", {}).items()):
            case("%s: %d frame(s) at a %s-byte header" % (name, n, hdr),
                 seen["headers"].get(int(hdr), 0) == n)
        for msg, n in sorted(want.get("msgs", {}).items()):
            case("%s: %d %s" % (name, n, msg),
                 seen["msgs"].get(msg, 0) == n)
        # The point of the fixture: a capture with one header layout in it
        # tests nothing the synthetic frames did not already cover.
        if want.get("layouts_at_least"):
            case("%s: more than one header layout" % name,
                 len(seen["headers"]) >= want["layouts_at_least"])


print("ok -- %d checks, no out-of-bounds read" % checks)
print("payload offsets: 32 / 34 / 38 / 38 for 3-addr, QoS, 4-addr, QoS+HT")
print("M1-M4 classify from key info; M2+M3 is what makes a row usable")
print("assist stops on capture, on the %d-burst cap, and when disarmed"
      % ASSIST_MAX_BURSTS)
