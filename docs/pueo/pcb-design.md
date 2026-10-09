# Pueo carrier board — design notes

Input for a KiCad project, not a layout. Everything here is derived from the
firmware pin map (`ESP32-DIV/board_pueo.h`), the enclosure
(`docs/pueo/pueo-enclosure.scad`), and module datasheets. Figures that need checking
against a meter are marked **[verify]**.

## What this board is

The CYD lives in the **lid**: the enclosure cuts a 55.5 × 92 window for its
bezel, and everything else sits in the 85 × 170 × 20 base. So a harness
between the two halves already exists in the hand-wired build. This board
replaces that harness and the loose modules with one PCB in the base, joined
to the CYD by a single connector.

It is explicitly **not** a shield. It cannot be, and that is worth
understanding before anything is drawn.

## Why it cannot plug in

**This board is drawn for the 3.5" ESP32-3248S035R**, which is the only panel
the firmware supports. It was drawn for the 2.8" ESP32-2432S028R until that
panel was dropped after 0.4.13, and retargeting it cost three rows of J1 and
nothing else: the outline comes from the enclosure base rather than from the
display, and the base does not change with the panel.

The three are `CC1101_CS`, GPIO 21 rather than 27 and on a header rather than
a pad; `NRF24 CSN`, GPIO 25 rather than 4; and the serial connector, which
this board calls P1 and the other called P5. The first two are in
`board_pueo.h` and always were: the backlight and the chip select swap 21 and
27 between the two boards, and 25 is free here because this panel's touch
controller hangs off the display's SPI instead of taking its own bus at
25/32/39.

Of the ten signals the firmware needs, only four reach a CYD header:

| signal | GPIO | where it is |
|---|---|---|
| CC1101 CS | 21 | P3 header |
| CC1101 GDO0 | 22 | P3 header |
| CC1101 GDO2 | 35 | P3 header |
| GPS RX | 1 | P1 JST |
| VSPI SCK | 18 | **microSD slot pin** |
| VSPI MOSI | 23 | **microSD slot pin** |
| VSPI MISO | 19 | **microSD slot pin** |
| NRF24 CSN | 25 | **module pad** |
| NRF24 CE | 16 | **RGB LED pad** |
| PN532 SS | 17 | **RGB LED pad** |

The SPI bus itself is not exposed. Six of ten signals require soldering to
pads or module pins on the CYD, so any carrier board needs a short pigtail
from those pads to a connector. The gain is that it becomes **six joints in
one documented place** instead of thirty scattered across four modules, and
an intermittent joint on MISO presents exactly like the bus faults that cost
a week of firmware archaeology, so reducing their count is not cosmetic.

**P1's two power pins are confirmed, 2026-09-29: pin 1 is 5V from the
silkscreen, pin 4 is GND by continuity to a known ground.** Two ends, two
methods, neither of them a document. That matters more than it sounds. A
second-hand answer consulted the same day gave the order as pin 1 TX, pin 2 RX,
and the carrier's cable puts `+5V_IN` on pin 1: following it would have driven
5 V into GPIO 1 and taken the ESP32 with it. Corrected, the same source then
explained itself by inventing a flipped board revision, which is worth knowing
about because a phantom variant is the kind of thing that gets written down.

Pins 2 and 3 are the serial pair by elimination and the firmware agrees, GPIO 1
carrying `GPS_UART_RX` with `GPS_UART_TX` at `-1`. That is inference rather than
a reading, which is fine for a pin nothing powers.

**P3 is confirmed too, 2026-09-30.** Its silkscreen is legible beside the
connector in the photograph of the board in the printed lid: `GND`, `IO35`,
`IO22`, `IO21`, which is what the pin map has said all along. Both connectors
this design depends on are now read off the board rather than off a document,
which is the whole lesson of the pin map in hardware.md.

## Architecture

```
  LID                              BASE (this board)
  ┌──────────────┐                 ┌────────────────────────────┐
  │ CYD          │                 │  radios · power · SMAs     │
  │  soldered ───┼──── J1  9-way ──┤                            │
  │  P3 ─────────┼──── J2  4-way ──┤                            │
  │  P1 ─────────┼──── J3  4-way ──┤                            │
  │  pigtail ────┤                 │                            │
  └──────────────┘                 └────────────────────────────┘
```

Three connectors, and nothing in the build needs crimping.

**[decided] 2026-09-26: three MX1.25 cables, all bought made up.** The split
falls out of where the signals actually are. Only four of the ten reach a
connector the CYD already has, three of them on `P3` and one on `P1`. The other
six are soldered to pads, and a pre-crimped lead with one end cut, stripped and
tinned serves those perfectly.

| | ways | series | carries |
|---|---|---|---|
| **J1** | 9 | PicoBlade | the six soldered signals, plus three grounds. No power pin |
| **J2** | 4 | JST GH | straight to `P3`, in `P3`'s own order: GND, IO35, IO22, IO21 |
| **J3** | 4 | JST GH | straight to `P1`: 5V, TX, RX, GND. The 5 V in, and the GPS out |

**Two series, on purpose. Do not unify them.** J2 and J3 have to mate with
the CYD's own headers, which are 1.25 mm **GH**, settled by fit on
2026-10-06 after a PicoBlade housing was offered to one and did not seat.
Both are 4-way, which is a size GH is stocked in.

J1 is the opposite case: only one of its ends is a connector, because the
other is nine wires soldered to the CYD's pads. Nothing about it has to
mate with a vendor part, and at 9 ways it must not be GH, for exactly the
reason the 14-way below was abandoned. PicoBlade runs 2 to 15 and is
stocked at 9.

**The 14-way it replaced was the one part that needed a crimp tool.** That
draft said 14-way 1.25 mm JST-GH, because GH is easier to hand-assemble than
FFC and tolerates a lid opening and closing. The second half still holds. The
first half was written without checking what a 14-way GH costs to obtain:
`GHR-14V-S` is a catalogue part, but the hobby market stocks GH in 4, 6, 8 and
10, because those are the Pixhawk sizes. Nothing turns up at 14. That left a
bare housing, a bag of `SSHL-002T-P0.2` contacts, and fourteen hand crimps at
0.2 mm² on the one cable carrying the whole SPI bus. A crimp that grips
insulation rather than conductor reads fine on a meter and fails under load.

So the connector question answered itself once the wires were counted rather
than assumed: three stock sizes, pre-crimped, against one odd size that has to
be made. An 0.5 mm FFC remains the alternative if height ever gets tight, with
the objection it always had, which is flex life on a lid that comes off to
reach the card.

**Whatever it is, the wire is 28 AWG.** GH contacts take about 32 to 28 and
are rated 1 A each, and 28 is what pre-crimped GH cable comes as. The same
figure applies to PicoBlade. So a contact is the limit here, not the copper.

**[decided] no 5 V pin at all, 2026-09-26.** This used to ask whether one 5 V
contact could carry what J1 would have to deliver, and worried that the return
was split three ways while the feed was not. The power tree answered it by
deleting the question. The CYD powers itself over its own USB-C, the carrier
taps P1 for the converter, and J1 carries signals and grounds only.

So J1 is 9-way with three grounds and no supply, and the contact rating that
worried this paragraph is not in the path any more. It is worth keeping as a
record of the order things were decided in: the power tree first, the pin count
second, which is what stopped it being found after fabrication.

### J1 pinout

| pin | net | to CYD | note |
|---|---|---|---|
| 1 | GND | GND | a ground at each end, and one in the middle |
| 2 | VSPI_SCK | GPIO 18 | microSD slot pad |
| 3 | VSPI_MOSI | GPIO 23 | microSD slot pad |
| 4 | VSPI_MISO | GPIO 19 | microSD slot pad |
| 5 | GND | GND | between the clocked lines and the static ones |
| 6 | NRF_CSN | GPIO 25 | module pad, not a header |
| 7 | NRF_CE | GPIO 16 | RGB LED pad |
| 8 | PN532_SS | GPIO 17 | RGB LED pad |
| 9 | GND | GND | |

Six signals and three grounds, which is every wire that has to be soldered at
the CYD end and nothing else. The three CC1101 lines left for `J2` because
`P3` already presents them, and the GPS left for `J3` because `P1` already
presents the pin it needs. There is no supply here at all.

