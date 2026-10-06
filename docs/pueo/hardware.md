# Pueo hardware

A CYD with four external peripherals, in a custom 84 x 142 x 20 enclosure,
85 x 170 if your board has no onboard charger and the power stack has to ride
inside. See [pueo-enclosure.scad](pueo-enclosure.scad).

The reference build is the 3.5" Sunton ESP32-3248S035R, silkscreened
`ESP32-035`, on a revision carrying **both micro-USB and USB-C**.

### Is your board this one

Turn it over. Five things identify the revision everything here was measured
on, and Sunton's own back drawing in the specification PDF shows none of the
last four:

| | |
|---|---|
| `ESP32-035` | on the silkscreen, back of the board |
| **two USB sockets** | micro-USB *and* USB-C, not one or the other |
| an FFC socket | mid-board, for the panel ribbon |
| `BAT1` | a 2-pin battery connector |
| `SW1` | a button beside BAT1, which is what starts it from a cell |

The second one is the quickest. A board with a single micro-USB and nothing
else is an earlier revision, and the pin map here is not promised to hold on
it. The specification sheet's back view is that earlier board: one micro-USB,
no battery connector, no flat-flex socket. Read it for Sunton's names for each
connector and not for what is in your hand.

For the order to wire it in, and which six of the ten signals need soldering
rather than a header, see [build-guide.md](build-guide.md).


### Datasheets

Every link here was checked. The parts this design uses come first, then the two
amplifier modules that were costed and turned down.

