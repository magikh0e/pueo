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

## Parts

Everything the stages below ask for. Nothing here is specific to this
project except the CYD itself, and nothing is expensive.

### Boards

| part | notes |
|---|---|
| Sunton **ESP32-3248S035R**, 3.5" | the CYD. Resistive touch, ST7796, USB-C. The 2.8" is a different board and is not supported |
| **CC1101** 433 MHz module | the HW-863 style 2x4 breakout. `[verify]` its silkscreen before wiring: the pinout here is not confirmed against a part in hand |
| **nRF24L01+ PA/LNA** | the module with the external antenna, not the bare one |
| **PN532** NFC, Elechouse V3 | the board with the DIP switches. Set them for **SPI** |
| **ATGM336H** GPS | with its 20 x 6 mm active antenna on a 90 mm u.FL pigtail |

### Cables and wire

Gauge is not a detail here. Two different jobs want two different wires,
and the reason is the terminal at each end rather than the current.

| for | wire | why |
|---|---|---|
| the two CYD cables | **28 AWG**, pre-crimped **JST GH 1.25 mm**, 4-way | the thickest a GH contact takes, and the gauge pre-crimped GH is sold as. Not PicoBlade, not MX1.25: see below |
| module ends of those cables | individual **female Dupont** sockets, 2.54 mm | the signals are not adjacent on a 2x4, so a second housing would be wrong even at the right pitch |
| the six soldered joints | **30 AWG** silicone-insulated stranded, or enamelled | 28 is too stiff for a castellation and levers the pad; silicone insulation survives an iron near it, PVC does not |
| bench power, if used | whatever reaches, **22 AWG** or thicker | short and fat costs nothing on a desk and keeps the rail up |

Keep every lead **short**. At 256 mA the P1 pin already sits at 3.99 V
rather than 5, and thin wire is a real share of that.

### Tools and consumables

| item | notes |
|---|---|
| iron with a **1.0 to 1.6 mm chisel tip** | a needle point is the wrong tool for a castellation, see build-guide.md |
| **gel flux**, no-clean | not optional. The rosin core in the solder is not enough |
| thin solder, **leaded 0.5 mm** if you have it | these pads were reflowed with something; leaded is easier to work with |
| **microSD breakout or sniffer** | for Stage 2, see below. Buy the part rather than improvising |
| kapton tape | strain relief on the castellation joints |
| a multimeter | for continuity before power, and for setting any converter |

### The connector trap, which costs an order

**Buy JST GH. Not PicoBlade, not MX1.25.** Pitch is not the whole part
number. The CYD's headers are 1.25 mm and so is Molex PicoBlade, and the
two do not mate: GH latches against the side of the shroud, PicoBlade on
top. Listings aimed at drone builders say "PicoBlade" and "for Pixhawk" in
the same title and those are different parts. Settled by fit on 2026-10-06,
after [hardware.md](hardware.md) had said the opposite for a while, having
named the series off a pitch measurement, which does not determine it.

### Check the module you were actually sent

These are cheap boards from whoever had stock, and revisions vary. Before
connecting anything, read the module's own silkscreen and confirm it wants
3.3 V. What this build assumes:

| part | supply | logic |
|---|---|---|
| ATGM336H GPS | 2.7 to **3.6 V max** | 3.3 V |
| PN532 V3 | 3.3 V to 5 V | **SPI on that board is 3.3 V TTL** |
| CC1101 | 3.3 V | 3.3 V |
| NRF24 PA/LNA | 3.3 V | 3.3 V |

The PN532 is the one that invites a mistake, because it carries a level
shifter and people assume it covers everything. It does not: that shifter
is there for I2C and UART, which are 5 V tolerant, and SPI is the mode this
build uses. Nothing here needs level translation, and anything that appears
to is a sign the module is not the one in this table.

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

## Stage 0: the meter, before anything is connected