`J3` pin 3 is `P1`'s RX and stays unconnected: `GPS_UART_TX` is `-1`, so
nothing is ever sent to the GPS.

| | |
|---|---|
| **J2** to `P3` | 1 GND, 2 CC1101_GDO2, 3 CC1101_GDO0, 4 CC1101_CS |
| **J3** to `P1` | 1 +5V_IN, 2 GPS_TX, 3 *(unused)*, 4 GND |

All three CC1101 control lines land on P3, which is the one place this board
is easier than its predecessor: chip select, GDO0 and GDO2 reach a connector
rather than costing a joint.

Ground on J1 pins 1, 5 and 9: one at each end and one in the middle, so
every signal has a return nearby and the clocked lines are separated from
the static ones. On a multi-drop SPI bus run through a cable this matters
more than the pin count suggests.

## The GPS series resistor

R1, 1 kΩ, in the GPS TX line before J3 pin 2.

GPIO 1 is UART0's transmit pin. The firmware releases it before the GPS
feature reads on it (`gpsPortOpen()` in `gps.cpp`), but the console owns it
the rest of the time, which means the ESP32 and the GPS are both driving that
net whenever a GPS feature is *not* open. R1 limits the contention current.

This is the single change most worth having in copper rather than in a
build note, because it is invisible, it is easy to forget, and forgetting it
degrades a pin slowly rather than failing loudly.

### Nothing in the base needs 5 V

Both datasheets are in hand now and they agree: the 5 V rail has no consumers.

| part | supply | source |
|---|---|---|
| ATGM336H GPS | 2.7 to **3.6 V max** | its manual, v1.2 |
| PN532 V3 | **3.3 V** to 5 V | Elechouse V3 guide |
| CC1101 | 3.3 V | |
| NRF24 PA+LNA | 3.3 V | |

The PN532 was the last candidate and it takes 3.3 V happily. Its onboard level
shifter exists for I2C and UART, which are 5 V tolerant; **SPI on that board is
3.3 V TTL**, and SPI is the mode this build uses. So there is no level
translation to pay for either.

**So delete the 5 V stage rather than rebuilding it.** The base needs one rail,
+3V3_RF, and one converter to make it from the cell. Going up to 5 V and back
down to 3.3 V was only ever there to feed a buck that cannot take a 1S cell
directly; with nothing else on 5 V, a single buck-boost does the whole job.

**J1 carries no power, decided 2026-09-26.** Pin 3 was `+5V_SW`, the carrier
feeding the CYD. The CYD has the cell on its own BAT1 and powers itself, and
the carrier taps the same cell for its buck-boost, so there is nothing for that
pin to do. **Thirteen nets**, all signals and grounds, which makes the
connector question above easier as well as smaller.

It also removes the hazard that replaced it: nothing now feeds 5 V into P1, so
there is no second source on a node USB also drives, and no question about what
arbitrates them.

### The GPS is a 3.3 V part, and it feeds its own antenna

From the ATGM336H-5N user manual, v1.2, which settles what was a [verify]:

| | |
|---|---|
| supply | 2.7 / **3.3** / 3.6 V |
| **absolute maximum** | **3.6 V** |
| typical | <25 mA at 3.3 V |
| peak, excluding antenna | **100 mA** |
| backup, VBAT | 1.5 to 3.6 V, 10 uA |

**5 V would destroy it.** 3.6 V is the absolute maximum on VCC, not a
recommended ceiling, so this part belongs on +3V3_RF and nowhere else. It is
on that rail in the tree below, which is correct, and the peak figure is four
times what the estimate said.

**The active antenna is not a separate supply problem.** Pin 14 VCC_RF is an
*output*, +3.3 V, and the module biases the antenna from it through an
inductor. The datasheet's own application circuit does exactly that, with
detection and short-circuit protection built in. Budget for it on +3V3_RF
rather than anywhere else: 3 mA with the antenna open, 50 mA into a short,
which the module limits rather than passing through.

**There is no backup cell, and there is no way to add one.** Checked against
the breakout on 2026-09-26: it is a GOOUUU board, 16 x 13 mm, and its header
is `VCC GND TX RX PPS`. Five pins. `VBAT` is not among them and neither is
`ON/OFF`, and there is no coin cell or supercap on the board.

So the RTC and SRAM die with the rail and every power-up is a cold start,
<=35 s rather than the <=1 s the module is capable of.

That matters less than it sounds, because the rail is always on: the GPS is
powered from boot whether or not a GPS feature is ever opened, so it has
acquired long before anyone goes looking. The 35 s is once per power-up of the
device, not once per use.

What it does cost is **25 mA continuously**, from boot, for a receiver that
may not be used that session. That is the argument for power-gating the GPS,
and this breakout forecloses it too: `ON/OFF` is not brought out either. The
choice the board offers is always-on-and-cold-starting, or absent.

Getting either back means soldering to the module's own castellations at
0.65 mm pitch, past the breakout. Worth knowing before that is the only
option left.

Confirmed at the same time: the antenna is 20 x 6 mm on a 90 mm u.FL pigtail,
which is what this document already assumed and what the enclosure's GPS
recess is cut for.

## Power tree

**Decided 2026-09-26: one rail, one converter, powered over USB.**

```
  USB-C ──► the CYD ──┬──► the CYD itself
  from a bank         │
  or a charger        └──► P1 `5V` ──► buck-boost ──► +3V3_RF ─┬─► NRF24
                                       (carrier)               ├─► CC1101
                                                               ├─► ATGM336H
                                                               └─► PN532
```

Power comes in on the CYD's own USB-C, from a power bank for portable use or a
charger on the bench. The carrier taps P1's `5V`, which is the board's 5 V
input node and is live whenever USB is, measured 4.75 V at 92 mA. That is
what the project's original notes meant by "tap 5V from USB VBUS, before the
CYD's regulator", and it is the pin they meant.

**Why not a cell: they are hard to source and a bank is not.** A power bank
also arrives with its own charger and its own protection circuit, both already
certified, which deletes three problems rather than solving them: sourcing the
cell, charging it, and the over-discharge protection that went out with the
TP4056.

**A cell is still a drop-in, and nothing here precludes it.** The enclosure
keeps its LiPo pocket and its BAT1 route; unused it is empty space, and no
part reprints either way. Fitting one is a single wire:

| | converter input | behaviour |
|---|---|---|
| no cell | P1's `5V` | USB only, radios die with the cable |
| cell fitted | the battery | radios stay up unplugged, charger tops it up |

That the decision is deferrable at all is down to the converter. The S7V8F3
spans 2.7 to 11.8 V, so both sources sit well inside its range and the same
part in the same pocket serves either.

**P1's `5V`, measured 2026-09-29.** Two points on one USB source, taken
without unplugging anything between them:

```
  open circuit          4.582 V
  220 Ω                 4.534 V      20.6 mA
```

`V = Voc - I*R` gives **2.33 Ω**, and a millivolt of meter resolution on each
reading puts it between 2.23 and 2.43.

That is the whole path rather than the pin: the USB lead, the board's traces
and the contact. A thin metre-long cable is an ohm on its own, so the share
that belongs to this design is smaller than the figure looks, and a better
cable moves it.

| load | the pin sits at | dissipated in the path |
|---|---|---|
| 108 mA | 4.33 V | 0.03 W |
| **256 mA**, the converter at worst case | **3.99 V** | **0.15 W** |
| 500 mA | 3.42 V | 0.58 W |

**So the tap is sound.** The S7V8F3 is specified from 2.7 V in, which makes 4 V
comfortable rather than marginal, and 0.15 W spread across a cable, a trace and
a contact heats nothing.

**It also explains the reading that came before it.** 4.75 V at 87 mA on
2026-09-26 looked incompatible with 4.534 V at 21 mA: voltage rising with
current, which no passive source does. At 2.33 Ω it resolves, because that
day's supply sat at 4.95 V and this one at 4.58. `Voc` belongs to the charger
and `R` belongs to the board, and only the second is a property of this design.
Two measurements 66 mA apart and three days apart agree on the half that
matters.

