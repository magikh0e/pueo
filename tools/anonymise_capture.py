#!/usr/bin/env python3
"""Strip the identifiers out of a capture, leaving every offset where it was.

A capture off a real radio is the only thing that can show the parser frames
nobody here designed. It is also a record of whoever was in earshot: the
BSSIDs, the station addresses and the network names of a street. A fixture
in a public repository must not be that.

So each distinct address is replaced by a synthetic one and each SSID by
filler of the same length. Nothing moves. Every frame keeps its length, its
frame control field, its header layout and its information elements, which
is the whole of what the checks read.

    python tools/anonymise_capture.py in.pcap out.pcap

It verifies its own output before writing it, and that is not decoration.
The first version of this script mapped addresses only in frames of 24
bytes or more, because that is the length a data or management header
needs. An acknowledgement is 14 bytes and carries a real receiver address;
a request-to-send is 20 and carries two. Eight such frames went through a
capture untouched, leaking eleven addresses, and the check written beside
the script shared the assumption and passed. Hence the sweep at the end,
which knows nothing about frame layout and only asks whether any original
address appears anywhere in the bytes being written.

What is preserved, deliberately
-------------------------------
  lengths         Every record and every frame, to the byte. An SSID is
                  overwritten in place, never resized, because an element
                  whose length byte stops matching its contents is a
                  different test.

  broadcast       ff:ff:ff:ff:ff:ff and 00:00:00:00:00:00 pass through. They
                  are not identifiers and a frame addressed to nobody in
                  particular should still read that way.

  the low bits    Bit 0 of the first octet is the group bit and bit 1 is the
                  locally administered bit, and both are kept. Spotter's
                  whole treatment of randomised addresses turns on that
                  second bit, so a fixture that flattened it would be
                  testing a world where nothing randomises.

What is not preserved
---------------------
  Any cryptographic consistency. A handshake MIC is computed over the
  addresses and the SSID, so rewriting them invalidates it. Nothing in this
  firmware verifies a MIC: it locates the payload, reads three bits of the
  key information field and counts which messages it has seen. If something
  here ever does verify one, this script stops being suitable and the
  fixture has to be captured on a network set up to be published.
"""
import struct
import sys
from pathlib import Path

PCAP_MAGICS = {
    b"\xd4\xc3\xb2\xa1": "<", b"\xa1\xb2\xc3\xd4": ">",
    b"\x4d\x3c\xb2\xa1": "<", b"\xa1\xb2\x3c\x4d": ">",
}

BROADCAST = b"\xff" * 6
ZERO = b"\x00" * 6
TYPE_MGMT, TYPE_CTRL, TYPE_DATA = 0x00, 0x01, 0x02
TO_DS, FROM_DS, ORDER = 0x01, 0x02, 0x80
SUBTYPE_QOS = 0x08

# Control subtypes that carry a transmitter address after the receiver one.
# An acknowledgement and a clear-to-send carry only the first.
CTRL_HAS_TA = {0x08, 0x09, 0x0A, 0x0B, 0x0E, 0x0F}

# Management subtypes whose body carries a tagged parameter list, and the
# fixed-field length that precedes it. Reassociation request is 10 because
# its fixed fields end with the current access point's address, which is an
# identifier in the body rather than in a header field.
IE_OFFSETS = {0: 0, 1: 6, 2: 10, 3: 6, 4: 0, 5: 12, 8: 12}

# The same address, in the body rather than the header: a reassociation
# request names the access point it is leaving.
BODY_ADDRS = {2: [24 + 4]}


class Addresses:
    """A stable synthetic address per real one, for one run."""

    def __init__(self):
        self.seen = {}

    def map(self, mac):
        if mac in (BROADCAST, ZERO):
            return mac
        # A group address is a destination, not an identity: 01:00:5e for
        # IPv4 multicast, 33:33 for IPv6, 01:80:c2 for the bridging
        # protocols. Rewriting those would damage the capture without
        # protecting anybody, so the group bit is the line. It also keeps
        # such a sequence out of the sweep below, which otherwise reports a
        # multicast destination echoed in some corrupt frame's payload as a
        # leak, and it is not one.
        if mac[0] & 0x01:
            return mac
        if mac not in self.seen:
            n = len(self.seen) + 1
            # The group and locally administered bits come from the real
            # address so the frame still means what it meant; everything
            # above them is a counter.
            first = 0x02 & ~0x03 | (mac[0] & 0x03)
            self.seen[mac] = bytes([first, 0x00, 0x00,
                                    (n >> 16) & 0xFF,
                                    (n >> 8) & 0xFF, n & 0xFF])
        return self.seen[mac]


def address_spans(body):
    """Every offset in this frame that holds an address.

    Written to over-reach rather than under-reach. A capture holds corrupt
    frames whose type and subtype bits decode to combinations the standard
    does not define, and guessing a layout for those is how addresses get
    left behind. Any six bytes sitting where an address sits is treated as
    one: mapping a run of garbage changes garbage into other garbage, and
    the lengths, which is all the checks read, do not move.
    """
    n = len(body)
    if n < 10:
        return []
    fc0, fc1 = body[0], body[1]
    t = (fc0 >> 2) & 0x03
    st = (fc0 >> 4) & 0x0F

    spans = [4]                                  # addr1, every frame that has one
    if t == TYPE_CTRL:
        if st in CTRL_HAS_TA and n >= 16:
            spans.append(10)
        return spans

    if n >= 16:
        spans.append(10)                         # addr2
    if n >= 22:
        spans.append(16)                         # addr3
    if t == TYPE_DATA and (fc1 & TO_DS) and (fc1 & FROM_DS) and n >= 30:
        spans.append(24)                         # addr4, four-address frames
    if t == TYPE_MGMT:
        for off in BODY_ADDRS.get(st, []):
            if off + 6 <= n:
                spans.append(off)
    return spans


