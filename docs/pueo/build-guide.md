# Building a Pueo by hand

Wiring a CYD and four modules into a working unit, in the order that makes a
fault easy to find.

**Nothing in this guide has been wired yet.** Step 2 is done (a board was
flashed and run on 2026-09-20, and the display, the menus, touch, the WiFi
scanner, the packet monitor, Surveillance and Hunt all work, and the submenu
grid was tried on 2026-10-03 with every tile launching the feature it names),
but no module has
been soldered to anything, so steps 3 onward remain untested. The rest is
derived from the pin map in [hardware.md](hardware.md), which
`tools/check_pinmap.py` verifies against each board's own wiring, from the
CYD schematic, and from module datasheets. Treat the order as reasoned and
the timings as untested.

**This is for the 3.5" Sunton ESP32-3248S035R and nothing else.** The 2.8"
ESP32-2432S028R was supported up to 0.4.13; its image was built every release
and never booted. Wiring instructions for a board nobody has run are worse
than none, so they are gone with it.

**The 3.5" connector table was rewritten on 2026-09-23** against a photograph
of the actual board, which is Sunton's and has Sunton's two breakout headers
rather than the four JSTs this guide had been promising off an lcdwiki
render. It went from three solder joints to six. The same look found the
backlight sitting on GPIO 27 while the sketch had been driving 21, which is
why the Brightness setting had never worked on that panel, and is fixed.

There is no PCB. [pcb-design.md](pcb-design.md) is design input for one, and
it deliberately comes *after* this: a board freezes a pin map that bring-up
can still move.

## Read this part before you buy anything

**The radios do not share the CYD's 3.3 V regulator.** The NRF24L01+PA+LNA
pulls on the order of 115 mA on transmit and the CC1101 around 34 mA, against
a regulator already carrying an ESP32, a backlight and a display controller.
Sharing it browns out the main rail, and a brownout on an ESP32 looks like a
random reboot, a corrupt SD write, or a touch controller that has stopped
answering: that is, like four different bugs rather than one power problem.

Two rails, from one 5 V bus:

```
  5 V ──┬── CYD onboard regulator ── 3.3 V ── ESP32, display, touch, SD
        │
        └── separate buck ────────── +3V3_RF ── CC1101, NRF24, ATGM336H
```

Common ground between them, and **10 µF across the NRF24's supply pins at the
module**, not at the buck. The PA module's current step is fast enough that
the wire between the two is an inductor.

On the bench you can substitute a lab supply set to 3.3 V for the buck. What
you cannot substitute is the separation.

The PN532 runs from 5 V, not from either 3.3 V rail.

## Parts

| | |
|---|---|
| Base | CYD, either panel, see below |
| Sub-GHz | CC1101 on an HW-863 breakout, 300–439 MHz, board SMA |
| 2.4 GHz | NRF24L01+PA+LNA, board SMA |
| NFC | PN532 V3, **SPI mode, DIP CH1=OFF, CH2=ON** |
| GPS | ATGM336H, 9600 baud, IPEX with an active antenna |
| Power | 1S LiPo, TP4056 with protection, MT3608 boost to 5 V, buck for +3V3_RF |

Antennas on both radios before power. A PA module transmitting into an open
SMA is a module you replace.

### "Cheap yellow display" names at least four boards

Two vendors, and they do not agree about GPIO 4. Get this wrong and the
NRF24's chip select lands on an audio amplifier. Only the second column is
this build's board; the rest are here to be told apart from it.

| | Sunton **ESP32-3248S035R** | lcdwiki **E32R35T** | Sunton **ESP32-2432S028R** | lcdwiki **E32R28T** |
|---|---|---|---|---|
| | **this build** | not this | dropped after 0.4.13 | never supported |
| Size | 3.5" | 3.5" | 2.8" | 2.8" |
| Driver | ST7796 | ST7796 | ILI9341 | ILI9341V |
| RGB LED | **4** / 16 / 17 | **22** / 16 / 17 | **4** / 16 / 17 | **22** / 16 / 17 |
| GPIO 4 is | the LED's red | **amp enable** | the LED's red | **amp enable** |
| Backlight | **27** | 27 | 21 | 21 |
| Breakouts | **P3, CN1, P1** | SPI / I2C / UART JSTs | P3, CN1, P5 | 1.25 mm JST |
| Outline | n/a | 55.50 × 101.50 | ~56 × 92.5 mm | 50.00 × 86.00 |

