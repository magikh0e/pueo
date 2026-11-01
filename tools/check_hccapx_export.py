#!/usr/bin/env python3
"""The handshake exporter is off by default, fenced, and feeds risinek's
parser only frames Pueo has already bounded.

The export writes a captured WPA 4-way handshake to the SD card as a hashcat
.hccapx. The assembly is the vendored risinek serializer under third_party/.
Its own EAPOL parser reads fixed offsets with no length guard, which is the
exact trap docs/pueo/eapol-capture.md was written to avoid, so it must never
see a frame Pueo has not first checked. This holds that line in source.

It also holds the other promise made when the serializer was vendored: with
the build flag off, the feature compiles to nothing, so the shipped firmware
does not carry it.

    python tools/check_hccapx_export.py

Reads source; needs no board.

What it asserts
---------------
  default off     FeatureFlags.h leaves PUEO_HANDSHAKE_EXPORT at 0, behind an
                  #ifndef so -D can override it.
  call sites      Both hooks in wifi.cpp, offer() in the callback and pump()
                  on the task side, sit inside #if PUEO_HANDSHAKE_EXPORT. An
                  unguarded call would reference symbols a flag-off build does
                  not define.
  implementation  HandshakeExport.cpp and the two vendor shims wrap their whole
                  bodies in the flag, so a flag-off build links empty units.
  the gate        offer() forwards a frame only when Eapol puts its header at
                  24 or 26 bytes and finds an EAPOL payload within it. That is
                  the precondition risinek's unguarded parser needs. Drop this
                  and the vendored over-read is back.
  the salt        tryWrite() will not save a record without an SSID, because
                  the SSID is what the hash is cracked against. A record with
                  the wrong one is uncrackable.
  driver excluded No shim pulls in frame_analyzer.c, the promiscuous-capture
                  driver. Pueo captures its own frames; taking that too would
                  fight its capture path.
  record size     The vendored hccapx_t is the 393-byte hashcat v4 record. A
                  different size is a malformed file.
"""
import re
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FLAGS = ROOT / "ESP32-DIV" / "FeatureFlags.h"
WIFI = ROOT / "ESP32-DIV" / "wifi.cpp"
BRIDGE = ROOT / "ESP32-DIV" / "HandshakeExport.cpp"
SHIM_H = ROOT / "ESP32-DIV" / "HccapxVendor.c"
SHIM_F = ROOT / "ESP32-DIV" / "FrameAnalyzerVendor.c"
HCCAPX_H = ROOT / "third_party" / "esp32-wifi-penetration-tool" / "hccapx_serializer.h"

FLAG = "PUEO_HANDSHAKE_EXPORT"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        FAILED.append(name)
        print("  FAIL  %-46s %s" % (name, detail))


def read(p):
    return p.read_text(encoding="utf-8", errors="replace")


def guarded(src, needle):
    """True when `needle` sits inside an #if PUEO_HANDSHAKE_EXPORT ... #endif:
    the nearest preprocessor conditional opened before it is ours, with no
    #endif in between, and an #endif closes it afterwards."""
    i = src.find(needle)
    if i < 0:
        return False
    before = src[:i]
    opens = [m for m in re.finditer(r"^\s*#\s*if\w*\b.*$", before, re.M)]
    ends = [m for m in re.finditer(r"^\s*#\s*endif\b.*$", before, re.M)]
    if not opens:
        return False
    # The conditional governing `needle` is the last #if with no matching
    # #endif before `needle`. Count depth.
    depth = 0
    governing = None
    for m in re.finditer(r"^\s*#\s*(if\w*|endif)\b(.*)$", before, re.M):
        if m.group(1).startswith("if"):
            depth += 1
            governing = m
        else:
            depth -= 1
    if depth <= 0 or governing is None:
        return False
    if FLAG not in governing.group(0):
        return False
    return "#endif" in src[i:] or re.search(r"#\s*endif", src[i:]) is not None


# ── hccapx_t size, parsed from the vendored header ───────────────────────────
SCALAR = {
    "uint8_t": 1, "int8_t": 1, "char": 1,
    "uint16_t": 2, "int16_t": 2,
    "uint32_t": 4, "int32_t": 4,
    "uint64_t": 8, "int64_t": 8,
}


