#!/usr/bin/env python3
"""Generate the carrier-board netlist, placement and BOM.

Emits into dist/pcb/:

  netlist.txt        human-readable, net by net, for schematic capture
  pueo-carrier.net   KiCad legacy netlist (untested import, see below)
  placement.csv      module centres in KiCad board coordinates
  bom.csv            parts the design notes call for

Two deliberate limitations.

Pins are named functionally -- VCC, GND, SCK, CSN -- not numbered. Pin
numbers come from each module's datasheet and none of these modules have a
standard library footprint, so numbering them here would be inventing data.
The netlist gives the connections; the datasheets give the numbers.

The KiCad .net file has never been opened in KiCad, because KiCad is not
installed on the machine that wrote it. Treat netlist.txt as the deliverable
and the .net as a convenience that may need hand-fixing.

Every signal net is checked against ESP32-DIV/board_pueo.h before anything is
written, so the connections cannot silently drift from the firmware.
"""

import csv
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
HDR = REPO / "ESP32-DIV" / "board_pueo.h"
OUT = REPO / "dist" / "pcb"

# ── Board geometry ──────────────────────────────────────────────────────────
# The enclosure works in millimetres from the case centre with +Y toward the
# radios. KiCad works from a top-left origin with +Y downward. Board is the
# full base interior: 85 - 2*2.5 wide, 170 - 2*2.5 long.
BOARD_W, BOARD_L = 80.0, 165.0


def to_kicad(x, y):
    """Enclosure centre-origin (+Y up) -> KiCad top-left origin (+Y down)."""
    return round(x + BOARD_W / 2, 2), round(BOARD_L / 2 - y, 2)


# From pueo-enclosure.scad MODULES_ONBOARD[]: (name, refdes, x, y, w, h).
#
# The default build, which is the one with no charger inside the case. The
# EXT_CHARGER variant puts an MT3608 and a TP4056 down here as well; this
# netlist does not describe that board.
MODULES = [
    # The enclosure pocket is 20 x 12. The part is a Pololu S7V8F3 at
    # 11.4 x 16.5 x 3.0, so it fits only turned 90 degrees, because 16.5 will
    # not go across 12. Turned, it clears the pocket in both directions and
    # is smaller than the MP2307 this slot was cut for. At 3 mm it is also
    # the shortest thing in the case.
    ("3.3V buck-boost", "U3",   0.0, -60.0, 20, 12),
    # Not wired to this board at all: a cell here runs to the CYD's own BAT1.
    # It is in this list because the enclosure reserves the pocket and the
    # pour has to keep out from under it.
    ("LiPo pack",       "BT1",  0.0, -35.0, 45, 34),
    ("PN532 V3",        "J6",   0.0,   4.0, 43, 41),
    ("HW-863 CC1101",   "J4", -23.0,  48.0, 15, 40),
    ("NRF24 PA+LNA",    "J5",  22.0,  47.5, 16, 41),
    # ATGM336H, 16 x 13, antenna on a 90 mm u.FL pigtail rather than a patch
    # on the module. Centred in the corridor between the two radios.
    ("ATGM336H GPS",    "J7",   0.0,  40.0, 16, 13),
]

SMA_X = [-23.0, 22.0]       # scad SMA_X, 45 mm apart
# No USB opening in any of these walls in this build: the only socket is the
# CYD's own, and the CYD is in the lid.