Linearity is still assumed. 2.33 Ω is what copper explains on its own, with no
polyfuse needed to account for it, so the extrapolation is reasonable rather
than proven. A third load point would settle that, and the cheap one is a
second 220 Ω in parallel: 41 mA, and 0.09 W in each part.

The cell lives in the base, in the pocket the floorplan already has at 0, -35,
and its leads run up to BAT1 exactly as the enclosure comment says. The carrier
taps the same cell. Nothing else is needed, because the CYD charges and powers
itself and every module in the base takes 3.3 V.

**Three parts leave the design**, and none of them is a compromise:

| part | why it is gone |
|---|---|
| TP4056 charger | the CYD's FM5324GA does it, confirmed on the bench |
| MT3608 boost | nothing wants 5 V, so there is nothing to boost for |
| MP2307 buck | it needs 4.75 V in and the cell never reaches it |

The 5 V rail only ever existed to feed a buck that cannot take a 1S cell. Once
the GPS and the PN532 datasheets showed nothing else wanted 5 V, the whole
stage was a conversion to nowhere. The MT3608 and the MP2307 are both in hand
and both stay on the shelf.

### The two-module path, reopened and then closed

**Settled: one buck-boost.** `pueo-enclosure.scad` records it, dated the
same day as the measurement that reopened the question: "Decided
2026-09-26: ONE buck-boost, from the cell straight to 3.3 V. The MT3608
does not come back and neither does the MP2307."

The rest of this section is why, and is kept because a rejected path is
worth more written down than deleted, and because the MT3608's real height
is a measurement somebody would otherwise take again. **Its arithmetic is
pre-shrink**: the power band it reasons about belongs to the 85 x 170 case,
and the LiPo pocket it measures against is at y = -35 now rather than -52.
Read it as the record of a decision, not as a live floorplan.

The MT3608's height was measured on 2026-09-26: **6.0 mm**, against a
CARRIER_HEADROOM of 13.9. It fits with 7.9 mm to spare, and the 14 mm figure
that would have ruled it out was simply wrong for this module.

That reopens an option Path A had closed. Cell to MT3608 to 5 V to MP2307 to
3.3 V uses two modules already owned, costs nothing and waits for nothing. The
price is efficiency, two switchers at about 90% each against a buck-boost's
one, so roughly 10% more drain, and two trimmers to set instead of none.

**The floorplan is what blocks it, by half a millimetre.** The power band runs
from the wall at y = -68.5 to the LiPo pocket at y = -52, which is 16.5 mm
deep, and the MP2307 sits in the middle of it leaving 29.5 mm each side:

    MT3608 beside the MP2307    36 long into 29.5    no
                                30 long into 29.5    no, by 0.5 mm
    both side by side           56 or 50 of 79 wide  width is fine
                                17 deep into 16.5    short by 0.5 mm

The disputed footprint does not change the answer. 36 x 17 and 30 x 17 both
fail the same way, because the binding dimension is the 17 mm depth against a
16.5 mm band. Moving the LiPo pocket up a millimetre fixes it, and that is a
SCAD edit and a reprint of the base rather than a redesign. The gap between
the pocket and the PN532 above it is 1.5 mm, so there is room to take it from.

**And it went the first way.** Buying a buck-boost kept the printed case
and the existing slot, which is the one at 0, -60, against a reprint and
10% of the runtime for the two modules already owned. The deciding argument
was not efficiency: a fixed 3.3 V part cannot be set wrong, and +3V3_RF
feeds three radios with nothing downstream to absorb a mistake, where the
two-module path puts two trimmers in series with them.

**[decide] which buck-boost**, and whether it fits. The slot at 0, -60 is
20 x 12 mm for a part that was 17.9 x 12, so there is 2.1 mm of slack in one
axis and none in the other. Most off-the-shelf buck-boost breakouts are larger
than that. As an IC on the carrier PCB it is a non-issue; as a module in the
hand-wired build it wants a small one or a bigger pocket.

**The S7V8F3 fits, but only turned.** Pololu give 11.4 x 16.5, and 16.5
does not go into the pocket's 12 mm depth. Rotated so the long axis runs
across the 20 mm, it clears with 3.5 mm to spare one way and **0.6 mm** the
other. That is a real fit rather than a comfortable one, and it depends on
the pocket being 12.0 rather than 12.0 minus print tolerance, so it is
worth a test print of the base before the part is bought.

Sizing it: the radios are the load that matters, and the GPS's peak is 100 mA
on its own.

**[verify] where over-discharge protection comes from.** This matters more
than it looks. The TP4056 module carried a DW01 and an FS8205 in the cell's
negative line, and an earlier netlist wired the load around them. Charging
would have worked, over-current would have worked, and over-discharge cutoff
would silently not have existed. That module is gone now, so the protection
went with it. What is left is whatever the pouch's own PCM does and whatever
the FM5324GA does, and neither has been established. A flat lithium cell taken
below 2.5 V is the failure this prevents, and it fails quietly.

**Why it has to be a buck-boost and not a buck.** A buck only steps down, and
a 1S LiPo swings 3.0-4.2 V against a 3.3 V target: above about 3.6 V a buck
works, below it the rail sags with the battery and the PA modules brown out
exactly when the pack is low. The MP2307 is spec'd from 4.75 V in, so it is
out of range across the entire discharge curve rather than part of it. This is
the same reasoning that used to argue for boosting to 5 V first; the
conclusion changed when it turned out nothing needed the 5 V.

That costs a conversion: two switchers in series at roughly 90% each is
about 81% end to end on the RF rail, against the ~90% a single buck-boost
would manage. A proper buck-boost is a real option if efficiency matters
more than using the part already on hand.

The separate 3.3 V rail itself is not optional. The PA/LNA modules brown out
if they share the CYD's regulator, which is why the enclosure already
allocates a third power module at (-29, -48).

### Where the 5 V rail came from

Worth recording, because with it deleted the old tree reads as though somebody
put a converter in for no reason. They did not. The project's original notes
say:

> Tap 5V from USB and step it down to 3.3V independently. Do not share the
> CYD's 3.3V rail or you'll get brownouts, random resets, and failed radio
> init.

Both halves were right when written, and one still is.

**The second half is still law.** The radios get their own rail and never
touch the display's. Every version of this design has obeyed it and this one
does too.

**The first half described a tethered device.** Tapping 5 V from USB and
bucking it down is correct, and P1's `5V` pin really does deliver it: measured
4.75 V at 92 mA with USB attached. Nothing about that instruction was wrong.

What broke it was the battery, which those notes predate. On a cell that pin
is an input with nothing behind it, so the 5 V the instruction assumed to be
free stopped existing. The design then did the faithful thing and added a
boost to manufacture it, so the buck could go on stepping it down as
specified:

    original, tethered      USB 5V ------------------> buck ------> 3.3V
    once a battery arrived  cell ---> boost to 5V ---> buck ------> 3.3V
    what it actually needs  cell ---> buck-boost -------------> 3.3V

The middle row is the top row carried forward after its premise expired. Every
part that followed descends from that one line: the MT3608 to remake the 5 V,
the MP2307's 4.75 V minimum that made it unusable from a cell, and the TP4056
to charge a battery the notes never anticipated.

The lesson is not that the old design was careless. It is that a premise can
expire quietly while everything built on it goes on looking sound, and that
what exposed it here was a datasheet and a resistor rather than a review.

### Budget

| load | rail | typical | peak | source |
|---|---|---|---|---|
| PN532 | **+3V3_RF** | ~10 mA idle | ~100 mA field on | rail from datasheet, current **[verify]** |
| NRF24L01+PA+LNA | +3V3_RF | 45 mA RX | ~115 mA TX @ +20 dBm | **[verify]** |
| CC1101 | +3V3_RF | 16 mA RX | ~34 mA TX @ +10 dBm | **[verify]** |
| ATGM336H | +3V3_RF | <25 mA @3.3 V | **100 mA peak** | datasheet |

The CYD is not in that table any more, because it does not draw through the
carrier at all: it powers itself from its own USB-C, and the carrier taps P1
only for the converter. The rail's own draw reflects back onto P1's 5 V, scaled
by 3.3/5 and divided by the converter's efficiency:

