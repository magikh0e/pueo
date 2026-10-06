"""Decode /pueo/subghz/profiles_current.bin off the SD card.

storeProfile() syncs the whole EEPROM profile table to the card after every
write, so this file is the device's own view of what it stored. Reading it
back is the only way to check an imported profile on a board with no CC1101
attached, because Saved Profile gates itself on the radio before it will
list anything.

Layout, from subghz.cpp. Header is packed:

    uint32 magic = 0x315A4753 ("GSZ1" little endian)
    uint16 version
    uint16 count
    uint16 profileSize
    uint16 reserved

then `count` packed records of `profileSize` bytes:

    uint32 frequency, uint32 value, uint16 bitLength, uint16 protocol,
    char name[16]

Usage:  python tools/decode_profiles.py <path to profiles_current.bin>
"""
import io
import struct
import sys

MAGIC = 0x315A4753
HDR = "<IHHHH"
REC = "<IIHH16s"
HDR_SIZE = struct.calcsize(HDR)
REC_SIZE = struct.calcsize(REC)

# Mirrors kFreqMinHz, kFreqMaxHz and the bit test in SubFile.cpp. A record
# outside these could not have come from a .sub file the parser accepted.
FREQ_MIN = 280000000
FREQ_MAX = 960000000
MAX_BITS = 32


def main(path):
    raw = io.open(path, "rb").read()
    print("  file    %s" % path)
    print("  bytes   %d" % len(raw))

    if len(raw) < HDR_SIZE:
        print("  !!  too short to hold a header")
        return 1

    magic, version, count, profile_size, reserved = struct.unpack(
        HDR, raw[:HDR_SIZE])

    print("  magic   0x%08X %s" % (magic, "ok" if magic == MAGIC else "WRONG"))
    print("  version %d" % version)
    print("  count   %d" % count)
    print("  recsize %d %s"
          % (profile_size,
             "ok" if profile_size == REC_SIZE else
             "WRONG, this decoder reads %d" % REC_SIZE))
    print("  resv    %d" % reserved)

    if magic != MAGIC:
        print("  !!  not a Pueo profile export")
        return 1
    if profile_size != REC_SIZE:
        print("  !!  record size disagrees, refusing to guess")
        return 1

    want = HDR_SIZE + count * REC_SIZE
    if len(raw) < want:
        print("  !!  header claims %d records, file holds %d bytes short"
              % (count, want - len(raw)))
        return 1
    if len(raw) > want:
        print("  ..  %d trailing bytes ignored" % (len(raw) - want))

    print()
    bad = 0
    for i in range(count):
        off = HDR_SIZE + i * REC_SIZE
        freq, value, bits, proto, name = struct.unpack(
            REC, raw[off:off + REC_SIZE])

        # The name is a fixed 16-byte field, NUL padded.
        shown = name.split(b"\0", 1)[0].decode("utf-8", "replace")

        print("  slot %d" % i)
        print("    name       %r" % shown)
        print("    frequency  %d Hz  (%.3f MHz)" % (freq, freq / 1e6))
        print("    value      0x%X  (%d)" % (value, value))
        print("    bitLength  %d" % bits)
        print("    protocol   %d" % proto)

        # Sanity, in the same spirit as the parser's own range checks.
        notes = []
        if not (FREQ_MIN <= freq <= FREQ_MAX):
            notes.append("frequency outside %d..%d, which SubFile.cpp refuses"
                         % (FREQ_MIN, FREQ_MAX))
        if bits == 0 or bits > MAX_BITS:
            notes.append("bit length outside 1..%d, and value is a uint32"
                         % MAX_BITS)
        if 0 < bits <= MAX_BITS and value >= (1 << bits):
            notes.append("value does not fit in bitLength bits")
        for n in notes:
            print("    !!  %s" % n)
            bad += 1
        print()

    if bad:
        print("  !!  %d record problem(s)" % bad)
        return 1
    print("  ok  %d record(s) decoded, all within range" % count)
    return 0


if __name__ == "__main__":
    if len(sys.argv) != 2:
        print(__doc__)
        raise SystemExit(2)
    raise SystemExit(main(sys.argv[1]))