The only step here that prevents damage rather than detecting it, which is
why it goes first. It takes a minute.

- **Set your rails and measure them, unloaded.** Anything adjustable ships
  at an arbitrary setting, and a boost module will happily deliver 28 V
  into a part expecting 3.3. Measure before a module is on the end of it,
  not after.
- **Confirm a common ground** between whatever is supplying the modules and
  the CYD. Two supplies with no ground in common is the other way to lose
  a module.
- **If a cell is involved, beep BAT1 for polarity before plugging it in.**
  Use a known ground such as `CN1` pin 1. BAT1 is covered at length in
  [hardware.md](hardware.md) and is the one connector here that punishes a
  guess.

Nothing is attached yet, so nothing is at risk while you do this.

## Stage 1: the bare board, no cables

Flash and boot with nothing attached.

```bash
esptool.py --chip esp32 -b 921600 write_flash 0x0 pueo-0.4.47-merged.bin
```

Display, backlight, touch, the menus and the SD card all work before you have
introduced a single connection of your own. The WiFi features are fully
testable here, because the ESP32's radio is on the die: the scanner, the
packet monitor, Surveillance and Hunt need no module at all.

This is also where you find out that the panel is the one the firmware is
built for. "Cheap yellow display" names at least four boards and a render of
one is not a description of another.

## Stage 2: GPS, which needs no soldering at all

One cable, no joints, and the module either reports satellites or it does
not. Power the GPS from a bench 3.3 V or from `CN1`'s 3.3 V pin, ground it
common, and run its TX into `P1` pin 2 through the 1 kΩ resistor.

**The resistor is not optional.** GPIO 1 is UART0's transmit pin. The
firmware releases it before the GPS feature reads on it, but the console owns
it the rest of the time, so for most of the board's life the ESP32 and the
GPS are both driving that net. The resistor is what makes that survivable.

Expect no fix indoors. A cold start by a window can take several minutes, and
the thing to look for first is the module's own PPS LED, not a position.

## Stage 3: the CC1101, with the bus tapped rather than soldered

The radio needs the three SPI lines, and those are pads. There is one way to
reach them without an iron: put something in the card slot that brings the
card's own contacts out where a jumper can reach them. CLK, DI and DO on
those contacts are GPIO 18, 23 and 19.

**Buy the part rather than improvising one.** Two kinds work and they are
not the same thing:

| part | what it gives you | notes |
|---|---|---|
| **microSD to DIP breakout** | the contacts on 2.54 mm pins | the easy one. Female Dupont jumpers push straight on, and there is nothing to solder |
| **microSD "sniffer" / interposer** | a card-shaped PCB with a pad or pin row | made for logic-analyser work. Also fine, and thinner |

What is miserable is the obvious-looking third option, a **flexible
extender cable**, where the contacts arrive as a naked ribbon with no
terminal on them. Soldering jumpers to that ribbon is fiddly, the pitch is
under a millimetre, and adjacent shorts are easy and not obvious. The whole
point of this stage is avoiding the iron, so do not start it by soldering
something harder than the joints you were trying to defer.

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

## Stage 4: NRF24 and PN532, which need the iron

There is no connector route to `NRF24 CSN` (25), `NRF24 CE` (16) or
`PN532_SS` (17). Three wires onto pads, plus the shared bus, which by this
point you may as well solder properly rather than extend the card-slot trick.

**Solder the three bus wires first and let a microSD card grade them.** SCK,
MOSI and MISO are the joints whose failure is hardest to attribute, because
a bad one takes out every device at once and looks like a dead module. The
card uses exactly those three lines, so putting one in and seeing it mount
tests all three at speed, which no meter reading does. It also tells you
that you have not bridged anything onto `SD_CS`, which is GPIO 5.

Do that before the NRF24 and the PN532 go on. If the card mounts, the bus
is sound and anything that fails afterwards is the module or its own chip
select, which is a much smaller thing to look for.