```
                  +3V3_RF        drawn at 5 V     plus the CYD    from the bank
  worst case      349 mA    ->      256 mA           310 mA           566 mA
  idle             96 mA    ->       70 mA          ~200 mA           270 mA
```

Worst case assumes everything transmits at once, which the SPI bus makes
impossible: it is shared and `SpiBus` enforces one owner at a time, so the
radios cannot all be mid-transaction together.

**The CYD's 310 mA is measured**, not estimated: 448 mA at 4.00 V through BAT1
with the beacon spammer running, 1.787 W, converted to the 5 V socket by
allowing 85 to 90% for the boost that the BAT1 path goes through and the USB
path does not. See *What the board actually draws* in
[hardware.md](hardware.md). The idle figure beside it is still a guess.

**Any power bank will do this.** 566 mA at 5 V sits inside the 1 A a basic bank
delivers and a long way inside 2 A. Runtime is the bank's rather than a cell's:
a 10,000 mAh bank is roughly 30 Wh usable, and at about 2.8 W that is **ten
hours or so**, against the two a 2000 mAh internal cell would have given. If it
is short, the lever is the backlight, not the radios.

### Decoupling

- 10 µF ceramic across the NRF24 supply **at the module pins**, not near the
  regulator. This module is notorious for browning out on TX transients and
  the inductance of a few centimetres of trace is enough to cause it.
- 100 nF at every module's VCC pin.
- 100 µF bulk on +3V3_RF at the buck-boost output.
- 220 µF bulk at the converter's input, close to where P1's 5 V arrives. This
  was specified at the boost's output on +5V_SW; there is no +5V_SW and no
  boost now, but the reason survives. The input is at the far end of a USB
  cable and a connector, and the converter's draw steps with the radios.

### Trace widths

1 oz copper, outer layer, 10 °C rise:

| net | current | width |
|---|---|---|
| P1's 5 V in, to the converter | up to 260 mA | **0.5 mm** |
| +3V3_RF | up to 500 mA | **0.5 mm** |
| signals | n/a | 0.25 mm |

## SPI signal integrity

Five devices on one bus, three of them through a cable, is the part of this
design most likely to disappoint.

- Keep module stubs off the SCK/MOSI spine as short as the placement allows.
  Daisy-chain along the spine rather than star-wiring from J1.
- Footprints for **22 Ω series resistors on SCK and MOSI** at the J1 end.
  Fit 0 Ω initially; they are there so ringing is a part swap, not a respin.
- The CC1101 runs at 4 MHz and the NRF24 at up to 10 MHz, both set in the
  `kProfiles` table in `ESP32-DIV/SpiBus.cpp`. At 10 MHz through a cable, edge
  rates matter.
- Chip selects are the safe nets to route awkwardly. They are static during a
  transaction, so a long CS trace costs nothing.

## Mechanical

Base interior is **79 mm** wide and **137 mm** long, with a 2 mm pocket and
a 4 mm floor. M3 bosses at ±36, ±66.

Module positions are fixed by the enclosure, and `pueo-enclosure.scad` is
the authority for them rather than this file (origin = case centre):

| module | centre (x, y) | size |
|---|---|---|
| buck-boost | 0, -60 | 20 × 12 (pocket) |
| LiPo pack | 0, -35 | 45 × 34 |
| PN532 V3 | 0, 4 | 43 × 41 |
| ATGM336H GPS | 0, 40 | 16 × 13 |
| CC1101 HW-863 | -23, 48 | 15 × 40 |
| NRF24 PA+LNA | 22, 47.5 | 16 × 41 |

**Six modules, not eight, and on the centreline.** The case is 84 × 142,
down from 85 × 170. The MT3608 and the TP4056 came out when the CYD's
FM5324GA turned out to be both of them in one chip, and taking them out
removed 28 mm of length and let everything else line up on x = 0. They
come back, and so does the larger case, only under `EXT_CHARGER = true`,
for a board that has no charger of its own.

**This table replaced an 85 × 170 one** that still listed the MT3608, the
TP4056 and the MP2307 at their old positions, with every y about 14 mm out
and no row for the part actually chosen. The enclosure had been redrawn and
this file had not: the two live in different repositories and nothing
compares them, which is the only reason it survived.

Whether a real buck-boost fits that 20 × 12 pocket is open, and is tracked
once under **[decide] which buck-boost** above rather than twice here.

Constraints that follow:

- **SMA bulkheads at x = -23 and x = +22**, 45 mm apart, axis 5 mm above the
  pocket floor, through the top wall. The radio footprints must put their
  board SMAs exactly there.
- **PN532 needs a 1.4 mm floor window** under its coil at (0, 18). Keep
  copper, especially ground pour, out of that footprint's area on both
  layers, or the field is attenuated by your own board. Now confirmed at the full
  43 x 41 extent, so the keepout and the floor window are both correctly
  sized. The module is exactly as large as the design assumed.
- **USB-C on the right wall** at y = −62, 20 × 7 cutout.
- **GPS antenna** is a separate 20 x 6 mm board on a 90 mm u.FL pigtail, not
  a patch on the module. It needs a flat spot with sky view and a thinned
  wall, but it is no longer tied to where the module sits, and the 90 mm
  is a hard limit. See the GPS section below; the top-centre slot the
  enclosure cuts today is out of reach from the module's current position.
- Battery pocket at (−16, -23) is 45 × 34 and must stay clear of copper.

### Recommendation: keep the radios as modules

Header sockets for the HW-863, NRF24 and PN532 rather than integrating the
silicon. Integrating CC1101 or NRF24 means matching networks, antenna tuning
and two or three spins before it works as well as a $4 module. Modules also
let you swap a dead PA board in the field. The cost is height, which this
enclosure has.

## Generated inputs

`tools/gen_netlist.py` emits into `dist/pcb/`:

```
netlist.txt        17 nets, 71 connections, net by net
pueo-carrier.net   KiCad legacy netlist
placement.csv      module centres in board coordinates
bom.csv            26 parts
```

Pins are named functionally (VCC, SCK, CSN), not numbered. Module pin numbers
come from datasheets, and none of these modules has a standard library
footprint, so numbering them here would be inventing data. The netlist gives
the connections; the datasheets give the numbers.

The generator refuses to write anything that disagrees with
`board_pueo.h`, so the netlist cannot drift from the firmware. It also
rejects single-ended nets, which caught a real mistake: an initial `USB_VBUS`
net had one endpoint, because the Type-C jack is on the TP4056 module rather
than on this board.

Enclosure coordinates convert to a top-left origin with Y downward:

```
kicad_x = enclosure_x + 40      board is 80 x 165 mm
kicad_y = 82.5 - enclosure_y    the base interior, less 2.5 mm walls
```

Checked after generation: all eight modules sit inside the outline, none
overlap, and the SMA positions land exactly on the J4 and J5 centres. The
tightest gaps are 2 mm (PN532 to NRF24, and the buck to the battery), so
there is little room to move anything without revisiting the enclosure.

**The KiCad `.net` file has never been opened in KiCad**. It is not installed
on the machine that generated it. `netlist.txt` is the deliverable; the
`.net` is a convenience that may need hand-fixing.

## What the first datasheet changed

The TP4056 module, photographed and measured, disagreed with three
assumptions. Two were paperwork. One was an electrical fault.

**It is micro-USB, not USB-C.** Both the BOM and the enclosure said Type-C.

**It is 27 x 17 mm, not 26 x 19.** The enclosure pocket is wrong in both
axes, and it mounts rotated 180 degrees from how it is usually photographed
so the jack faces +X and reaches the right wall.

**The load was wired around the protection circuit.** This is the one that
mattered. On these boards the DW01/FS8205 protection MOSFETs sit in the
*negative* line, between `B-` and `OUT-`:

```
  BT1 + ────────────► B+            OUT+ ────► U1.VIN, U3.VIN
  BT1 - ────────────► B-   [FETs]   OUT- ────► system ground
```