**The 3.5" board this was built against is Sunton's, silkscreened
`ESP32-035`**. The short form of `ESP32-3248S035R`, which is the part the
code has named all along. The panel identification was never wrong. What was
wrong was this guide's description of its *connectors*: it has the same two
breakouts as Sunton's 2.8", P3 and CN1, and none of lcdwiki's `SPI` /
`I2C` / `UART` JSTs, which the table below used to promise.

It has a `SPEAK1` connector, a two-pin 1.25 mm JST for an 8 ohm speaker
driven from GPIO 26. DAC2, the same pin Sunton's 2.8" uses. It also has
`BAT1` for a battery, into a charger already on the board.

**GPIO 4 on it is the RGB LED's red channel**, measured on 2026-09-23 by
driving each candidate low in turn. The LED is common anode, so a pin sinks
its own channel. GPIO 4 red, 16 blue, 17 green, 22 nothing. Both Sunton
boards agree, and the "two vendors disagree about GPIO 4" warning above is
about lcdwiki's parts rather than about the two panels this tree builds for.

**Pueo's builds target Sunton boards**, where GPIO 4 is the LED's red channel
and spending it is the trade this pin map makes. On any lcdwiki E32 board that
pin is an amplifier's enable, and either image would key it at chip-select
rates. The 3.5" image puts `NRF24 CSN` on GPIO 25 rather than 4, which was
done to dodge an amplifier that is not on this board; 25 is free there and it
stays, but the reason in the commit is not the reason in the hardware.

**The lcdwiki 4.0" E32R40T should run the 3.5" image unchanged.** Every pin
lcdwiki lists for it is the same as the E32R35T's (display SPI on
14/13/12 with CS 15 and DC 2, touch on that bus behind CS 33 with IRQ 36,
backlight 27, SD on 5/18/23/19, RGB on 22/16/17, amplifier on 4 and 26,
battery on 34), and the resolution is the same 320x480. The only entry
that differs at all is the controller's suffix, ST7796S against the 3.5"'s
ST7796U, and TFT_eSPI drives the family with one `ST7796_DRIVER`.

So the firmware needs nothing: flash `pueo-<version>-merged.bin`, which is
the 3.5" image and selects nothing this board wants differently.

One exception, as of 2026-09-23: **sound would not work on it.** The 3.5"
image used to assert GPIO 4 as an audio amplifier's enable, which is exactly
right for an lcdwiki board and was doing nothing but blinking an LED on the
Sunton one it is actually built for. That assert is gone. On an E32R40T the
amplifier would now never be enabled. A one-line fix for anyone who has one,
and a good illustration of why the pin map should not be carrying a second
board's datasheet in the first place.

What does not carry over is the enclosure. The 4.0" is 60.88 x 111.11 x
5.65 mm against the 3.5"'s 55.50 x 101.50 x 5.80, and there is no outline
drawing here for it (no aperture, no module outline, no mounting-hole
pattern), so a lid for it would be guesswork. [verify] and measure before
cutting one.

None of this has been on a 4.0" board. It is two datasheets agreeing, which
is a good reason to expect it to work and not the same thing as it working.

### The board this tree builds for

| | 3.5″ ESP32-3248S035R |
|---|---|
| Display | ST7796, 320×480 |
| Touch | XPT2046 on the display's SPI, CS 33 |
| Backlight | GPIO 27 |
| RGB LED | 4 / 16 / 17 |
| GPIO 4 is | the LED's red channel |
| GPIO 34 is | a CdS light sensor |
| Flash image | `pueo-<ver>-merged.bin` |

**That filename changed meaning.** Up to 0.4.13 the plain name was the 2.8"
image and this board's carried a `-35`. From the next release the plain name
is this board and there is no other.

The 3.5" figures are from lcdwiki's E32R35T page and QDtech's outline drawing
(V1.0, 2024-08-14): PCB 55.50 × 101.50 × 5.80 mm, corners R3.50, four 3.20 mm
mounting holes on a 47.90 × 94.50 pattern, 5.09 mm of SMD standing off the
back. The enclosure is dimensioned from those numbers.

There is no runtime detection. The image is built for one panel and the wrong
one is a dark screen rather than an error message.

## Tools