def scrub_ssids(body):
    """Blank every SSID element in place, keeping its length byte."""
    if len(body) < 26:
        return body, 0
    st = (body[0] >> 4) & 0x0F
    if ((body[0] >> 2) & 0x03) != TYPE_MGMT or st not in IE_OFFSETS:
        return body, 0
    i = 24 + IE_OFFSETS[st]
    out = bytearray(body)
    hits = 0
    while i + 2 <= len(out):
        tag, tlen = out[i], out[i + 1]
        if i + 2 + tlen > len(out):
            break
        if tag == 0 and tlen:
            out[i + 2:i + 2 + tlen] = b"X" * tlen
            hits += 1
        i += 2 + tlen
    return bytes(out), hits


def sweep_candidates(bodies):
    """Everything that might be an address, decided by length alone.

    Deliberately not address_spans(). A sweep built from what the mapper
    found can only ever confirm that the mapper rewrote what it found: put
    the old `n < 24` back and the short frames vanish from both sides at
    once, which is the bug reappearing with the alarm disabled. This reads
    offsets 4, 10 and 16, which hold addresses in every frame long enough
    to have them, and decides nothing from the type bits.

    It stops at 16. Offsets 24 and 28 are addresses in a four-address data
    frame and a reassociation request, and in a beacon they are the
    timestamp and the beacon interval, which look exactly like an address
    beginning 00:00 and are not one. Those two are covered by the mapper;
    this is the net underneath it, and a net that cries wolf gets widened
    until it catches nothing.
    """
    out = set()
    for b in bodies:
        for off, need in ((4, 10), (10, 16), (16, 22)):
            if len(b) >= need:
                m = bytes(b[off:off + 6])
                if m[0] & 0x01 or m in (BROADCAST, ZERO):
                    continue
                out.add(m)
    return out


def sweep(blob, originals):
    """Any original address still present in the bytes about to be written."""
    return sorted(m for m in originals if m in blob)


def main():
    if len(sys.argv) != 3:
        print(__doc__)
        return 2
    src, dst = Path(sys.argv[1]), Path(sys.argv[2])
    raw = src.read_bytes()
    if raw[:4] not in PCAP_MAGICS:
        print("not a classic pcap: %s" % raw[:4].hex())
        return 1
    end = PCAP_MAGICS[raw[:4]]
    link = struct.unpack(end + "I", raw[20:24])[0]
    if link not in (105, 127):
        print("link type %d is not 802.11" % link)
        return 1

    addrs = Addresses()
    out = bytearray(raw[:24])
    source_bodies = []
    i, frames, ssids, dropped = 24, 0, 0, 0
    while i + 16 <= len(raw):
        head = raw[i:i + 16]
        _ts, _us, caplen, _orig = struct.unpack(end + "IIII", head)
        i += 16
        rec = bytearray(raw[i:i + caplen])
        i += caplen
        frames += 1

        rtlen = 0
        if link == 127:
            rtlen = struct.unpack("<H", rec[2:4])[0] if len(rec) >= 4 else 0
            if rtlen < 8 or rtlen > len(rec):
                # A record whose radiotap header does not parse used to be
                # written through untouched, which is the one case where
                # "leave it alone" means "publish it". Keep the record, so
                # the frame count holds, and keep nothing in it.
                rec[:] = b"\x00" * len(rec)
                out += head + rec
                dropped += 1
                continue
        body = bytes(rec[rtlen:])
        source_bodies.append(body)

        nb = bytearray(body)
        for off in address_spans(body):
            nb[off:off + 6] = addrs.map(bytes(nb[off:off + 6]))
        scrubbed, n = scrub_ssids(bytes(nb))
        ssids += n
        rec[rtlen:] = scrubbed
        out += head + rec

    # The candidate set comes from the capture rather than from what the
    # mapper noticed, so a missed offset shows up here instead of going
    # quiet on both sides at once.
    candidates = sweep_candidates(source_bodies) | set(addrs.seen)
    leaked = sweep(bytes(out), candidates)
    print("  %s -> %s" % (src.name, dst.name))
    print("  %d frame(s), %d byte(s) in, %d out"
          % (frames, len(raw), len(out)))
    print("  %d distinct address(es) replaced, %d SSID element(s) blanked"
          % (len(addrs.seen), ssids))
    print("  %d candidate(s) swept for, found by length rather than layout"
          % len(candidates))
    if dropped:
        print("  %d record(s) had an unparsable radiotap header and were "
              "zeroed" % dropped)
    if leaked:
        print("\n  REFUSED: %d original address(es) survive in the output:"
              % len(leaked))
        for m in leaked[:20]:
            print("    %s" % m.hex(":"))
        print("\n  Nothing written. Every leak so far came from a layout")
        print("  assumption in address_spans(); add the offset there.")
        return 1

    dst.write_bytes(bytes(out))
    print("  swept: no original address appears anywhere in the output")
    return 0


if __name__ == "__main__":
    sys.exit(main())