`B-` and `OUT-` are not the same node. The first netlist put the battery
negative on the common ground net and took the converters off `B+`, which
runs the entire load around the protection. Charging would have worked,
over-current would have worked, and **over-discharge cutoff would silently
not exist**, the failure mode being a flat lithium cell taken below 2.5 V
because nothing was watching.

Now `VBAT`/`BATT_NEG` reach only the battery and the module, and everything
else hangs off `+VSYS` and a ground that is `OUT-`.

Worth generalising: the enclosure was laid out from module outlines, and an
outline tells you nothing about which terminal is which. Expect the other
datasheets to move things too.

## The MT3608, and a bring-up order that matters

**This section applies to `EXT_CHARGER` builds only.** The default build has no
boost and no charger inside the case: the CYD is powered over its own USB-C and
the carrier taps P1's 5 V for one converter. Everything below is about the
variant that carries its own charger, which the enclosure still lays out as
`MODULES_EXTERNAL`.

Pin names are `VIN+`/`VIN-`/`OUT+`/`OUT-`, not the `VIN`/`VOUT`/`GND` the
netlist first assumed. `VIN-` and `OUT-` are the same node on a boost, so
both land on ground.

Two things about this module are worth more than a pin correction.

**Both switchers ship adjustable, and neither ships at the voltage you
want.** The MT3608's is a 25-turn pot; the MP2307's is a single-turn SMD
trimmer, which is worse to set precisely because the whole range is in one
rotation. Set *both* against a meter, into no load, before either one is
connected to anything. The buck is the dangerous one: +3V3_RF feeds the
NRF24, the CC1101 and the GPS directly, with no regulator downstream to
absorb a mistake.

**The output is a multi-turn trimpot, not a fixed 5 V.** These ship at an
arbitrary setting and the MT3608 will happily produce about 28 V. Connecting
J1 to an unadjusted module destroys the CYD, and the ESP32 behind it,
instantly and permanently.

So the assembly order is not a preference:

```
1.  Power the boost from the battery with NOTHING connected to OUT+
2.  Meter OUT+ to ground, turn the trimpot to 5.00 V
3.  Only then fit J1
```

A test point on the boost's output exists for exactly this. Worth a line of silkscreen next to
the pot saying `SET 5V FIRST`, because the person who assembles the second
one in a year will not remember.

**It has its own micro-USB jack**, which this design does not use. The boost
is fed from `+VSYS`. The enclosure cuts only one USB opening, at the TP4056,
so the boost's connector ends up inside the case. That is the right outcome
(two identical micro-USB sockets, one charging and one backfeeding the boost
input, is a support question waiting to happen) but it does occupy space and
wants clearance from anything it could short against.

The MT3608 silicon is SOT-23-6, 2.9 x 1.6 mm, 0.95 mm pitch, if integrating
it ever becomes tempting. The recommendation above still stands: the module
costs about a dollar and comes with its inductor, diode and feedback network
already laid out and working.

## Module dimensions

Collected as datasheets and listings turn up. Footprints are mostly settled;
heights are the gap, and heights are what the enclosure budget runs on.

```
module           footprint mm    height mm    basis
--------------   -------------   ----------   -----------------------------
MT3608 boost     36 x 17  (?)    6.0          MEASURED 2026-09-26, on the
                 30 x 17  (?)                 module in hand. Neither figure
                                              in circulation was right; 14 was
                                              the one that would have ruled it
                                              out. Footprint still unmeasured,
                                              and it turns out not to matter
TP4056 charger   27 x 17         ~4  (est)    micro-USB jack is the tallest
                                              thing on it, ~2.7 over ~1.0
S7V8F3 buck-boost 11.4 x 16.5    3.0          Pololu give 0.45 x 0.65 x 0.1
                                              inch. The first two convert to
                                              11.43 and 16.51; the third is
                                              2.54, and 3.0 is that ROUNDED UP
                                              on purpose. Still the shortest
                                              part in the case, and the only
                                              one whose figure comes from the
                                              maker rather than from eyeing
                                              the tallest component
MP2307 buck      17.9 x 12       ~6  (est)    inductor and trimmer stand
                                              proud of a ~1.0 board. Rejected:
                                              a buck cannot start at 2.7 V
PN532 V3         43 x 41         ~3.5 (est)   flat board, no tall parts
ATGM336H GPS     16 x 13         ~3.5 (est)   shield can over a ~1.0 board
HW-863 CC1101    28 x 15         ~7  (est)    set by the SMA barrel, 6.35
                                              OD, axis near the board top
NRF24 PA+LNA     41 x 15.5       ~8  (est)    SMA barrel plus the shield can
LiPo pack        45 x 34         ?            depends on the cell; 6-10 for
                                              a 2000 mAh pouch
```

**The S7V8F3 has four pins, not three.** In a row along one edge at 0.1 inch
spacing, and confirmed on the part in hand, 2026-10-06:

```
VOUT   GND   VIN   SHDN
                   ‾‾‾‾
```

`SHDN` is printed with an overbar, so it is **active low**: the board runs
when the pin is high or floating and shuts down when it is pulled to ground.

The netlist connects three of them and that is correct electrically, because
Pololu say `SHDN` may be tied to `VIN` or left disconnected to leave the board
permanently enabled. That works because of the polarity above, not in spite of
it: an unconnected pad is a running converter. It still needs a pad. A three-pad footprint for a
four-pin part is the kind of thing that is free to fix now and a scalpel
later.

**[verify] the silkscreen does not say which output.** The back of the part
in hand reads `reg09b`, `0J7031`, the Pololu logo and (c)2012, photographed
2026-10-08. Those markings are the whole S7V8 family's: the adjustable S7V8A,
the S7V8F3 at 3.3 V and the S7V8F5 at 5 V all carry them. They do rule out the
S7V7F5, which is `reg09a` and `0J3933` and a different size, so they confirm
the 11.4 x 16.5 footprint everything above is dimensioned against.

No trimmer on the component side rules out the S7V8A, which leaves F3 against
F5 and nothing on the board to tell them apart. A 5 V part on `+3V3_RF` feeds
three radios rated 3.3, so the output wants a meter on it before it feeds
anything, not a trusted shipping label.

The minute that takes is already in the build: Stage 0 of the bench build sets
the rails and measures them unloaded before a module is on the end of them.
That step was written for adjustable parts shipping at an arbitrary setting,
and it is also what tells you which fixed variant arrived. Worth saying,
because a fixed regulator reads as nothing to check.

**Read that order off the silkscreen, not off this page.** A four-pin inline
footprint has two ways to be right and one of them is backwards: the same part
is `VOUT GND VIN SHDN` from one end and `SHDN VIN GND VOUT` from the other,
and both sentences are true. This note used to give the second one with no
reference edge, which is free to carry in prose and expensive on a board,
where it puts `VIN` on the `VOUT` pad. The silkscreen labels every pin, so the
board is its own authority and nothing here needs to be trusted.

Worth knowing for a second spin: `SHDN` is how the RF rail could be switched
off in software, which would take the radios' idle draw out of the budget
entirely. There is no spare GPIO for it today, so the pad is the whole of the
provision.

Whichever pin it eventually gets has to be chosen on its reset behaviour
rather than its number. Active low means the rail is off while the pin is
held down, so a strapping pin or anything else that sits low out of reset
keeps the radios dark until firmware gets far enough to raise it, and a brief
dip during a reset drops the rail mid-operation. A pin that comes up as a
high-impedance input is the safe shape: the part's own pull-up then holds it
enabled until something deliberately pulls it down.

**The five marked `(est)` are reasoned from the tallest visible component,
not measured.** Listings for these parts publish footprint and almost never
publish height. I went looking and it is simply not there. They are good
enough to answer the question below, and not good enough to cut plastic to.

One number got confirmed rather than estimated: an SMA coupling barrel is
6.35 mm outside diameter, fixed by the connector standard rather than by a
vendor. `SMA_D = 6.5` in the enclosure is a 0.15 mm clearance hole on that,
so the bulkhead holes have been right all along.

Both radios land within a millimetre of what the enclosure already guessed:
CC1101 at 15 wide against a 15 mm pocket, NRF24 at 15.5 against 16. Their
pocket lengths (40 and 41) hold too, and the CC1101's 38 mm overall leaves
2 mm. Nothing moves horizontally for either.