An iron with a small **chisel** tip, 1.0-1.6 mm; gel flux; thin solder, leaded
63/37 if you have it; 30 AWG stranded silicone wire; kapton tape; tweezers; a
multimeter with a continuity beep; solder wick; and magnification with light
on it. Six of the ten joints land on the ESP32 module's castellations, at
1.27 mm pitch with pads either side you must not bridge. The continuity beep
is not optional. It is how you tell which pad you are on.

Stranded silicone rather than solid Kynar wire-wrap wire. Kynar places more
easily because it stays where you put it, and solid wire work-hardens and
cracks at the joint under vibration. This is a handheld that gets carried.

## Soldering to a castellation

Six joints at 1.27 mm is not difficult work, but almost none of what makes it
go well is dexterity.

**Flux is the whole game.** Gel flux, no-clean, on the pad before the iron
goes near it. Most of "I cannot solder things this small" is really "I
soldered without flux". The rosin core in the solder is not enough for a
joint you are reflowing rather than making from scratch.

**Tin the wire off the board.** Strip about 1.5 mm, tin it, then trim the
tinned end back to roughly 1 mm. A short stiff pre-tinned stub; a long one
levers the joint every time the wire moves. Now the joint itself is a
one-second touch instead of a three-handed juggle.

**Use the solder that is already there.** These castellations were reflowed
at the factory and carry a fillet. Flux, the tinned wire laid into it, one
second of heat, usually with no added solder at all. Feeding solder off the
roll while making the joint is exactly how the blob that bridges to IO5
happens.

**A small chisel tip beats a needle point.** The instinct is to reach for the
finest tip in the drawer, then no heat gets into the joint, so you dwell, and
dwell time is what lifts pads. Contact area is what you want.

**330-350 C, in and out.** Hot and quick is gentler on the board than warm and
slow. Leaded 63/37 is far more forgiving here than lead-free.

**Tape the wire down before soldering it.** Kapton, a few millimetres back.
Both hands free and the wire cannot spring away mid-joint. This matters more
than any tool on the list above.

**Strain relief, or the joint fails later.** 30 AWG on a castellation is a
cantilever. Anchor the wire to the board within about 5 mm of the joint,
hot glue, UV resin, more kapton. The joint that survives the bench is the one
that fails in a bag.

**Beep after every joint, not at the end.** Specifically, after IO18 and after
IO17, check each against IO5 between them. A bridge found immediately tells
you which joint made it. A bridge found five joints later has you reworking
blind, next to your own work, on the pads with the worst neighbours.

## The ten signals

All ten are drawn in [wiring.svg](wiring.svg): the board on the left with its
solder pads and its two useful headers, the four modules on the right, green
for what reaches a connector and orange for what has to be soldered. The three
shared bus lines are one spine there rather than nine wires, which is what they
are electrically.

**Six joints, on the board this was built against.** An earlier version of
this table said three, because it described lcdwiki's E32R35T and the board
in hand is Sunton's `ESP32-035`. Read the section under the table before you
trust it.

| Signal | GPIO | Sunton `ESP32-035`, the 3.5" |
|---|---|---|
| CC1101 CS | 21 | **P3**, `IO21` |
| CC1101 GDO0 (TX) | 22 | **P3**, `IO22` |
| CC1101 GDO2 (RX) | 35 | **P3**, `IO35` |
| GPS TX → ESP32 | 1 | **P1** JST, `TX` |
| VSPI SCK | 18 | module pad 9R, **solder** |
| VSPI MOSI | 23 | module pad 2R, **solder** |
| VSPI MISO | 19 | module pad 8R, **solder** |
| NRF24 CSN | 25 | module pad 10L, **solder** |
| NRF24 CE | 16 | module pad 12R, **solder** |
| PN532 SS | 17 | module pad 11R, **solder** |

Not connected, deliberately: **NRF24 IRQ** (nothing in the tree reads it; the
driver polls) and **GPS RX** (the module only ever talks).

**The SPI bus is not brought out, but do not solder to the microSD slot.** Earlier versions of this guide sent you to the card slot's own
pins for SCK, MOSI and MISO, and called those three joints the whole
difficulty of the build, because the slot still has to work afterwards,
Spotter's capture log and the wardriver both write to it.