# ── Nets ────────────────────────────────────────────────────────────────────
# (net, [(refdes, pin-name), ...], note)
#
# Three cables reach the CYD, because only four of the ten signals land on a
# connector it already has. J1 carries the six that have to be soldered, plus
# grounds. J2 goes straight to P3 and J3 straight to P1.
#
# J1, J2 and J3 pin numbers are ours to choose. J1 is fixed here and matches
# docs/pueo/pcb-design.md; J2 and J3 follow the CYD header's own order, so the
# cable is straight through with nothing crossed.
NETS = [
    ("GND", [("J1", "1"), ("J1", "5"), ("J1", "9"),
             ("J2", "1"), ("J3", "4"),
             ("J4", "GND"), ("J5", "GND"), ("J6", "GND"), ("J7", "GND"),
             ("U3", "GND"),
             ("C1", "2"), ("C2", "2"), ("C3", "2"), ("C4", "2"),
             ("C5", "2"), ("C6", "2"), ("C7", "2"),
             ("TP5", "1")],
     "one ground, poured both layers except under the PN532 coil. It arrives "
     "on three J1 pins and on both cable connectors"),

    # Power in. P1's 5V is the CYD's 5 V INPUT node, not an output: measured
    # 4.75 V at 92 mA with USB connected, and 0 V under the same load on
    # battery alone. The carrier taps it and makes its own 3.3 V.
    ("+5V_IN", [("J3", "1"), ("U3", "VIN"), ("C6", "1"), ("TP1", "1")],
     "from P1 pin 1, about 260 mA at full load, 0.5 mm"),

    # SHDN is active low and internally pulled up, so an unlanded pad is a
    # running converter. The test point is how the fourth pad exists without
    # the trace to VIN that would spend the provision below.
    ("RF_SHDN", [("U3", "SHDN"), ("TP3", "1")],
     "the fourth pad. Floating, so the rail is on; pull it to GND and the "
     "radios go dark. No GPIO reaches it today and none should be given one "
     "that sits low out of reset"),

    ("+3V3_RF", [("U3", "VOUT"), ("J4", "VCC"), ("J5", "VCC"), ("J6", "VCC"),
                 ("J7", "VCC"),
                 ("C1", "1"), ("C2", "1"), ("C3", "1"), ("C4", "1"),
                 ("C5", "1"), ("C7", "1"), ("TP2", "1")],
     "the one rail this board makes, and everything on it. Separate from the "
     "CYD's 3.3 V because the radios' spikes brown that regulator out"),

    # SPI, with the series-resistor break between the connector and the bus.
    ("SCK_J1",   [("J1", "2"), ("R2", "1")], "from CYD GPIO 18"),
    ("MOSI_J1",  [("J1", "3"), ("R3", "1")], "from CYD GPIO 23"),
    ("VSPI_SCK", [("R2", "2"), ("J4", "SCK"), ("J5", "SCK"), ("J6", "SCK"),
                  ("TP3", "1")],
     "daisy-chain along a spine, short stubs"),
    ("VSPI_MOSI", [("R3", "2"), ("J4", "MOSI"), ("J5", "MOSI"), ("J6", "MOSI")],
     "as above"),
    ("VSPI_MISO", [("J1", "4"), ("J4", "MISO"), ("J5", "MISO"), ("J6", "MISO"),
                   ("TP4", "1")],
     "no series resistor: an input at the CYD end"),

    # Chip selects and control. Static during a transaction, so these are the
    # nets to route awkwardly if something has to be.
    ("NRF_CSN",  [("J1", "6"), ("J5", "CSN")], "CYD GPIO 25, a module pad"),
    ("NRF_CE",   [("J1", "7"), ("J5", "CE")],  "CYD GPIO 16, an RGB LED pad"),
    ("PN532_SS", [("J1", "8"), ("J6", "SS")],  "CYD GPIO 17, an RGB LED pad"),

    # The CC1101's three lines are the only signals with a header at the CYD
    # end, so they leave on their own 4-way to P3 instead of through J1. J2's
    # order is P3's order: GND, IO35, IO22, IO21.
    ("CC1101_GDO2", [("J2", "2"), ("J4", "GDO2")], "CYD GPIO 35, input-only"),
    ("CC1101_GDO0", [("J2", "3"), ("J4", "GDO0")], "CYD GPIO 22"),
    ("CC1101_CS",   [("J2", "4"), ("J4", "CSN")],  "CYD GPIO 21"),

    # GPS, through the series resistor, onto P1's pin marked TX.
    ("GPS_TX_RAW", [("J7", "TX"), ("R1", "1")], "GPS module transmit"),
    ("GPS_TX",     [("R1", "2"), ("J3", "2"), ("TP6", "1")],
     "to CYD GPIO 1 through R1. The ESP32 drives that pin at boot and the GPS "
     "drives it always, so R1 limits the contention. GPS_UART_TX is -1, so "
     "nothing goes the other way"),
]

# Signal nets whose GPIO must agree with the firmware: net -> macro in the
# board header.
FIRMWARE_CHECK = {
    "CC1101_CS":   ("CC1101_CS", 21),
    "CC1101_GDO0": ("SUBGHZ_TX_PIN", 22),
    "CC1101_GDO2": ("SUBGHZ_RX_PIN", 35),
    "NRF_CSN":     ("CSN_PIN_1", 25),
    "NRF_CE":      ("CE_PIN_1", 16),
    "PN532_SS":    ("PN532_SS", 17),
    "GPS_TX":      ("GPS_UART_RX", 1),
}