The CC1101 drawing confirms something the enclosure was already built
around: **the SMA jack is soldered to the module**, edge-mounted, with the
2x4 pin header at the opposite end. The comment in the enclosure
("radios: vertical, board SMA against the top wall") had this right. It
means the radio SMAs are not free to move: they sit wherever the module
sits, which is the constraint the next section runs into.

## Height, and the pockets problem

**The MT3608 has two contradictory dimension sets and they disagree in the
axis that matters.**

```
  36 x 17 x 6.25 mm     with a 25-turn trimpot
  30 x 17 x 14 mm       Amazon listing, "Board Size (L*W*H)"
```

Width agrees. Length differs by 6 mm, height by more than double. The
likely reading is that 6.25 mm is the bare PCB plus low components while
14 mm is the overall height with the trimpot and inductor standing proud
(the blue 3296-style multi-turn pot is tall on its own), and that 30 vs 36
is a variant difference. But that is a guess, and the conclusion flips on
it, so it wants a caliper rather than a listing.

The height is the first real figure in a budget that has been running on
assumption, and it surfaces something structural either way.

Vertical space in the base:

```
  base outer height              20.00 mm
  floor less pocket depth         2.00 mm
  pocket floor to base top       18.00 mm    everything lives in here
```

### How much room is there, really

The carrier does not sit on the pocket floor. It sits on standoffs, because
through-hole leads have to go somewhere. That was missing from the earlier
arithmetic, and it changes the answer:

```
  cavity above the pocket floor        18.00 mm
  standoff 2.5 + carrier 1.6            4.10 mm
  room above the carrier               13.90 mm
```

**13.90 mm is the budget every soldered-down module lives inside.** Which
makes the MT3608 conflict decisive rather than academic:

```
  MT3608 at  6.25 mm   ->  +7.65 mm spare
  MT3608 at 14.00 mm   ->  -0.10 mm
```

At 14 mm it does not fit. Not "fits tightly", over by a tenth of a
millimetre, before any tolerance on the standoff or the print.

This corrects what this document said one revision ago, which was that at
14 mm the module "fits, 1.65 mm spare" soldered direct. That was carrier
plus module with the standoff left out. With the standoff counted the two
candidate dimensions give opposite answers to "does this design work", so
the MT3608 measurement is no longer one detail among several.

### The PN532 pays for height twice

Copper keepout solves one half of the NFC problem. The other half is
distance, and a carrier board makes it worse rather than better.

The base thins its floor to 1.4 mm under the coil specifically so the field
can reach through the case. That budget assumed the module sitting on the
pocket floor. Put it on a carrier instead and the coil moves up by the board
plus whatever holds it:

```
  soldered direct              1.6 mm further from the outside surface
  low-profile socket 5.0       6.6 mm
  standard socket 8.5         10.1 mm
```

Read range on a PN532 is a couple of centimetres to begin with. Ten
millimetres of added standoff is a large fraction of it, and it is spent on
nothing the user gets back.

## The GPS is a different module than assumed

The part in hand is an **ATGM336H** on a GOOUUU breakout, 16 x 13 mm, with a
1x5 header silkscreened VCC / GND / TX / RX / PPS. Two things follow.

**The footprint collapses.** 28 x 27 was budgeted; 16 x 13 is what turned
up. That is roughly 550 mm2 of floor handed back, in the middle of the
board, next to the battery. It also resolves the `[verify pinout]` note on
J7, the silkscreen matches the assumed order exactly, so the netlist
stands.

Firmware is unaffected: `GPS_UART_BAUD` is 9600 and the ATGM336H defaults to
9600 NMEA, same as the GT-U7 would have.

**The antenna moves off the module**, onto a 20 x 6 mm board on a 90 mm u.FL
pigtail. This is mostly good. A patch soldered to the module has to sit
wherever the module sits, and this one does not. But 90 mm is short, and it
is measured through whatever path the cable can actually take:

```
  from J7 at (24, -21) to ...          straight line
  top centre, the slot cut today          94.1 mm    over
  top left, clear of the NRF24           105.8 mm    over
  upper right wall                        61.5 mm    ok
```

Straight-line is the optimistic case; the cable has to route around modules,
so treat anything past about 75 mm as doubtful.

### Where it went

**J7 moved from (24, -21) to (0, 52).** The antenna slot the enclosure
already cuts sits hard against the inside of the top wall, centred, so the
antenna lands at about (0, 80). From the new position the run is **28 mm**
against 90 mm of cable, comfortable even after routing around things, with
enough left over that the excess has to be coiled somewhere.

The two radios leave a corridor between them:

```
  CC1101 right edge    x = -15.50
  NRF24  left edge     x = +14.25     29.75 mm of clear corridor
  PN532  top edge      y = +38.50
```

A 16 mm module centred in 29.75 mm has room on both sides, and y = 52 sets
the clearances evenly:

```
  to CC1101   7.50 mm
  to NRF24    6.25 mm
  to PN532    7.00 mm
```

All three beat the 2 mm that is the tightest gap elsewhere on the board.
Biasing left, toward the CC1101 and away from the 2.4 GHz PA, was the
obvious temptation, but it buys about a millimetre of separation while
cutting the CC1101 gap to 2.5 mm, which is a bad trade. Centred is better.

This leaves 28 x 27 of floor free at the old position, next to the battery.
Nothing needs it yet.

A second pairing, not addressed anywhere until now: the **ESP32's own
antenna against the NRF24's**. Both are 2.4 GHz, the ESP32 transmits, and
the NRF24 PA/LNA is built to hear weak signals, so WiFi or BLE activity
should be expected to deafen it while it runs. The board cannot fix that by
layout alone, since the ESP32's antenna is on the CYD and the NRF24's is on
a bulkhead; it is an enclosure decision about distance and about mounting
the two at right angles rather than parallel. Unmeasured, because the NRF24
has never been wired. Noted in bench-build.md as the first thing to check
when it is.

One thing the move does not fix: the antenna still ends up between the two
SMA bulkheads, so a -130 dBm L1 receiver sits between a 433 MHz transmitter
and a 2.4 GHz PA. The pigtail means it *could* go elsewhere, flat against
a side wall or the inside of the lid, which is a freedom the patch-antenna
assumption never had. That is an enclosure decision rather than a board one,
and it can be made later without moving J7 again: 90 mm of cable reaches
most of the upper half of the case from (0, 52).

So the PN532 is the one module with a reason not to be socketed, which cuts
against the swappability argument that applies to the radios. Three ways
out, none free:

1.  **Solder it down** and accept 1.6 mm. Cheapest, loses the ability to
    swap a module whose counterfeit rate is not low.
2.  **Cut it out of the carrier entirely**: leave it on the floor in its
    existing pocket and run a short flying lead to the board. Keeps the
    range, adds a cable and an assembly step.
3.  **Window the carrier** so the PN532 hangs through a cutout at floor
    level while its header still lands on the board. Best of both, and the
    most work to get right.

Option 3 is the interesting one because the board already needs a keepout
over that area, turning a copper keepout into an actual hole costs
nothing in routing terms. It does mean the 43 x 41 region stops carrying
structure, which matters for a board that is 80 mm wide. The NRF24 with its PA/LNA can and its SMA is taller, and
the SMA axis is fixed at 5 mm above the pocket floor by the case wall. That
constraint and a carrier board are in direct tension: raising the modules by
a PCB plus a socket raises their SMAs too, and the bulkhead holes do not
move.

**Which is the real finding: the enclosure is built for modules sitting
directly on the floor.** It cuts an individual pocket per module, at
per-module depth. A carrier board is one flat plane spanning all of them, so
those pockets stop being useful and start being obstructions, and the SMA
height stops lining up.

That is not an argument against the board. It means the base needs revising
alongside it:

```
per-module pockets   ->  one flat shelf at a single height for the PCB
mounting bosses      ->  positioned for PCB holes, not module corners
SMA_Z = 5.0          ->  recomputed from PCB + socket + module-SMA height
```

Worth resolving before layout rather than after, because SMA height drives
where the radio modules sit vertically, which drives socket choice, which
drives whether the radios are swappable or soldered down.