Of the three remaining pads, **only two are RGB LED channels**: CE is the
blue one and `PN532_SS` is the green. `NRF24 CSN` is GPIO 25 and has to be
found on the silkscreen. Soldering CSN to the red channel puts it on GPIO 4
and the radio never answers.

At that point you are doing the build in [build-guide.md](build-guide.md).
Read [**Soldering to a
castellation**](build-guide.md#soldering-to-a-castellation) before the first
joint rather than after the third. It is four things, and each is there
because skipping it costs a pad:

- **Gel flux on the pad before the iron.** Not optional and not the same as
  the rosin core in the solder.
- **A 1.0 to 1.6 mm chisel tip**, not a needle point.
- **Use the solder already on the castellation** rather than adding a blob.
- **Tape the wire down** before soldering it. 30 AWG on a castellation has
  no mechanical strength of its own and the joint fails later, not now.

## What this does not prove

Everything here is a bench rig: short leads, no enclosure, bench power.
Carried over to the finished build, three things change and each has bitten
somebody:

- **Lead length on a shared bus.** Short jumpers on a desk are a different
  electrical problem from a harness between a lid and a base.
- **Power under load, and what to do about it.** The CYD's own regulator is
  already carrying an ESP32, a backlight and a display controller. Sharing
  it with the radios browns out the main rail, and a brownout on an ESP32
  looks like a firmware bug: random resets, a radio that fails to init, a
  capture that stops.

  The fix is not a bigger capacitor. **The radios get their own 3.3 V rail
  and never touch the display's**, which has been the rule in
  [pcb-design.md](pcb-design.md) since the first notes on this design and is
  why the enclosure allocates a third power module. On a bench that means
  feeding the modules from a separate supply, bench PSU or a converter off
  the 5 V tap, with grounds commoned to the CYD, rather than from `CN1`'s
  3.3 V pin. `CN1` is fine for something small and is not a radio rail.

  Two details come with it, and the NRF24 is the reason for both.
  **10 uF ceramic across the NRF24's supply at the module's own pins**, not
  back at the supply: that module is known for browning out on transmit
  transients and a few centimetres of wire is enough inductance to cause
  it. **100 nF at every module's VCC pin** as well.
- **Antenna spacing, which a bench gives you for free.** On a desk the
  modules are wherever you put them, which is to say far apart. An
  enclosure is the first time the ESP32's own antenna and the NRF24's are
  centimetres from each other, and those two are the problem pair: both
  are 2.4 GHz, the ESP32 is a transmitter, and the NRF24 PA/LNA is a
  receiver built to hear weak things.

  Expect WiFi or BLE transmitting to deafen the NRF24 while it does. A
  receiver cannot hear a fob across a car park through a transmitter in
  the same box, and the front end of a PA/LNA module is not protected
  against being shouted at from a few centimetres away. The failure looks
  like the NRF24 working on the bench and finding nothing once assembled,
  which reads as a wiring fault and is not one.

  What to do, in the order the leverage runs: put as much distance as the
  case allows between the ESP32 module and the NRF24's antenna, and keep
  the NRF24's antenna off the board and outside the shell, which it
  already is on an SMA bulkhead. Mount the two antennas at **right angles**
  rather than parallel; polarisation is worth several dB and costs
  nothing. Do not run a WiFi feature and expect 2.4 GHz receive at the
  same time. The GPS is the other victim here and
  [pcb-design.md](pcb-design.md) already notes it: a -130 dBm L1 receiver
  ends up between a 433 MHz transmitter and a 2.4 GHz PA, and its pigtail
  exists so it can be moved away from both.

  **None of this has been measured on this build**, because the NRF24 has
  never been wired. It is what to expect, not what was observed, and it is
  the first thing to check when the 2.4 GHz features are finally run.

- **Flex life.** Nothing here moves. A lid that opens and closes is the
  reason the connector question in pcb-design.md was argued at all.