| part | what it is here |
|---|---|
| [ESP32-WROOM-32](https://www.espressif.com/sites/default/files/documentation/esp32-wroom-32_datasheet_en.pdf) | the module on the CYD |
| [ESP32](https://www.espressif.com/sites/default/files/documentation/esp32_datasheet_en.pdf) | the SoC inside it |
| [CC1101](https://www.ti.com/lit/ds/symlink/cc1101.pdf) | sub-GHz radio |
| [nRF24L01+](https://www.sparkfun.com/datasheets/Components/SMD/nRF24L01Pluss_Preliminary_Product_Specification_v1_0.pdf) | 2.4 GHz radio, on a PA/LNA module |
| [PN532](https://www.nxp.com/docs/en/nxp/data-sheets/PN532_C1.pdf) | NFC, on an Elechouse V3 board |
| [S7V8F3](https://www.pololu.com/product/2122) | the carrier's buck-boost, with its numbers |
| [CH340](https://www.wch-ic.com/downloads/CH340DS1_PDF.html) | the USB serial bridge on the CYD |
| [MP2307](https://www.monolithicpower.com/en/mp2307.html) | the buck that was rejected, and why |
| [E07-433M20S](https://www.cdebyte.com/products/E07-433M20S) | sub-GHz PA, considered and not fitted |
| [E01-2G4M27SX](https://www.cdebyte.com/products/E01-2G4M27SX) | 2.4 GHz PA, considered and not fitted |

No manufacturer page is linked for **ST7796, XPT2046, FM5324GA, ATGM336H, TP4056, MT3608**. Their datasheets circulate as PDFs passed
between vendors rather than living on a maker's site, and the ATGM336H's reached this
project as a file rather than a URL. That is not a footnote: those are
disproportionately the parts whose figures here are marked **[verify]**, and the two
facts have the same cause.

## Connectors

Read off the board, connector by connector, on 2026-09-23.

| | Pins | Vendor's name for it | Signals |
|---|---|---|---|
| **P3** | 4 | Extended IO | `GND IO35 IO22 IO21`, all three CC1101 control lines |
| **CN1** | 4 | temperature/humidity (DHT11) | `GND IO22 IO21 3.3V` |
| **P1** | 4 | "4P 1.25 Power supply base" | `5V TX RX GND`, serial and the GPS's TX. **Pin 1 is 5V** off the silkscreen and **pin 4 is GND** by continuity, both 2026-09-29 |
| **SPEAK1** | 2 | Speak | speaker, GPIO 26 |
| **BAT1** | 2 | *(not in the datasheet)* | battery, into the onboard charger |

The middle column comes from Sunton's own datasheet, *3.5 inch ESP32
module ESP32-3248S035R/C*, Shenzhen Jingcai Intelligent, six pages, with a
labelled render of the board on page 5. It explains CN1, which this file
listed as four pins with no stated purpose: `GND IO22 IO21 3.3V` is a
four-pin DHT11 header, which is why there is a 3.3 V pin on it at all.

**The four-pin connectors are 1.25 mm pitch.** P1 carries the vendor's own
"4P 1.25" label, and a photogrammetric check of CN1 agreed with it
independently.

### BAT1 is not one of them

This file used to say "all of them are 1.25 mm pitch" and close with *Sunton's
own documentation puts SPEAK and the battery connector at 1.25 as well.*
That sentence was wrong twice. The datasheet's render shows six connectors
and **BAT1 is not among them** (nor is the USB-C socket), so it documents
an earlier revision than the board in hand and says nothing about either.
And the pitch was never measured on a two-pin connector at all.

Measured on 2026-09-24 against P1 in the same frame, which needs no scale bar
because it is a ratio: P1's four pins fall at y = 852 / 911 / 971 / 1031 for a
pitch of 60.0 px against a known 1.25 mm, and BAT1's two pads at x = 442 / 507
for 65.5 px, or 1.36 mm.

That **excludes JST PH 2.0**, which would have measured 96 px, and that is the
exclusion that matters in practice because PH is what most hobby cells ship
with. It does not separate 1.25 (+9%) from 1.50 (-9%), and a 9% scale gradient
across 500 px of a macro shot is ordinary perspective. Settle it by fit: if a
2-pin **GH** housing seats, it is 1.25.

Not a PicoBlade housing, which is the part this used to name. PicoBlade does
not seat on these headers at any pitch, so a refusal would read as "not
1.25" when it only means "not PicoBlade".

**Buy JST GH, not MX1.25 or Molex PicoBlade.** Both are 1.25 mm pitch and
they do not mate: GH latches on the side, PicoBlade on top. Listings make
this worse rather than better, because plenty of them say "PicoBlade" and
"for Pixhawk" in the same title, and those are two different parts.

Settled by fit on 2026-10-06, which is the only way it can be settled: a
PicoBlade / MX1.25 housing offered to the header does not seat, and the
pitch is right. This paragraph used to say the opposite, having gone one
step past its own evidence. The measurement established the **pitch**, and
pitch does not determine series; the series was named without a housing ever
being tried, and an order was placed against it.

**[verify] GH by elimination, not by identification.** What is known is
1.25 mm pitch and not PicoBlade, which leaves GH as the common answer and
Hirose DF13 as the legacy one. Confirm on yours by where the latch sits: GH
locks against the side of the shroud.

The useful consequence is that both cables this build needs are **4-way**,
and 4 is one of the sizes the hobby market actually stocks GH in, along with
6, 8 and 10, because those are the Pixhawk sizes. Pre-crimped is the thing
to buy. `GHR-04V-S` is the housing if you are crimping your own, with
`SSHL-002T-P0.2` contacts, but a hand crimp at 0.2 mm² that grips insulation
rather than conductor reads fine on a meter and fails under load.

**BAT1's polarity is on the silkscreen, and only half of it. The half that
is there is right.** `BAT1` sits to the left of the connector and `BAT-` to
the right, so the pin nearer the corner mounting screw is negative and the
other one is positive.

Measured on 2026-09-25 against CN1's GND pin with a continuity beep: the pin
under the `BAT-` marking is the one that conducts. The silkscreen and the
meter agree, which is worth saying because four other things this board was
supposed to be turned out to describe somebody else's hardware.

### BAT1 needs the button, and the button is on the board

**Observed 2026-09-26.** A bench supply on BAT1, at 3.8 V and at 4.25 V, both
with the output confirmed live and the voltage present at the pin: the board
does nothing and draws **0.000 A**. Plug USB in and it boots. So the path
works; what is missing is whatever starts it.

The cell tells the same story from the other side. With a 3.8 V pouch on BAT1
the board would not start either, but once USB had booted it, pulling USB left
it running on the cell.

**Resolved 2026-09-27: the button beside the BAT1 connector starts it.** Press
it with a source on BAT1 and the board comes up, no USB anywhere in the
sequence. The boost has a key. The board was never refusing, it was waiting to
be switched on, and plugging USB in was only the other way of doing that.

**It is silkscreened `SW1`.** Read off the board on 2026-09-27, which also
corrects what was written here first: RESET and BOOT are not the only buttons
on this thing. `SW1` sits beside BAT1 and is the charger's key, and a dedicated
power button is what a power-bank front end wants, so its presence is a sign
the battery path was meant to be used rather than left over.

The enclosure has to reach it. Nothing in the current model does, because until
today there was no reason for it to, and a button that starts the device is not
something to leave under a lid.

**A bench supply is not a cell, and the difference showed here.** Repeating
that last step from a supply at 3.7 V with a 500 mA limit switched the board
off the moment USB came out. At **4.0 V with a 1.5 A limit it stays running**,
so it was the limit and not the board.

### What the board actually draws

**Measured 2026-09-27, and it is the first measured figure in the entire load
budget.** Running from BAT1 with USB out, beacon spammer transmitting:

  4.00 V    0.448 A    1.787 W

That number explains the drop-out above better than the guess that followed it.
The same 1.787 W at 3.7 V is **483 mA**, which leaves about 3% of headroom
under a 500 mA limit, and the whole load transfers in one step when USB leaves.
The supply was not set wrongly by a wide margin. It was set wrongly by a hair,
and the step did the rest.

So the claim that first went in here, that the board wants more than 500 mA at
4 V, is wrong: it wants 448. The limit matters at 3.7 V, not at 4.

**At the USB socket the same work costs less**, because the boost is not in the
path there. Allowing 85 to 90% for the boost that the BAT1 path goes through,
1.787 W at the cell is about **310 mA at 5 V**. The budget had carried ~500 mA
for the CYD on WiFi TX, so that estimate was high by roughly half, on the
largest single line in it.

Read it as one sample, not a range. The backlight sat wherever the firmware
leaves it, beacon transmission is duty-cycled so this is an average rather than
a peak, and nothing here establishes the idle figure the budget also guesses
at.

That is ordinary behaviour for the part. The FM5324GA is a charger and a
power-bank boost in one, and boost stages of that kind sit disabled until
something enables them: a key press, or a load-detect event. Once enabled
they stay on, which is exactly the asymmetry seen here: it will not start from
a cell, and it will not stop when USB goes away.

**An internal-battery build is workable after all, with a condition.** For a
day this looked like the finding that sank one, on the grounds that a handheld
needing a USB cable plugged in and pulled out before it turns on is not a
handheld. A button press is a different proposition entirely: that is just how
a power bank behaves, and nobody minds. The condition is that the enclosure has
to expose the button, which the current one does not, because until today there
was no reason to.

The decision to run on USB stands regardless. It was taken for other reasons,
none of which depended on the cold-start question: runtime from a bank beats a
cell that fits, and P1's 5 V is a real input.

**EXT_CHARGER keeps a narrower version of its second reason.** That flag was
about whether the board can charge a cell; this board can. A build that wants
to start with no button at all still needs an external boost feeding 5 V into
P1, which is what EXT_CHARGER fits, because a boost with no enable pin runs the
moment its input appears.

**5 V on BAT1 does nothing, and did no harm once.** Tried on 2026-09-26 with a
bench supply: the board stayed dark, and booted normally on USB straight
afterwards. It runs from BAT1 at 3.7 V, so the path works; something between
4.2 and 5 V refuses, which is what a charger with battery over-voltage
protection does when it sees a cell out of range.

Do not read that as a rating. One survival is not a specification, the
threshold is unknown, and the protection that refused over-voltage is not the
protection that is missing. **If you want to run the board off a bench supply,
feed it 5 V through the USB-C socket**, which is the input the design uses
anyway. BAT1 wants 3.0 to 4.2 V and nothing else.

Check it on your own board anyway. The FM5324GA has no reverse protection on
its cell input, so the cost of being wrong is the charger, and a label three
millimetres from the pin it refers to is exactly the kind of thing that reads
one way in a photograph and the other way on the bench.

### P1's `5V` is the board's 5 V input node, live only while USB is

**Measured on the board on 2026-09-26, battery only, USB unplugged.**

Cell at 3.8 V, load 54.4 Ω, which draws 92 mA at 5 V:

| supply | unloaded | under load |
|---|---|---|
| **battery only** | ~4 V | **0 V** |
| **USB connected** | ~4 V | **4.75 V** |

The pin is the board's 5 V input node. It is live when USB is and dead when it
is not, and while it is live it will source current: 87 mA at 4.75 V is a
measurement of it doing exactly that. Sunton's own label said so and was
believed too late: P1 is the **"4P 1.25 Power supply base"**, and a supply base
is where you put power in. Being an input is about which side of it decides the
voltage, not about whether anything can be taken off it.

On battery the 4 V is meaningless. Unpowered, `5V` to BAT+ measures **1.3 MΩ**,
and 1.3 MΩ feeding 54 Ω divides to nothing, which is what the meter shows. A
10 MΩ meter on a megohm node reads most of the cell voltage and tells you
nothing about whether anything can be drawn from it.

On USB that 0.25 V below nominal across 92 mA suggested about 2.7 Ω of series
resistance, cable and trace rather than a junction, because a diode would have
dropped more and held it flatter.

**Measured properly on 2026-09-29: 2.33 Ω.** Two points on one USB source,
nothing unplugged between them, 4.582 V open circuit and 4.534 V across 220 Ω
at 20.6 mA. A millivolt of meter resolution on each reading puts it between
2.23 and 2.43.

The inference above was close because it was doing the same arithmetic on one
point, and it assumed the source was a round 5.0 V. It sat at 4.95 V that day,
and putting that in gives 2.33 Ω exactly. Two readings 66 mA and three days
apart agree on the resistance and disagree on the source voltage, which is the
right way round: `Voc` belongs to the charger and `R` belongs to the board.

At the 256 mA the carrier's converter draws, that puts the pin at **3.99 V**
with **0.15 W** in the path. The workings are in pcb-design.md.

**The carrier does take 5 V from here, and that became true after this was
written.** The conclusion here used to be the opposite, and it was right for
the design it was written against: the base was meant to run from a cell, and a
rail alive only while tethered to USB cannot feed the radios of a
battery-powered device.

The power tree then went the other way for reasons that had nothing to do with
this pin. The device is powered over the CYD's own USB-C, so the condition that
made this rail useless is the condition the design now runs in permanently, and
the carrier taps P1 to feed one buck-boost. See pcb-design.md.

Nothing measured here changed. What changed is what was being asked of it,
which is worth saying plainly, because a conclusion that outlives its premise
reads exactly like a fact.

**And feeding 5 V into this pin puts a second source on a node USB also
drives.** That is the designed direction (it is an input), but nothing here
knows what arbitrates the two, and the charger is on the same side of it.

#### How this was got wrong first, which is the useful part

The FM5324GA beside BAT1 is a charger *and* a synchronous boost with no-load
shutdown, so the first theory was a sleeping converter: an unloaded pin
sitting at cell voltage through the high-side FET's body diode, waking under
load. Everything fitted. It was wrong.

Two things should have killed it earlier. The forward resistance was never
asked for, "not open" was read as "low", when a diode and a megohm leak both
answer to that description, and only one of them is a diode. And the
prediction was never taken seriously enough to be checked against: a silicon
body diode passing 92 mA drops about 0.7 V, so the theory predicted roughly
3.1 V under load. It measured 0.

An unloaded reading on a high-impedance node tells you almost nothing, and a
theory that explains every observation you happen to have is not thereby
correct. The load was what settled it, and it settled it in one reading.

### What else the datasheet is good for, and where it is not

Two things in it are worth keeping. It gives the mounting holes as 94.5 x
47.9, which is a second source for the `47.90 x 94.50` the enclosure's
`SCREEN_HOLE_DX/DY` are built on. And its render has no I2C or SPI JST on it
either, which is independent support for those having been lcdwiki's rather
than this board's.

Two things in it are not. It gives the module as 101.5 x 54.9 where the
dimensioned drawing the enclosure uses says 55.50 wide, which would have made
`BEZEL_W`'s 56.0 a 1.1 mm clearance rather than the 0.5 its comment claims.
**Settled on 2026-09-24 by printing the lid: the board fits it.** Looser, not
tighter, so nothing bound, and that is why a drawing of the wrong board
(QDtech's E32R35T, lcdwiki's, not Sunton's) still produced a part that works.

And page 3 says "The display resolution is 240x320" in prose and "320X480
resolution" in the feature list four lines below it. It is the manufacturer,
but it is not careful, and it is copy-pasted from the smaller panel's
document. Weigh it accordingly.

**The serial header is P1.** This document called it P5 for a while, which is
what Sunton's smaller board calls the same connector; somebody read it off one
of those and it stood here unchallenged until a board was in hand.

Its pins run **5V, TX, RX, GND** with 5V nearest the corner mounting hole and
GND furthest from it. That order matters more than the inventory does: the
GPS's transmit line goes to the pin marked `TX`, which is the ESP32's own
UART0 transmit, GPIO 1. Reversing it puts two push-pull drivers on one net.
See "Why GPIO 1 for GPS" below for why that assignment is deliberate.

There is no `SPI` and no `I2C` JST. Those belong to lcdwiki's E32R35T, which
is where most of what this file used to say about the 3.5" came from.

## Pin map

Machine-checked by `tools/check_pinmap.py`, which resolves the macros the way
the compiler will and cross-references them against the CYD's own wiring.

| Signal | GPIO | Notes |
|---|---|---|
| VSPI SCK | 18 | shared bus |
| VSPI MOSI | 23 | shared bus |
| VSPI MISO | 19 | shared bus |
| SD CS | 5 | onboard slot |
| CC1101 CS | 21 | 27 is the backlight, see below |
| CC1101 GDO0 (TX) | 22 | P3 header |
| CC1101 GDO2 (RX) | 35 | P3 header, input-only pin |
| NRF24 CSN | 25 | see below; GPIO 4 is the RGB LED's red channel |
| NRF24 CE | 16 | was RGB LED blue |
| NRF24 IRQ | not connected | see below |
| PN532 SS | 17 | was RGB LED green, SPI mode (DIP CH1=OFF, CH2=ON) |
| GPS TX -> ESP32 | 1 | UART0 TX, the `TX` pin on P1 |
| GPS RX | not connected | |

### Second-hand pin maps name lcdwiki's boards, and this is not one

Every wrong pin this project has carried had the same shape: an lcdwiki
description applied to a Sunton board. Not one of them was a typo, and not one
was caught by reading the tree. They were caught by measuring.

| | lcdwiki E32R35T | Sunton ESP32-3248S035R |
|---|---|---|
| GPIO 4 | audio amplifier enable | **RGB LED red**, measured 2026-09-23 |
| GPIO 22 | RGB LED red | nothing |
| GPIO 34 | battery divider | **CdS light sensor** |
| breakouts | SPI / I2C / UART JSTs | **P3 and CN1**, no SPI breakout at all |

The GPIO 4 one was the expensive one. Believing it keyed an amplifier is why
NRF24 CSN was moved to GPIO 25, to avoid a hazard that does not exist on this
board.

**Where it comes from is second-hand pin maps.** CYD advice in circulation
tends to name lcdwiki part numbers, `E32R35T` for the 3.5", `E32R28T` for
the 2.8", and to close with some form of "all other pins identical". The
first half is often right about the pin it is discussing. The second half is
the trap: those two boards may well match each other, and neither of them is
this one.

One piece of inherited advice shows the shape completely. It reads, of the
same two lcdwiki boards: *NRF24 CSN moves to GPIO 26, GPIO 4 used for CC1101
PA TX_EN.* Every part of that is sound where it was written and wrong here.

The reason does not apply: a CC1101 with a power amplifier needs TX_EN and
RX_EN lines, and that design spent GPIO 4 on one. The HW-863 breakout in this
build has no PA and no such pins. The E07 upgrade discussed in pcb-design.md
runs into the same wall from the other side, and for a sharper reason than the
missing pins: this firmware already uses both GDO lines to carry data, so the
one way of driving a PA that costs no GPIOs is closed here.

And the destination is worse than the reason. **GPIO 26 on this board is the
speaker**, `BUZZER_PIN`, on DAC2. Following that note would trade a working
radio pin for a dead one and lose sound as well.

Worth noticing that the same advice may be where "do not use GPIO 4" entered
this tree at all. It was recorded here as an audio amplifier's enable, which
is lcdwiki's story; it may equally have arrived as a pin already spent on a PA
control line. Either way it produced the same result, NRF24 CSN moved to 25 to
dodge a hazard this board does not have.

**So inherited advice is a hypothesis, not a fact**, and the test is cheap.
The backlight claim came in that way and survived, because it was checked here
on the board: moving the brightness slider did nothing until GPIO 27 was
driven. The GPIO 4 claim came in the same way and did not survive the first
time anyone drove each candidate low and watched which colour came up.

Read the silkscreen before trusting a pin map, and prefer a measurement to a
part number every time the two are available.

**Which board you have matters more than it should.** "Cheap yellow display"
names boards from at least two vendors. This tree targets Sunton's
**ESP32-3248S035R**. lcdwiki's 3.5" is the **E32R35T**, a different board that
puts an audio amplifier's enable on GPIO 4 and moves the RGB LED's red channel
to GPIO 22. Nothing here has been built for it, and this image would drive
that amplifier as a chip select.

**On the Sunton board GPIO 4 is the RGB LED's red channel.** Measured on
2026-09-23, by driving each candidate low in turn (the LED is common anode,
so a pin sinks its own channel), and watching which colour came up. GPIO 4
red, 16 blue, 17 green, and 22 nothing at all. The table above said 16 green
and 17 blue until 2026-09-26: the measurement corrected the prose here and
never reached the pin map three screens up, where a reader is more likely to
look. `check_pinmap.py` had it right the whole time, which is the argument for
putting facts somewhere a machine reads them. This tree had followed the
lcdwiki datasheet for months.

Until then this section said GPIO 4 was an **audio amplifier's enable** on the
3.5" with RGB red moved to 22. That is true of lcdwiki's E32R35T and of
nothing in this build. The pin map had been following a datasheet for a board
nobody here owns.

It cost less than it might have. **NRF24 CSN is 25**, and the reason it was
moved off GPIO 4, that keying an amplifier enable at chip-select rates
clicks and draws off the display's rail, was about a hazard that is not
there. It stays on 25 anyway: 25 is free, the assignment is published and
built against, and what reverting would buy is one pin.

25 is free because this board hangs its XPT2046 off the display's SPI behind
TOUCH_CS rather than giving touch its own bus at 25/32/39, which is what the
stock CYD profile assumes and why that profile puts a chip select there.

Two more pins Pueo does not drive: GPIO 34 is a CdS light sensor and GPIO 36
is the touch IRQ.

**The lcdwiki 4.0" E32R40T is pin-identical to the 3.5"** on every line
lcdwiki publishes, at the same 320x480, differing only in the controller's
suffix, which TFT_eSPI covers with one driver. The 3.5" image should run
on it unchanged. Untried here: two datasheets agreeing is a reason to
expect it to work, not a report that it did. GPIO 26 is the
amplifier's DAC output rather than a plain speaker pin.

The onboard RGB LED is gone on both. GPIO 4/16/17 are the only contiguous spare pins on
this board, and three radios need six lines.

## CC1101 CS is 21, and the backlight is why

The stock CYD profile puts `CC1101_CS` on GPIO 27. On this board 27 is the
backlight, so driving the chip select would dim the screen. 21 is free here
for the same reason: the two pins swap roles between Sunton's 3.5" and their
2.8", and this is the larger one.

It is a small bonus rather than a compromise. 21 is on the Expand IO header
(P3: GND, IO35, IO22, IO21) beside GDO0 and GDO2, so all three CC1101 control
lines reach a connector instead of a pad.

`tools/check_pinmap.py` reads the backlight pin out of `User_Setup cyd.h`
rather than assuming it, so putting the select on the wrong one fails the
build.

That was only half the job, and the other half went missing for the whole
life of the 3.5" port. `User_Setup cyd.h` branches `TFT_BL` per panel, 21
and 27, and TFT_eSPI drives it HIGH at `begin()`. But `shared.h` also has
`BACKLIGHT_PIN`, which the sketch attaches a PWM channel to for the
Brightness setting, and it kept the `BOARD_CYD` default of 21 on both
panels. `board_pueo.h` argued in a comment that 27 was the backlight on the
3.5", and moved `CC1101_CS` onto 21 on that basis, without ever saying
it in code.

So on the 3.5" build, `BACKLIGHT_PIN` and `CC1101_CS` were both GPIO 21, and
this script printed "no collisions" because it read `TFT_BL` and never
compared it to the pin the sketch drives. Neither half failed loudly:
TFT_eSPI lit the real pin so the screen worked, and nothing was soldered to
CC1101 so its select never toggled. The only symptom was Brightness doing
nothing, which is what PWM into an unconnected pad looks like. It now checks
`BACKLIGHT_PIN == TFT_BL` for each panel.

## Why GPIO 1 for GPS

It looks backwards: GPIO 1 is the ESP32's UART0 *transmit* pin, and the GPS is
also transmitting. The alternative is worse.

GPIO 3 (UART0 RX) is driven by the USB-UART bridge's TX output. Tying the GPS
module's TX there puts two push-pull drivers on one net, and they will fight
whenever the bridge is enumerated. GPIO 1 runs the other way: the ESP32 drives
it and the bridge's RX merely listens, so the bridge never contends. Once the
ESP32 releases the pad, the GPS is the only driver on the net.

Releasing it is the part that needs firmware. `gpsPortOpen()` in `gps.cpp`
tears down UART0, resets the pad to a plain input, and only then lets UART2
claim GPIO 1 as its RX. `gpsPortClose()` puts the console back. Every
`gpsSerial` begin/end in the tree goes through that pair, so the handover
cannot be skipped at one call site and silently half-work.

**USB serial logging is dead while a GPS feature is open.** That is inherent to
this wiring, not a bug. It comes back when you leave the feature.

## NRF24 IRQ and the GPIO 17 question

The handoff listed NRF24 IRQ and PN532 SS both on GPIO 17. Resolved in favour
of the PN532: upstream never reads the NRF24 interrupt line. There is no IRQ
pin macro anywhere in the tree and no `whatHappened()` or `maskIRQ()` call; the
NRF24 path is polled throughout. Leave the pin off the board.

## One NRF24, three radio objects

The multi-channel BLE jammer modes were written around three radio objects;
since 0.2.2 they go through `Nrf24Raw`'s free functions instead. Pueo has one
module, so `CE_PIN_2`/`_3` and `CSN_PIN_2`/`_3` are aliased onto the same pads
as `_1`. Those modes will run
degraded on a single radio rather than failing to build. They are outside the
initial feature set anyway.

## Bugs in upstream's stock CYD profile

Running the checker against `BOARD_CYD` as shipped turns up six collisions:

```
GPIO 22  CC1101 GDO0  vs  NRF24 #2 CE
GPIO 25  PN532 SS     vs  NRF24 #3 CSN
GPIO 25  both of those vs the XPT2046 touch clock
GPIO 27  CC1101 CS    vs  NRF24 #2 CSN
GPIO 35  CC1101 GDO2  vs  GPS RX
```

The GPIO 25 pair is the interesting one: anyone wiring a PN532 to a CYD by
upstream's defaults lands the chip select on the touchscreen's clock line.
Worth reporting upstream.

## CC1101 TX/RX orientation

The handoff flagged this as the first thing to fix. It is already correct on
upstream main. `subghz.cpp` calls `setGDO(CC1101_GDO0, CC1101_GDO2)`, which
resolves to `setGDO(22, 35)`, GDO0 on the output pin, GDO2 on the input-only
pin. RCSwitch's `enableTransmit`/`enableReceive` agree. Nothing to change.

GPIO 35 being input-only is a useful accident here: the assignment physically
cannot be made backwards without the transmit path silently failing.

## SPI bus

Done, and it turned out to be five devices rather than four: the XPT2046 touch
controller is on the same peripheral as the radios and the SD card, and on a
CYD it is the only input device. The conflict that matters is the GPIO matrix
rather than the clock. MISO can only be sourced from one pad, and touch reads
GPIO 39 while everything else reads GPIO 19.

`SpiBus` now owns the bus. See [spi-bus.md](spi-bus.md).