**Current recommendation**: low-profile sockets for the radios, so a dead PA
module can still be replaced, and solder the power modules directly since
they will not be swapped. That splits the difference on height and keeps the
part that actually fails serviceable.

### Enclosure changes: applied

Done in `docs/pueo/pueo-enclosure.scad`. Both parts still render manifold.

The enclosure now lives in this repo. It was a loose file in the parent
directory, tracked by nothing, while being the thing every dimension in this
document is measured against. The working copy alongside the repo has since
been renamed to match, so both are `pueo-enclosure.scad`; the tracked one is
authoritative and the two are currently in sync.

```
MODULES[1]   26 x 19  ->  27 x 17         TP4056 pocket
MODULES[4]   28 x 27 at (24,-21)          GPS pocket shrunk and moved to
             ->  16 x 13 at (0, 52)       the corridor between the radios
GPS_D        5  ->  7                     the antenna board is 6 mm deep
                                          and would not fit a 5 mm recess
USBC_W/HT    20 x 7  ->  USB_W/HT 11 x 6  micro-USB, not Type-C; clears the
                                          plug shell, not the overmould
SMA_Z        5.0  ->  derived             now a formula, see below
NFC_INDEX    5  ->  looked up by name     reordering MODULES can no longer
                                          point the thin floor at the wrong part
```

Housekeeping picked up along the way: the header said `85 x 140 x 20` while
`L = 170`, and it still carried the name the workspace had before this was
called Pueo. Both fixed. Part names in MODULES
now match the netlist (`ATGM336H GPS`, `MP2307 buck`).

Pockets left deliberately oversized, with a comment saying so: MT3608 36 x 17
for a part that may be 30 mm, CC1101 15 x 40 for a 15 x 38, NRF24 16 x 41 for
a 15.5 x 41, buck 20 x 12 for a 17.9 x 12. `pockets()` adds a further 0.6 mm
to each dimension, which is worth knowing. The NRF24's length clearance is
0.6 mm rather than the zero the nominal numbers imply. Still tight for FDM,
so still worth a test print, but not the interference it looked like.

### SMA_Z is now derived, and it is not 5.0

The old value was asserted. It cannot be: both radios carry their own
edge-mounted SMA, so the bulkhead height is wherever the module's connector
ends up once the module is stacked on a carrier board.

```
SMA_Z = STANDOFF_H + PCB_T + SOCKET_H + MOD_PCB_T + SMA_AXIS_H
```

`MOD_PCB_T` (1.0) and `SMA_AXIS_H` (2.5) are marked `[VERIFY]`. They need
the radio modules in hand. With the current values and the radios soldered
down, OpenSCAD echoes **SMA_Z = 7.6 mm**, against the 5.0 the case was cut
for. The bulkheads move up 2.6 mm.

### Only two unknown heights can change anything

Seven heights were open. Sorting them against the 13.90 mm budget collapses
the list:

```
  MT3608 boost     6.0  MEASURED   7.9 mm spare -- it fits
  LiPo pack        6-10            structural, not a fit question
  NRF24 PA+LNA     ~8   est        ~6 mm clear at the estimate
  HW-863 CC1101    ~7   est        ~7 mm clear
  MP2307 buck      ~6   est        ~8 mm clear
  TP4056 charger   ~4   est        ~10 mm clear
  ATGM336H GPS     ~3.5 est        ~10 mm clear
  PN532 V3         ~3.5 est        sits on the floor, not on the board
```

Every estimate would have to be wrong by 6 mm or more to matter, and these
are flat modules whose tallest part is visible in a photograph. Not worth
chasing further.

The radios deserve one extra note, because their SMA has to line up with a
hole in a wall. The axis must land between 3.25 and 14.75 mm above the
pocket floor for the 6.5 mm hole to stay inside the cavity, and with the
carrier top at 4.10 mm that means:

```
  radios soldered down      SMA axis 0.00 .. 10.65 mm above their own board
  radios on a 5 mm socket   SMA axis 0.00 ..  5.65 mm
```

Any real SMA mounting is 1 to 4 mm above the board it sits on. So SMA height
is not a fit risk in either configuration. It only sets where the hole goes,
and that is already a formula in the enclosure rather than a constant.

**So two measurements are blocking, and neither can be looked up:**

1.  **MT3608 overall height**, trimmer and inductor included. Over 13.90 mm
    and it cannot be soldered to the carrier at all, which means a shorter
    boost module, a thinner standoff, or a taller base.
2.  **Battery thickness.** Not a fit question but a structural one; see
    below.

Everything else is decided.

### Still open: the carrier cannot be a flat plane

The remaining delta was "per-module pockets -> flat PCB shelf". It did not
get applied, because working through it surfaced a problem that a shelf
parameter does not solve.

The interior is 80 x 165, which is exactly the carrier outline. So a
full-span board covers the battery pocket at (-16, -23) completely. The
battery is a physical object with a thickness nobody has measured yet (a
2000 mAh 1S pouch is typically 6 to 10 mm), and it has to be either under
the board, on top of it, or through it.

Under it means the standoff grows to the battery's thickness, and everything
above rises with it:

```
  standoff   socket   SMA_Z    hole spans      18 mm cavity
     2.5       0       7.6     4.35 .. 10.85   ok     through-hole leads only
     2.5       5.0    12.6     9.35 .. 15.85   ok
     7.0       0      12.1     8.85 .. 15.35   ok     6 mm battery underneath
     7.0       5.0    17.1    13.85 .. 20.35   over
    11.0       0      16.1    12.85 .. 19.35   over   10 mm battery underneath
```

A 6 mm cell under the board works only with the radios soldered down. A
10 mm cell does not work at all, the SMA holes run out through the top of
the base.

Which is the same shape of problem as the PN532: two things want to be at
floor level and the board is in the way. One answer covers both. **The
carrier wants to be a frame, not a plane**, cut out the battery footprint
and the PN532 footprint, let both sit on the floor where the case already
has pockets and a thinned NFC window for them, and keep the standoff at the
2.5 mm that through-hole leads need. That holds SMA_Z at 7.6 and keeps NFC
range.

The cost is structural: two large holes, 45 x 34 and 43 x 41, in an 80 mm
wide board. Whether what is left is stiff enough to carry a screwed-down
lid is a question for the layout, not for the enclosure.

This needs a decision and a measured battery before the base geometry
changes. Until then the per-module pockets stay, which is the conservative
state. They are correct if the modules sit on the floor, and harmless
extra clearance if they do not.

MT3608 keeps its 36 x 17 pocket: if the board is really 30 mm the pocket is
6 mm oversized, which is slack rather than interference, and oversizing is
the safe direction to be wrong in until it can be measured.

Both radio pockets stand as cut. CC1101 15 x 40 against a 15 x 38 module,
NRF24 16 x 41 against 15.5 x 41, tight on the NRF24's length with no
slack at all, so that one is worth a test print before committing.

### The NRF24 fit test

`docs/pueo/nrf24-fit-test.scad` is that test print. It renders two parts.

`PART="ladder"` is a 100.5 x 55 x 4 plate carrying five pockets at 0.2,
0.4, 0.6, 0.8 and 1.0 mm of total clearance over a 15.5 x 41 module, each
labelled and each with a window through the floor to push the module back
out. The smallest pocket the module seats into flat, without forcing, is
what this printer needs. Print this one first; it is flat and quick and it
answers the question on its own.

For reference, the pocket as currently cut is 16.6 x 41.6, `pockets()`
adds 0.6 to the 16 x 41 in `MODULES`. Against a 15.5 x 41 module that is
1.1 mm of clearance across the width and 0.6 along the length, so the
ladder's 0.6 rung is the closest thing to the shipping geometry. If even
the 1.0 rung is tight, the `MODULES` entry has to grow rather than the
kerf.

`PART="insitu"` is a 36.5 x 51 x 15 slice of the base around the pocket,
taken as an `intersection()` with `base()` itself rather than redrawn, so
it cannot drift from the real part. It carries the top wall with the SMA
bulkhead bore at its derived height, the corner boss at (36.5, 70), and
slivers of the GPS and PN532 pockets where they cross the cut. Print it
second, to check that the module's edge-mounted SMA actually lines up with
the bulkhead hole, which is the half of "does the NRF24 fit" that the
ladder cannot answer.