They reach the slot *through the ESP32*, so the module's own castellated pads
are the same three nets with none of that risk. They are 1.27 mm pitch with
the board pad extending clear of the module body, against eight spring-contact
terminations hard up against a grounded shell. A bad joint on a module pad
costs you a retry; a bad joint on the slot costs you the slot.

Five of the six land on the module's right-hand edge. Only NRF24 CSN is on the
other side, and on the 3.5" that is GPIO 25, pin 10 down the left.

### Finding the pad

[solder-pads.svg](solder-pads.svg) draws the table below: the module with its
antenna up, the six targets in orange, and the five pads that end the board
marked. Hold the board upright with the back toward you and the USB-C socket at
the bottom, and the antenna is already pointing up.

Counting castellations is how you break something. Start at the top of the
right-hand edge with the antenna up and count down.

**That edge has fifteen pads, not nineteen.** The module is the usual
WROOM-32: fifteen down each long edge and eight across the bottom. So the count
reaches 15 at the bottom-right corner, **turns it**, and 16 to 19 run leftwards
along the bottom edge. All six targets are at 15 or below, so none of them is
around that corner. Three of the pads that end the board are.

```
   1 GND       6 IO21      11 IO17  <- PN532 SS       16 IO15
   2 IO23 <-   7 NC        12 IO16  <- NRF24 CE       17 SD1   !!
   3 IO22      8 IO19 <-   13 IO4                     18 SD0   !!
   4 TXD0      9 IO18 <-   14 IO0    !!               19 CLK   !!
   5 RXD0     10 IO5   !!  15 IO2
```

The last four in that list are the ones on the bottom edge.

Three ways to lose a board there. **IO5 sits directly between IO18 and IO17**
and is the SD chip select. Bridge it and you have broken the card slot from
the one direction you were trying to avoid. **SD1, SD0 and CLK** at the bottom
are the module's internal flash; bridge those and it will not boot. **IO0** is
the boot strap.

So identify by continuity, not by position. SCK, MOSI and MISO share a net
with the microSD slot, so beep from a candidate module pad to the slot pin:
the right pad beeps and the wrong one does not. You use the slot to *find* the
pad without soldering to it. IO16 and IO17 are the RGB LED's blue and green
channels on these boards, so beep those to the LED's cathodes.

If you would rather not probe blind, drive the pin low from firmware and sweep
the edge with a meter on DC volts. The pad reading 0 V is the one. That is
how GPIO 4/16/17/22 were identified in the first place.

**What the 3.5" does give you is all three CC1101 lines on one connector.**
P3 carries `GND IO35 IO22 IO21`, which is chip select, GDO0 and GDO2 with a
ground beside them. That is the pay-off from putting `CC1101_CS` on 21 on
this panel, and it is the reason the guide's older 3.5" column had GDO0 on a
solder pad: it was describing a different board.

`CN1` is a second four-pin connector, `GND IO22 IO21 3.3V`. It duplicates two
of P3's signals and adds 3.3 V, which makes it useful for powering something
small rather than for a new signal.

**The 3.5" board also has an audio amplifier**. There is a two-pin `SPEAK`
connector on it. Nothing in the pin map touches the amp, and `NRF24 CSN` is
25 on this panel rather than 4 for exactly that kind of reason, but it is
worth knowing it is there before you go looking for spare pins.

### [verify] Read your own silkscreen before you cut a wire

The table above was read off a photograph of the actual board, connector by
connector. The version before it was not, and it was wrong twice.

Two lessons out of that. The first is that "cheap yellow display" names at
least four boards, and a render of one is not a description of another. The
second is that this guide and [hardware.md](hardware.md) disagreed about where
GDO0 lands. Hardware.md had P3 and was right, and neither of them noticed,
because prose does not get checked the way the pin map does.

**The serial connector is P1**, beside the USB-C socket, carrying
`5V TX RX GND` with 5V nearest the corner mounting hole. This guide called it
P5 for a while, which is what Sunton's smaller board calls its equivalent.