BOM = [
    # J1 to J3 are the three cables to the lid. J4 to J7 are the module
    # headers on this board.
    ("J1",  1, "Connector 9-way 1.25mm MX1.25",
     "lid harness: the six signals that have to be soldered, plus grounds"),
    ("J2",  1, "Connector 4-way 1.25mm MX1.25",
     "straight to the CYD's P3, in P3's own order: GND, IO35, IO22, IO21"),
    ("J3",  1, "Connector 4-way 1.25mm MX1.25",
     "straight to the CYD's P1: 5V, TX, RX, GND. Pin 3 is unused, because "
     "GPS_UART_TX is -1 and nothing is sent to the GPS"),
    ("J4",  1, "Header 2x4 2.54mm", "HW-863 CC1101 module [verify pinout]"),
    ("J5",  1, "Header 2x4 2.54mm", "NRF24L01+PA+LNA module [verify pinout]"),
    ("J6",  1, "Header 1x6 2.54mm", "PN532 V3, SPI mode, DIP CH1=OFF CH2=ON"),
    ("J7",  1, "Header 1x5 2.54mm", "ATGM336H GPS, VCC GND TX RX PPS"),
    ("U3",  1, "Pololu S7V8F3 buck-boost, 11.4 x 16.5 x 3.0 mm",
     "3.3 V from P1's 5 V, 2.7 to 11.8 V in, 1 A stepping down. Fixed output: "
     "no trimpot to set wrong, which is why it replaced an adjustable module. "
     "FOUR pins at 0.1 inch, VOUT GND VIN SHDN read off the silkscreen. All "
     "four are netted now: SHDN goes to TP3 and nowhere else, which gives it "
     "a pad while leaving it floating. It is active low with a pull-up, so "
     "floating is enabled"),
    ("BT1", 1, "1S LiPo, optional",
     "NOT wired to this board. A cell here runs to the CYD's own BAT1, and "
     "the CYD will not start from it until SW1 is pressed"),
    ("R1",  1, "1k 0805", "GPS TX series, contention limit on GPIO 1"),
    ("R2",  1, "0R 0805", "SCK series; 22R footprint if it rings"),
    ("R3",  1, "0R 0805", "MOSI series; 22R footprint if it rings"),
    ("C1",  1, "10uF 0805 X7R", "AT THE NRF24 PINS, not near the regulator"),
    ("C2",  1, "100nF 0805", "CC1101 VCC"),
    ("C3",  1, "100nF 0805", "NRF24 VCC"),
    ("C4",  1, "100nF 0805", "PN532 VCC"),
    ("C5",  1, "100nF 0805", "GPS VCC"),
    ("C6",  1, "220uF electrolytic",
     "bulk at the converter input, where P1's 5 V arrives after a cable"),
    ("C7",  1, "100uF electrolytic", "+3V3_RF bulk at the converter output"),
    ("TP1", 1, "Test point", "+5V_IN"),
    ("TP2", 1, "Test point", "+3V3_RF"),
    ("TP3", 1, "Test point", "RF_SHDN, U3 pin 4"),
    ("TP3", 1, "Test point", "VSPI_SCK"),
    ("TP4", 1, "Test point", "VSPI_MISO"),
    ("TP5", 1, "Test point", "GND"),
    ("TP6", 1, "Test point", "GPS_TX"),
]


def read_firmware_pins():
    txt = HDR.read_text(encoding="utf-8", errors="replace")
    return {m.group(1): int(m.group(2))
            for m in re.finditer(r"^#define\s+(\w+)\s+(-?\d+)", txt, re.M)}


def verify(pins):
    """Refuse to emit anything that disagrees with the firmware."""
    problems = []
    for net, (macro, expect) in FIRMWARE_CHECK.items():
        got = pins.get(macro)
        if got != expect:
            problems.append("%s: board_pueo.h has %s=%s, netlist assumes %d"
                            % (net, macro, got, expect))
    # Every net must have at least two endpoints, or it is not a net.
    for name, conns, _ in NETS:
        if len(conns) < 2:
            problems.append("%s has only %d connection" % (name, len(conns)))
    # Every refdes used in a net must exist in the BOM.
    known = {r for r, *_ in BOM}
    for name, conns, _ in NETS:
        for ref, _pin in conns:
            if ref not in known:
                problems.append("%s references %s, which is not in the BOM" % (name, ref))
    return problems


