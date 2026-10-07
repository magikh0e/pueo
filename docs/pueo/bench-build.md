# Bench build: testing modules without the carrier board

[build-guide.md](build-guide.md) builds the finished thing: modules soldered
to the CYD's castellations, everything in the enclosure, a battery.
[pcb-design.md](pcb-design.md) describes the carrier board that would replace
that harness, which is input for a KiCad project and not a layout. There is
no fabricated board.

This is the third thing, and the one to do first: get each module answering
on a bench, with as few permanent changes as possible, before committing to
the six solder joints that are hard to undo.

**The point of building in this order** is that the SPI bus is shared. A bad
joint on SCK, MOSI or MISO breaks every device at once, and the symptom is
indistinguishable from a dead module. Proving a module works while it is
still unsoldered means that when something breaks later you know which half
changed.

## What the connectors can and cannot reach

Ten signals. Only four reach a CYD connector, and the rest are pads:

| signal | GPIO | where | needs solder |
|---|---|---|---|
| CC1101 CS | 21 | `P3` pin 4 | no |
| CC1101 GDO0 | 22 | `P3` pin 3 | no |
| CC1101 GDO2 | 35 | `P3` pin 2 | no |
| GPS TX → ESP32 | 1 | `P1` pin 2 | no |
| VSPI SCK | 18 | module pad 9R | yes, or tap the card slot |
| VSPI MOSI | 23 | module pad 2R | yes, or tap the card slot |
| VSPI MISO | 19 | module pad 8R | yes, or tap the card slot |
| NRF24 CSN | 25 | module pad 10L | yes |
| NRF24 CE | 16 | module pad 12R | yes |
| PN532 SS | 17 | module pad 11R | yes |

That table is the whole constraint. It is why the stages below are in the
order they are: each one adds the cheapest thing that is still provable.

`CN1` is not in it. It carries `GND IO22 IO21 3.3V`, duplicating two of P3's
signals and adding a 3.3 V tap, so it is useful for powering something small
and for nothing else here. It is also a 4-pin 1.25 mm connector sitting near
P3, so it is the one you can plug the CC1101 cable into by mistake: that puts
chip select on the 3.3 V rail and leaves GDO2 unconnected, and the symptom is
the "No CC1101" screen rather than anything that looks like a wiring fault.

## Cables for a bench build

**Buy JST GH, 4-way, pre-crimped. Not PicoBlade, not MX1.25.**

Pitch is not the whole part number, and this is the specific way this build
wastes an order. The CYD's headers are 1.25 mm, and so is Molex PicoBlade,
and the two do not mate: GH latches against the side of the shroud,
PicoBlade on top. Listings aimed at drone builders say "PicoBlade" and "for
Pixhawk" in the same title, and those are different parts.

Settled by fit on 2026-10-06: a PicoBlade housing offered to the header does
not seat, with the pitch correct. [hardware.md](hardware.md) said the
opposite until then, having named the series off a pitch measurement, which
does not determine it. 4-way is a stocked GH size, so both cables here are
ordinary parts.

**28 AWG.** GH contacts take roughly 32 to 28 and are rated 1 A each, so 28
is at once the thickest the contact accepts, the gauge pre-crimped GH comes
as, and well inside the rating. The contact is the limit here, not the
copper: the worst case anywhere in this design is 256 mA, the buck-boost
converter flat out on the 5 V tap, and the signal cable carries almost
nothing.

Keep the leads **short**, which on a bench is free. At 256 mA the P1 pin
sits at 3.99 V rather than 5, and a long thin cable is a real share of that
drop. The converter is specified from 2.7 V in so 4 V is comfortable, but
there is no reason to spend the margin on wire.

Both cables are **GH at the CYD end only**. The other end goes to a 2.54 mm
pin header on a module, so it wants individual female Dupont sockets, not a
second housing. A 4-way housing at the module end would be wrong even if the
pitch matched, because the signals are not adjacent on a 2x4.

**Cable A, P3 to the CC1101.** Four ways:

| P3 pin | wire | CC1101 module |
|---|---|---|
| 1 | GND | GND |
| 2 | IO35 | GDO2 |
| 3 | IO22 | GDO0 |
| 4 | IO21 | CSN |

**Cable B, P1 for power and GPS.** Four ways, `5V TX RX GND` with 5 V nearest
the corner mounting hole:

| P1 pin | wire | goes to |
|---|---|---|
| 1 | 5V | the bench supply, or leave it and power by USB-C |
| 2 | TX | GPS module TX, through a 1 kΩ series resistor |
| 3 | RX | **nothing**, pull this contact out of the housing |
| 4 | GND | common ground |

Pin 3 is unused because `GPS_UART_TX` is -1 and nothing is ever sent to the
GPS. Leaving a wire there hangs a stub off UART0's receive line, which is the
console, so remove the contact rather than leaving it floating.

**[verify] the CC1101 module's own silkscreen.** The HW-863 breakout's 2x4
pinout has not been confirmed against a part in hand, and it is marked
`[verify pinout]` in the BOM for that reason. The names on P3 are solid. The
names at the other end of cable A are the unverified half, so read them off
the module before you trust the order.

## Stage 0: the bare board, no cables

Flash and boot with nothing attached.

```bash
esptool.py --chip esp32 -b 921600 write_flash 0x0 pueo-0.4.42-merged.bin
```

Display, backlight, touch, the menus and the SD card all work before you have
introduced a single connection of your own. The WiFi features are fully
testable here, because the ESP32's radio is on the die: the scanner, the
packet monitor, Surveillance and Hunt need no module at all.

This is also where you find out that the panel is the one the firmware is
built for. "Cheap yellow display" names at least four boards and a render of
one is not a description of another.

## Stage 1: GPS, which needs no soldering at all

One cable, no joints, and the module either reports satellites or it does
not. Power the GPS from a bench 3.3 V or from `CN1`'s 3.3 V pin, ground it
common, and run its TX into `P1` pin 2 through the 1 kΩ resistor.

**The resistor is not optional.** GPIO 1 is UART0's transmit pin. The
firmware releases it before the GPS feature reads on it, but the console owns
it the rest of the time, so for most of the board's life the ESP32 and the
GPS are both driving that net. The resistor is what makes that survivable.

Expect no fix indoors. A cold start by a window can take several minutes, and
the thing to look for first is the module's own PPS LED, not a position.

## Stage 2: the CC1101, with the bus tapped rather than soldered

The radio needs the three SPI lines, and those are pads. There is one way to
reach them without an iron: a **passive microSD extender** in the card slot
breaks out CLK, DI and DO on the card's own contacts, which are GPIO 18, 23
and 19.

This is a bench trick and not a build step. Two conditions on it:

- **The card slot is occupied while you do this**, so anything that writes to
  the card is off the table: Spotter's capture log, the wardriver, the pcap
  writer, and `.sub` import and export. Test the radio here and the card
  separately.
- **It is a stub on a shared bus.** Keep it short. At the clock the CC1101
  runs this is fine; it is not a way to run the whole build.

Do not *solder* to the card slot. Earlier versions of the build guide sent
people there for SCK, MOSI and MISO, and the slot still has to work
afterwards.

With cable A on P3 and the bus tapped, these should work:

- **SubGHz > Freq Analyser.** The fastest confirmation that the radio is
  alive, because it needs only tune-and-read. Press a key fob nearby and its
  frequency should take the bright bar.
- **Saved Profile**, which opens without a radio and sends with one.
- **Replay Attack** and the jammer, which transmit, so mind what is nearby.

If the analyser shows every row at the floor with a remote being pressed in
front of it, suspect cable A's pin 2 and pin 3 before suspecting the module:
IO35 and IO22 swapped gives a radio that is detected and never reports
anything, because GPIO 35 is input-only and correct only for GDO2.

## Stage 3: NRF24 and PN532, which need the iron

There is no connector route to `NRF24 CSN` (25), `NRF24 CE` (16) or
`PN532_SS` (17). Three wires onto pads, plus the shared bus, which by this
point you may as well solder properly rather than extend the card-slot trick.

At that point you are doing the build in [build-guide.md](build-guide.md),
and its sections on flux, chisel tips and taping the wire down are the
relevant ones. Read "Soldering to a castellation" before the first joint
rather than after the third.

## What this does not prove

Everything here is a bench rig: short leads, no enclosure, bench power.
Carried over to the finished build, three things change and each has bitten
somebody:

- **Lead length on a shared bus.** Short jumpers on a desk are a different
  electrical problem from a harness between a lid and a base.
- **Power under load.** The CYD's own regulator is already carrying an ESP32,
  a backlight and a display controller. Sharing it with the radios browns out
  the main rail, and a brownout on an ESP32 looks like a firmware bug.
- **Flex life.** Nothing here moves. A lid that opens and closes is the
  reason the connector question in pcb-design.md was argued at all.