Get that order off your own silkscreen before you crimp. The GPS's transmit
line goes to the pin marked `TX` (the ESP32's own UART0 transmit, GPIO 1),
and the reasoning for that is in [hardware.md](hardware.md). Reversed, it puts
two push-pull drivers on one net.

The four-pin connectors (P3, CN1 and P1) are **1.25 mm pitch**. P1 carries
that label on Sunton's own drawing, and a photogrammetric check of CN1 agreed
with it independently.

**SPEAK1 and BAT1 are two-pin and were never measured.** BAT1 reads 1.36 mm
against P1 in one frame, which rules out JST PH 2.0, the connector most
hobby cells ship with, but does not separate 1.25 from 1.50. See
[hardware.md](hardware.md). Check by fit before you rely on it.

**Ask for MX1.25 or Molex PicoBlade. Do not buy JST GH.** Both are 1.25 mm
pitch and they will not mate: GH latches on the side, PicoBlade on top. GH is
the [Pixhawk connector standard](https://github.com/pixhawk/Pixhawk-Standards),
so it dominates listings aimed at drone builders, and plenty of those say
"PicoBlade" and "for Pixhawk" in the same title. Searching `MX1.25 2P` and
leaving "Pixhawk" out of the query drops the GH parts from the results.

Do not crimp 1.25 mm yourself. It needs the proper tool, and a bad crimp is
an intermittent you will chase for hours. Either buy assembled pigtails, or
buy a kit of **pre-crimped wires with loose housings**, which is better here:
the terminals push in with tweezers, so you choose the polarity at assembly
rather than discovering it afterwards. Push each one until it clicks and then
tug the wire. A terminal seated on the lip instead of behind the barb backs
out under vibration.

## Order of work

The bus is shared, so a bad joint on SCK, MOSI or MISO breaks every device on
it at once. Build outward from the things that can be tested alone.

**1. Power, with nothing else attached.** Bring up the 5 V bus and the
+3V3_RF rail and meter both before anything is connected to them. Confirm
common ground. A supply that is wrong here damages modules later.

Identify BAT1's polarity here too, before a cell is anywhere near it. The
silkscreen marks `BAT-` on one side and nothing on the other, so beep each
pin against a known ground: the one that conducts is negative. CN1's GND pin
is a convenient reference and is right next to it.

On the board this was written against the marked pin is the one that
conducts, so the silkscreen is telling the truth. Confirm it on yours
regardless. The FM5324GA has no reverse protection on its cell input, and
the colours on a pre-crimped pigtail tell you about the pigtail rather than
about the board.

**2. Flash the stock firmware and boot the bare CYD.** Display, backlight and
touch all work before you have introduced a single joint of your own. If the
boot screen and menu come up and touch responds, you have a known-good
starting point, and you will want one.

```bash
esptool.py --chip esp32 -b 921600 write_flash 0x0 pueo-0.4.40-merged.bin
```

`-b 921600` because esptool defaults to 115200, and the write is about 19
seconds at the fast rate against two and a half minutes at the slow one.
The rate changes the upload and not the result: esptool verifies a SHA-256
of every region it writes, so a rate the cable cannot carry fails during
the handshake rather than corrupting anything. If it will not sync, try
`-b 115200` before suspecting the board.

Add `-p COM7`, or whichever port the board came up on, if esptool does not
find it by itself.

There is one image and it is the 3.5" one. Up to 0.4.13 that plain filename
meant the 2.8" board and the 3.5" carried a `-35` suffix; it is the other
way round now, and there is no 2.8" image at all. `pueo-X.Y.Z-beacon-35-merged.bin`
is the bench transmitter rather than Pueo, so it is not what you want here.

**3. The three bus lines, then the SD card.** Solder SCK, MOSI and MISO to
the ESP32 module's pads (9R, 2R and 8R) identifying each by beeping it
against the matching microSD slot pin first. Then insert a card and confirm it
still mounts.

Testing the bus with the one device already wired to it isolates your work
from everything that follows. The card is also the check on the joints
themselves: it shares all three nets, so if it still mounts, all three are
sound, and if it does not, you have three suspects and no other variable.
Nothing here touches the slot, so a card that stopped mounting means a bridge
on the module. Look at IO5 first, which is the SD chip select and sits
between two of the pads you just worked on.

**4. CC1101.** Four wires, three of them to headers, plus power and ground
from +3V3_RF. Then the jamming detector: activity on screen is enough to say
the bus and the chip select both work.

**CSN is GPIO 25, not GPIO 4.** Not because GPIO 4 is dangerous. It is the
RGB LED's red channel on this board, measured rather than read off a
datasheet. The assignment came from believing 4 was an audio amplifier's
enable, which is true of lcdwiki's E32R35T and not of this one. It stays on 25
because 25 is free and the map is published. The firmware already picks 25;
what this guide cannot do is solder the wire to the right pad for you. 25 is
not on a header, and it is free here only because this board's touch
controller shares the display's SPI rather than taking a bus of its own, so
it still needs finding on the silkscreen.

**5. NRF24.** CSN and CE to the RGB LED pads, power from +3V3_RF, and the
10 µF at the module. The channel scanner should show a populated 2.4 GHz
band in any occupied building; a flat sweep means the module is not answering
on the bus.

**6. PN532.** SS to the last RGB LED pad, power from 5 V. **Set the DIP
switches to SPI before wiring it**: CH1=OFF, CH2=ON. In the wrong mode the
module is silent and looks like a bad joint.

**7. GPS.** One signal, module TX to GPIO 1, plus power from +3V3_RF and an
active antenna with sky view. Give it minutes, not seconds, for a first fix.

**Do not take its VCC from P1's 5V pin.** That pin sits on the same four-way
connector as the TX line you are wiring, one position away, which is the whole
reason this needs saying. The ATGM336H's **absolute maximum VCC is 3.6 V**
(a destruction limit, not a recommendation), and P1 carries 5 V whenever USB
does.

Wiring diagrams in circulation show GPS `VCC` going to `VIN` on this connector,
and they are right about the module they describe: a GT-U7 or NEO-6M breakout
carries its own regulator and takes 5 V happily. The ATGM336H breakout used
here has not been shown to. **3.3 V is the safe answer either way**, because a
breakout with a regulator will run from it too, and one without passes it
straight to a part that wants exactly that.

Leave the module's RX open as well. Those same diagrams land it on GPIO 3,
which is UART0 receive and is driven by the USB-UART bridge, so console
output would arrive at the GPS's command input, and that input accepts
configuration commands.

Test after each module rather than at the end. Five devices share VSPI; the
failure you are trying to avoid is one intermittent joint that presents as
four unrelated faults.

## Things that look like faults and are not

**USB serial goes dead while a GPS feature is open.** GPIO 1 is UART0's
transmit pin. The firmware hands the pad to the GPS on entry and takes it
back on exit (`gpsPortOpen`/`gpsPortClose` in `gps.cpp`); the console cannot
exist at the same time. It returns when you leave the feature. This is
inherent to the wiring, and the reasoning for choosing that pin over GPIO 3
is in [hardware.md](hardware.md).

**The RGB LED does nothing.** It is gone: GPIO 4, 16 and 17 are the NRF24's
and the PN532's now.

This paragraph used to say the red channel was GPIO 22 here, so the LED would
flicker with sub-GHz traffic rather than go dark. That is lcdwiki's E32R35T.
On this board, measured on 2026-09-23, GPIO 4 is red, 16 is blue, 17 is green
and 22 is nothing at all.

The LED is spent because those were the only contiguous spare pins on the
board and three radios needed six lines. On the 3.5" the amplifier on GPIO 4
is *not* spent the same way: NRF24 CSN moved to 25 rather than the pin map
growing to cover it, because keying an amplifier at chip-select rates is not
the same kind of trade as losing an LED.

**CC1101 receive cannot be wired backwards.** GDO2 is on GPIO 35, which is
input-only. If you swap GDO0 and GDO2 the transmit path fails rather than
silently half-working, which is a useful accident.

## Things that are faults

**Touch stops responding after a radio is used.** The touch controller reads
MISO on GPIO 39 while everything else reads GPIO 19, and only one pad can
drive that input at a time. `SpiBus` re-points the matrix on every handover.
If this appears, it is a real bug and worth reporting with the feature you
were in, see [spi-bus.md](spi-bus.md).

**Flakiness that follows CC1101 use.** LSatan's driver calls `SPI.end()`
after every register access, which resets the whole peripheral, including
for the SD card and the touch controller that are also on it. Pueo works
around this, but if the symptom returns it points here first.

**Random reboots under transmit.** Power. Go back to the two rails.

## After it works

`tools/check_pinmap.py` will tell you whether the map you built matches the
one the firmware expects, which is worth running once even though it checks
the source rather than your soldering.

The enclosure is dimensioned around these modules and can be printed while
you build. The STLs are published at
[pueo.magikh0e.pl/enclosure.html](https://pueo.magikh0e.pl/enclosure.html);
the OpenSCAD they are rendered from is not, and is not in this archive.