def write_netlist_txt(path):
    lines = [
        "Pueo carrier board - netlist",
        "",
        "Pin names are functional, not numbered: module pin numbers come from",
        "the datasheets. J1, J2 and J3 numbers are ours; J1 matches",
        "docs/pueo/pcb-design.md and the two 4-ways follow the CYD header order.",
        "",
        "Generated by tools/gen_netlist.py from ESP32-DIV/board_pueo.h.",
        "",
    ]
    for name, conns, note in NETS:
        lines.append("%s" % name)
        if note:
            lines.append("    # %s" % note)
        for ref, pin in conns:
            lines.append("    %-5s %s" % (ref, pin))
        lines.append("")
    path.write_text("\n".join(lines), encoding="utf-8", newline="\n")


def write_kicad_net(path):
    """KiCad legacy netlist. Never opened in KiCad; see the module docstring."""
    out = ['(export (version D)', '  (design (source "tools/gen_netlist.py"))',
           '  (components']
    for ref, qty, part, note in BOM:
        out.append('    (comp (ref "%s") (value "%s"))' % (ref, part))
    out.append('  )')
    out.append('  (nets')
    for i, (name, conns, _n) in enumerate(NETS, 1):
        out.append('    (net (code "%d") (name "%s")' % (i, name))
        for ref, pin in conns:
            out.append('      (node (ref "%s") (pin "%s"))' % (ref, pin))
        out.append('    )')
    out.append('  )')
    out.append(')')
    path.write_text("\n".join(out), encoding="utf-8", newline="\n")


def write_placement(path):
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["refdes", "module", "kicad_x_mm", "kicad_y_mm",
                    "width_mm", "height_mm", "enclosure_x", "enclosure_y", "note"])
        for name, ref, x, y, wd, ht in MODULES:
            kx, ky = to_kicad(x, y)
            note = ""
            if ref == "J6":
                note = "KEEP-OUT: no copper or pour, 1.4 mm floor window under the coil"
            elif ref in ("J4", "J5"):
                sma = SMA_X[0] if ref == "J4" else SMA_X[1]
                note = "board SMA must land at enclosure x=%.0f (kicad x=%.2f)" % (
                    sma, to_kicad(sma, 0)[0])
            elif ref == "BT1":
                note = ("battery pocket, no copper beneath. Not a net on this "
                        "board: a cell here goes to the CYD's BAT1")
            w.writerow([ref, name, kx, ky, wd, ht, x, y, note])


def write_bom(path):
    with path.open("w", newline="", encoding="utf-8") as f:
        w = csv.writer(f)
        w.writerow(["refdes", "qty", "part", "note"])
        for row in BOM:
            w.writerow(row)


def main():
    """--check verifies and writes nothing, so the checks can run it.

    This file drifted two design revisions precisely because its guard only
    fired when somebody ran it by hand, and nothing ran it.
    """
    check_only = "--check" in sys.argv[1:]
    pins = read_firmware_pins()
    problems = verify(pins)
    if problems:
        for p in problems:
            print("  ! " + p, file=sys.stderr)
        print("netlist disagrees with the firmware; nothing written", file=sys.stderr)
        return 1

    if check_only:
        print("  netlist agrees with board_pueo.h: %d nets, %d parts"
              % (len(NETS), len(BOM)))
        return 0

    OUT.mkdir(parents=True, exist_ok=True)
    write_netlist_txt(OUT / "netlist.txt")
    write_kicad_net(OUT / "pueo-carrier.net")
    write_placement(OUT / "placement.csv")
    write_bom(OUT / "bom.csv")

    nets = len(NETS)
    conns = sum(len(c) for _n, c, _x in NETS)
    print("  %d nets, %d connections, %d parts" % (nets, conns, len(BOM)))
    print("  all %d firmware-checked signals agree with board_pueo.h"
          % len(FIRMWARE_CHECK))
    print("  board %.0f x %.0f mm, origin top-left" % (BOARD_W, BOARD_L))
    for f in sorted(OUT.iterdir()):
        print("    dist/pcb/%s" % f.name)
    return 0


if __name__ == "__main__":
    sys.exit(main())