Neither coupon changes the enclosure. Whatever the ladder says still has
to be applied to `pockets()` by hand.

## Before laying anything out

**Build it by hand first and bring it up.** A PCB freezes the pin map, and
three things could still move it:

1. The **GPIO 1 GPS handover** is reasoned from the core source and the
   datasheet, with nothing measured. If it does not work, the GPS moves to a
   different pin and J1 changes.
2. The **SpiBus prediction** (that touch loses the bus to the radios) is
   likewise untested. If the fix is wrong, the arrangement changes.
3. `Spotter`'s OUI signatures come from public research, not captured
   packets.

None of those are reasons not to design the board. They are reasons to order
it after bring-up rather than before.

## Suggested first-spin scope

Two layers, 1.6 mm, 1 oz. JLCPCB or PCBWay will do five for the price of
lunch. Fit:

- J1 and the CYD pigtail
- Power tree with the separate RF rail
- Module headers at the enclosure-fixed positions
- R1, the series-resistor footprints, the decoupling
- Test points on P1's 5 V in, +3V3_RF, SCK, MISO and GPIO 1

Leave off anything not needed to prove the above. The second spin is for
what bring-up teaches you.

### Deliberately not fitted: a sub-GHz PA

The stock CC1101 transmits at +12 dBm, which this firmware already asks for:
`setPA(12)` at three sites in `subghz.cpp`, the chip's own ceiling. An Ebyte
E07-433M20S reaches +20 dBm on the same 433 MHz work from a footprint that fits
the same space, and its core is a CC1101, so every register, the whole driver
and all six SPI wires are unchanged.

Asked in September 2026 whether it would therefore just work as a swap. Most of
it would. The amplifier would not, and that is the half worth understanding
before ordering one.

**The module cannot be left to itself.** Ebyte's manual gives `TX_EN` and
`RX_EN` as active-high inputs that must be asserted before data moves, and says
they cannot both be high. Float them and the T/R switch sits in a state the
manual does not describe, with a PA and an LNA in the path that are not
enabled. So a straight swap is not "the same radio, louder". It is plausibly
quieter than the bare CC1101 it replaced, which is the worst way to find out.

**The trick that normally makes those pins free is already spent here.** The
usual way to drive a CC1101 PA costs no GPIOs at all: configure the radio's own
`GDO0` and `GDO2` as `PA_PD` and `LNA_PD` through the IOCFG registers, and the
switch follows the radio automatically. This build cannot. Both GDO pins carry
data, `SUBGHZ_TX_PIN` on GPIO 22 and `SUBGHZ_RX_PIN` on GPIO 35, handed to the
driver as `setGDO(TX, RX)` at five sites, which is what the OOK work needs them
for. That is the real obstacle, and until now this section did not mention it.

**The power objection was overstated.** This section used to give the budget as
one of three reasons. It moves the budget by 66 mA:

| | CC1101 | E07-433M20S |
|---|---|---|
| transmit | 34 mA @ +10 dBm | **100 mA @ +20 dBm** |
| receive | 16 mA | 20 mA |
| sleep | | 2 uA |
| supply | 3.3 V | 2.1 to 3.3 V, **3.6 V absolute max** |

| | now | with E07 |
|---|---|---|
| +3V3_RF worst case | 349 mA | **415 mA** |
| reflected onto P1's 5 V | 256 mA | 304 mA |
| from the bank, with the CYD | 566 mA | **614 mA** |

The +3V3_RF pour is sized for 500 mA already, the S7V8F3 is a 1 A part, and the
module's supply range covers the rail exactly as the CC1101 does. Nothing in
the power design has to change. Module figures from the E07-433M20S user
manual, v1.20.

**Three ways to do it, cheapest first.**

1. **Tie `RX_EN` high and `TX_EN` low.** A permanently enabled LNA. No GPIOs, no
   firmware change, and every receive path gains the module's sensitivity while
   every transmit path silently radiates nothing. For scanning and analysis
   that is a real option rather than a consolation prize.
2. **One GPIO and an inverter.** The two signals are complementary, so
   `TX_EN = G` with `RX_EN = !G` halves the cost to a single pin. The firmware
   then has to drive it, and the scope is bounded: 15 call sites in
   `subghz.cpp`, 9 `SetTx()` and 6 `SetRx()`, which a wrapper around those two
   covers.
3. **Free a real pair** and drive both directly. The obvious candidates are
   gone: GPIO 4 is the RGB LED's red channel, and GPIO 0 is a strapping pin that
   decides boot mode. The RGB LED gave up two of its three pins, 16 and 17;
   the third repurposed pin is 25, which was free rather than taken from
   anything, and UART0 gave up a fourth. So this means J1 grows and
   something else moves. Second-spin work,
   with the pin budget reopened.

**[verify] the direction of `GDO0` before trusting any of this.** Ebyte's pin
table lists it as an output, while this firmware drives it as an input to the
radio for asynchronous TX. Almost certainly the module passes the CC1101's pin
straight through and it is as bidirectional as the chip makes it, but a buffer
in the way would be a contention. It is a meter and five minutes.

**What other projects say about it, and how much transfers.** A set of notes on
this module reads: *powered from an independent 5V to 3.3V buck, not the CYD's
3.3 V rail, because the PA module draws too much current; same for the NRF24
E01-2G4M27SX; enable PA mode in Settings > CC1101 Module; E32R28T/E32R35T
only.*

The first two transfer completely and are the rule this board was built on
before the notes were seen. The third does not: there is no PA mode setting in
this tree, and the only thing resembling one is `setPA(12)`, which sets the
CC1101's own output power and has nothing to do with an external amplifier's
enable pins. Whatever that toggle does over there, it does not exist here.

The fourth is the one to read carefully, because `E32R28T` and `E32R35T` are
lcdwiki part numbers rather than Sunton ones, and confusing the two is exactly
how four wrong pins entered this tree. A note scoped to those boards is scoped
to hardware that is not this hardware, which is the whole reason the GPIO 4
advice had to be thrown out. See hardware.md.

**Two reasons it is still not fitted.** Transmit testing happens in a shielded
enclosure, where 8 dB buys nothing. And 100 mW at 433 MHz is well above what the
ISM allocations permit for general use on either side of the Atlantic, so the
extra power is only usable in the cage where it is not needed. Worth revisiting
if the sub-GHz work ever moves outside one.

### Deliberately not fitted: a bigger 2.4 GHz PA

Same question on the other radio, and the numbers are worse. The module here
is a generic NRF24L01+PA+LNA, budgeted at ~115 mA transmitting at +20 dBm. An
Ebyte E01-2G4M27SX reaches +27 dBm, and Ebyte give its peak transmit current
as **500 mA**.

That is not a tweak to the budget, it is a different rail:

    +3V3_RF peak, as budgeted     349 mA
    with an E01 in place of it    734 mA

And the input side is where it bites, because the supply is now a USB pin
rather than a cell. 734 mA at 3.3 V is 2.4 W out, so about 2.7 W in at 90%,
which is roughly **670 mA drawn through P1's `5V`**.

That is now a measured objection rather than an unknown. At 2.33 Ω the pin sits
at **3.0 V** under 670 mA and the path burns **1.05 W**, against 0.15 W at the
load this design actually asks for. The converter would still regulate, being
specified from 2.7 V, but a watt in a USB lead and a 1.25 mm contact is not
something to design in on purpose.

The S7V8F3 is rated 1 A stepping down, so it would carry the load on paper with
thin margin. The supply path is the part that would not.

Ebyte also ask for **at least 100 µF** of decoupling on that module against
the 10 µF specified here for the current one, which is its own evidence about
the size of the transients.

So: not fitted, and if it ever is, it reopens the converter, the input pin and
the decoupling together rather than being a module swap.

**What does carry across from any PA module is the rule this design was built
on**: an amplified radio gets its own regulated rail and never shares the
CYD's 3.3 V. That is the one piece of inherited advice that has survived every
revision, including the one that deleted the 5 V rail it originally arrived
attached to.