def hccapx_size(src):
    m = re.search(r"typedef struct[^{]*\{(.*?)\}\s*hccapx_t\s*;", src, re.S)
    if not m:
        return None, "no hccapx_t typedef"
    total = 0
    for line in m.group(1).splitlines():
        line = line.split("//")[0].strip().rstrip(";").strip()
        if not line:
            continue
        fm = re.match(r"(\w+)\s+(\w+)(?:\[(\d+)\])?$", line)
        if not fm:
            return None, "unparsed field: %r" % line
        typ, _name, arr = fm.group(1), fm.group(2), fm.group(3)
        if typ not in SCALAR:
            return None, "unknown type: %s" % typ
        total += SCALAR[typ] * (int(arr) if arr else 1)
    return total, ""


def main():
    flags = read(FLAGS)
    wifi = read(WIFI)
    bridge = read(BRIDGE)
    shim_h = read(SHIM_H)
    shim_f = read(SHIM_F)
    hdr = read(HCCAPX_H)

    # default off
    ok("flag is defined behind #ifndef",
       re.search(r"#ifndef\s+" + FLAG, flags) is not None)
    m = re.search(r"#define\s+" + FLAG + r"\s+(\d+)", flags)
    ok("flag defaults to 0 (shipped firmware does not carry it)",
       m is not None and m.group(1) == "0",
       "default is %s" % (m.group(1) if m else "absent"))

    # call sites guarded
    ok("wifi.cpp calls HandshakeExport::offer", "HandshakeExport::offer(" in wifi)
    ok("  offer() is behind the flag",
       guarded(wifi, "HandshakeExport::offer("))
    ok("wifi.cpp calls HandshakeExport::pump", "HandshakeExport::pump(" in wifi)
    ok("  pump() is behind the flag",
       guarded(wifi, "HandshakeExport::pump("))

    # implementation guarded
    ok("bridge defines offer()", "void offer(" in bridge)
    ok("  offer() body is behind the flag", guarded(bridge, "void offer("))
    ok("bridge defines pump()", "void pump(" in bridge)
    ok("  pump() body is behind the flag", guarded(bridge, "void pump("))
    ok("hccapx shim is behind the flag",
       guarded(shim_h, 'include "hccapx_serializer.c"'))
    ok("parser shim is behind the flag",
       guarded(shim_f, 'include "frame_analyzer_parser.c"'))

    # the gate: offer forwards only layouts Eapol has bounded
    off = bridge[bridge.find("void offer("):]
    off = off[:off.find("\n}\n") + 2] if "\n}\n" in off else off
    ok("offer() rejects header lengths other than 24 or 26",
       re.search(r"hdr\s*!=\s*24\s*&&\s*hdr\s*!=\s*26", off) is not None)
    ok("offer() requires a bounded EAPOL payload (findPayload)",
       "findPayload(" in off and "< 0" in off)

    # the salt: no SSID, no write
    tw = bridge[bridge.find("void tryWrite("):]
    tw = tw[:tw.find("\n}\n") + 2] if "\n}\n" in tw else tw
    ok("tryWrite() looks up the SSID", "lookupSsid(" in tw)
    ok("tryWrite() bails without an SSID", re.search(r"sl\s*<\s*0", tw) is not None)

    # the capture driver stays out (an #include of it, not a mention in prose)
    drv = re.compile(r'#\s*include\s+"frame_analyzer\.c"')
    ok("no shim includes frame_analyzer.c",
       drv.search(shim_h) is None and drv.search(shim_f) is None)

    # record size
    size, why = hccapx_size(hdr)
    ok("vendored hccapx_t is the 393-byte v4 record", size == 393,
       why or ("got %s" % size))

    print()
    if FAILED:
        print("FAILED (%d/%d): %s" % (len(FAILED), CHECKS, ", ".join(FAILED)))
        return 1
    print("all %d checks passed" % CHECKS)
    return 0


if __name__ == "__main__":
    sys.exit(main())
