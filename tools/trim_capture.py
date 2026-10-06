#!/usr/bin/env python3
"""Cut a capture down to the frames a fixture needs, keeping them verbatim.

A fixture is read on every release, so a capture of a few minutes is mostly
weight. editcap does this and is not always installed; this does the one
thing needed here and nothing else.

    python tools/trim_capture.py in.pcap out.pcap --eapol --around 40
    python tools/trim_capture.py in.pcap out.pcap --first 200

What it keeps
-------------
  --eapol         Every frame carrying an 802.1X payload, found the way
                  Eapol.cpp finds it rather than by a fixed offset.

  --around N      N frames either side of each kept frame, so the
                  association and the beacons that give the handshake its
                  context come too. A handshake with nothing around it
                  tests the parser; a handshake in traffic tests the parser
                  in the place it runs.

  --first N       The first N frames, for a fixture about frame mix rather
                  than about a handshake.

  --keep-short    Frames under 24 bytes are kept when they fall in range
                  like anything else. This says so explicitly because they
                  are the ones an earlier version of the anonymiser walked
                  straight past, and a trim that quietly dropped them would
                  hide that class of bug rather than test for it.

Records are copied byte for byte with their original timestamps. Nothing is
rewritten, so run this before anonymise_capture.py or after it; the order
does not matter and after is cheaper.
"""
import struct
import sys
from pathlib import Path

PCAP_MAGICS = {
    b"\xd4\xc3\xb2\xa1": "<", b"\xa1\xb2\xc3\xd4": ">",
    b"\x4d\x3c\xb2\xa1": "<", b"\xa1\xb2\x3c\x4d": ">",
}
SNAP = bytes([0xAA, 0xAA, 0x03, 0x00, 0x00, 0x00])
TYPE_DATA = 0x02
TO_DS, FROM_DS, ORDER, PROTECTED = 0x01, 0x02, 0x80, 0x40
SUBTYPE_QOS = 0x08


def header_length(b):
    if len(b) < 2:
        return None
    fc0, fc1 = b[0], b[1]
    if ((fc0 >> 2) & 0x03) != TYPE_DATA:
        return None
    n = 24
    if (fc1 & TO_DS) and (fc1 & FROM_DS):
        n += 6
    if ((fc0 >> 4) & 0x0F) & SUBTYPE_QOS:
        n += 2
        if fc1 & ORDER:
            n += 4
    return n


def has_eapol(body):
    hdr = header_length(body)
    if hdr is None or (body[1] & PROTECTED):
        return False
    if hdr + 8 > len(body):
        return False
    return body[hdr:hdr + 6] == SNAP and body[hdr + 6:hdr + 8] == b"\x88\x8e"


def main():
    args = sys.argv[1:]
    if len(args) < 2:
        print(__doc__)
        return 2
    src, dst = Path(args[0]), Path(args[1])
    want_eapol = "--eapol" in args
    around = 0
    first = None
    if "--around" in args:
        around = int(args[args.index("--around") + 1])
    if "--first" in args:
        first = int(args[args.index("--first") + 1])
    if not want_eapol and first is None:
        print("nothing selected: pass --eapol or --first")
        return 2

    raw = src.read_bytes()
    if raw[:4] not in PCAP_MAGICS:
        print("not a classic pcap: %s" % raw[:4].hex())
        return 1
    end = PCAP_MAGICS[raw[:4]]
    link = struct.unpack(end + "I", raw[20:24])[0]

    records, i = [], 24
    while i + 16 <= len(raw):
        head = raw[i:i + 16]
        _ts, _us, caplen, _o = struct.unpack(end + "IIII", head)
        i += 16
        records.append((head, raw[i:i + caplen]))
        i += caplen

    keep = set()
    hits = 0
    for n, (_h, rec) in enumerate(records):
        if first is not None and n < first:
            keep.add(n)
        if not want_eapol:
            continue
        body = rec
        if link == 127:
            if len(rec) < 4:
                continue
            rtlen = struct.unpack("<H", rec[2:4])[0]
            if rtlen < 8 or rtlen > len(rec):
                continue
            body = rec[rtlen:]
        if has_eapol(body):
            hits += 1
            for k in range(max(0, n - around), min(len(records), n + around + 1)):
                keep.add(k)

    out = bytearray(raw[:24])
    for n in sorted(keep):
        head, rec = records[n]
        out += head + rec
    dst.write_bytes(bytes(out))

    print("  %s -> %s" % (src.name, dst.name))
    print("  %d frame(s) in, %d kept, %d byte(s) -> %d"
          % (len(records), len(keep), len(raw), len(out)))
    if want_eapol:
        print("  %d frame(s) carried 802.1X, %d either side kept"
              % (hits, around))
    if want_eapol and hits == 0:
        print("\n  no 802.1X in this capture, so the output is whatever")
        print("  --first selected. A four-way exchange is over in tens of")
        print("  milliseconds: Packet Monitor has to be parked on the")
        print("  channel the association happens on.")
    return 0


if __name__ == "__main__":
    sys.exit(main())
