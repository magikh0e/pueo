<p align="center">
  <img src="../img/pueo-guide.webp" width="100%"
       alt="The Pueo mark: a stylised horned owl in green on near-black, with
            a WiFi arc, a Bluetooth rune and a satellite across its chest and
            the word PUEO beneath, in a sweep of concentric signal arcs.">
</p>

# Using Pueo

This is the operating manual. It assumes you have a working unit: firmware
flashed, modules wired, and a screen that responds when you touch it. If you
are still building one, [build-guide.md](build-guide.md) is the other
document and it ends where this one starts.

> **Everything here is for equipment you own or have written permission to
> test.** Several of these tools transmit, jam, or impersonate, and doing
> that to other people's equipment is illegal in most places. Nothing in
> this guide changes that.

---

## Contents

- [The screen](#the-screen)
- [Getting around](#getting-around)
- [The menu](#the-menu)
- [What needs what](#what-needs-what)
- [The SD card](#the-sd-card)
- [Settings](#settings)
- [Stealth mode](#stealth-mode)
- [When something says it is not there](#when-something-says-it-is-not-there)

---

## The screen

Power on and you land at the main menu. Across the top is the status bar,
and it stays there in almost every feature.

| | |
|---|---|
| battery | percentage, from the cell on BAT1. Reads 0% with no cell fitted |
| version | the firmware you are running, for example `Pueo 0.4.13` |
| Bluetooth | lit when the BLE radio is up |
| signal | WiFi activity |
| satellite | lit when the GPS has a fix |
| temperature | the ESP32's internal sensor, not the room |
| SD | one icon for a card present, a different one for none |

The SD icon is the one worth glancing at before you start anything that
captures. It is the difference between a session you can read afterwards and
one you cannot.

---

## Getting around

**The screen is the input.** Touch a tile to open it. Inside a feature, a
row of buttons along the bottom does the work, and the labels change with
the feature. Common ones are `Back`, `Exit`, `Save`, `Next` and `Prev`.

**`Back` and `Exit` both leave**, and they are not always in the same place,
so read the row rather than aiming from memory.

**Every menu is one screen.** A submenu is a grid of tiles, fifteen slots,
and the largest menu uses twelve of them. There is no second page and no page
button: if a feature is not on the screen in front of you, it is not in that
menu. Back is the bar along the bottom, and the whole strip below the tiles
is part of it.

---

## The menu

Eight tiles, two columns of four, in column order:

```
WiFi        Bluetooth
NRF24       SubGHz
Detect      RFID/NFC
GPS         System
```

### WiFi

Page one: **Packet Monitor**, **Beacon Spammer**, **WiFi Deauther**,
**Probe Request Flood**, **Deauth Detector**, **WiFi Scanner**.

Page two: **Captive Portal**, **Hidden SSID Revealer**, **WPS Scanner**,
**ARP Scanner**, **Karma Attack**, **AP Tracker**.

**AP Tracker** is the one that behaves differently from the rest: pick an
access point from a scan and it locks to that channel and shows a signal
gauge that rises as you get closer. It is for walking a signal down rather
than reading a list.

**Beacon Spammer** broadcasts a list of network names. By default that is a
list built into the firmware, which is the same in every copy. Put your own
in `/pueo/ssids.txt` on the card and it uses those instead, and the screen tells
you which list it loaded when it starts.

### Bluetooth

Page one: **BLE Jammer**, **BLE Spoofer**, **Sour Apple**, **AirTag
Spoofer**, **AirTag Sniffer**, **Sniffer**.

Page two: **BLE Scanner**, **BLE Rubber Ducky**, **Skimmer Detect**,
**Hunt**, **Fast Pair**.

**BLE Scanner** lists what is advertising. Open a row for the detail view:
address, signal, the manufacturer data, what the appearance field says the
device claims to be, and the services it advertises, named where they are
standard ones.

**Info** in the detail view connects to that one device and reads its Device
Information Service, which is where a device publishes its manufacturer,
model, serial number and its firmware, hardware and software versions. It
connects to the device you have open and nothing else, and it only reads.

It is the only thing in the scanner that connects to anything. The scan
itself is not silent either: a BLE scan asks every advertiser it hears for
its name unless stealth mode is on. A connection is louder than asking,
which is why this one needs a button. Not every device publishes
one; when there is none you are told that, separately from a device that
would not accept the connection at all.

Use it when you want the version rather than the name. An advertisement says
what something is, never which build it is running, and which build is the
question behind whether it has been patched.

**Hunt** is the Bluetooth equivalent of AP Tracker: choose a device and walk
the gauge. **Skimmer Detect** looks for the BLE signatures card skimmers are
known to advertise.

**BLE Rubber Ducky** needs an ESP32-S3 and will tell you so on this
hardware. It is in the menu because the firmware is shared.

### NRF24

**Scanner**, **Proto Kill**, **ESB Sniffer**, **ESB Replay**, **MouseJack
Scan**, **MouseJack Inject**.

**Scanner** is a 2.4 GHz band sweep. It shows which channels are busy across
the whole band, which is the fastest way to see what is around you before
picking a tool. It is passive.

### SubGHz

**Replay Attack**, **SubGHz Jammer**, **De Bruijn / Brute**, **Jamming
Detector**, **Saved Profile**.

**Jamming Detector** is passive and listens for interference. The other four
use the CC1101.

### Detect

**Surveillance** and **Drone Detector**. Both are passive. Drone Detector
watches for the Remote ID broadcasts drones are required to send.

#### What Surveillance is looking at

283 signatures across 10 kinds. A row appears when something in range
announces itself in a way one of them recognises.

The useful thing to know is not the list of vendors, which goes stale, but
the questions being asked, because that is what explains a row that is there
and a row that is not. Eight questions over the nine tables, because a name
matched from its first character is one question asked separately of a WiFi
network and a BLE device:

| It can see | What that catches |
|---|---|
| a whole MAC address | hardware with no vendor block, only a hardcoded one. A Pwnagotchi is `de:ad:be:ef:de:ad` on every unit |
| the first three bytes of a MAC | the block IEEE assigned to a manufacturer |
| a BLE or WiFi name, from the start | matched from the first character, optionally at an exact length |
| a network name, anywhere in it | the same, in the middle. A Pineapple's SSID has whatever its owner typed in front of it |
| a BLE company or service ID | who made it, or what protocol it speaks |
| a 128-bit service UUID | somebody's own protocol rather than a shared allocation, so worth much more |
| manufacturer data | the bytes *past* the company ID. Apple's `0x004C` is every iPhone in range; the `0x12` after it is an AirTag separated from its owner |
| service data | the bytes past a service UUID. A Find My Device tag and a shop beacon both advertise `0xFEAA`, and the frame type says which |

Every row is graded **strong**, **likely** or **weak**, and the colour on
screen says which. A weak row is a hint and not a finding: a contract
manufacturer's OUI, or a brand name that is also an ordinary English word. A
second, differently-labelled match on the same device promotes a likely to a
strong, because two independent fields agreeing is worth more than either.

A detector that cries wolf is one you stop believing, which is why so many
rows are deliberately weak.

#### Finding a tracker you did not put there

Surveillance finds, Hunt locates, and the step between them is yours.

1. **Leave Surveillance running.** It is passive, so nothing announces you.
   The device can be in a bag or face down: the dwell alarm is audible, which
   is what the speaker is for. Filter to **TRACKER** if the list is busy.

2. **Wait for the dwell alarm.** Ten minutes of continuous presence turns a
   row into an alert.

3. **Separate "with me" from "near me" by moving.** Pueo has no position of
   its own. It knows it heard a radio and for how long, and from inside the
   device a tag in your coat and a beacon by the till are the same
   observation. Walk somewhere a fixed beacon cannot follow, wait, and see
   what came along.

4. **Walk it down with Hunt.** Pick the row and the gauge opens: smoothed
   signal strength, a green tick at the best reading so far, and WARMER or
   COLDER in letters you can read at arm's length.

   It is not distance. An unobstructed tag across a room can read stronger
   than one under a seat two feet away. Turn slowly on the spot and watch
   where the needle peaks rather than staring at one number, and let each
   reading settle. Your own body between the device and the tag costs about
   10 dB, which you can use: block it deliberately, and the direction the
   signal drops is the direction the tag is in.

**A quiet screen is not proof.** None of these announce themselves, and none
of them are unusual:

| Invisible to Pueo | Why |
|---|---|
| a tag still with its owner | Apple's separated broadcast is type `0x12`; a tag near its owner sends `0x07`, and that is deliberately ignored |
| cellular-only trackers | no BLE advertisement to hear |
| anything switched off | including a tag between periodic wakes |
| 5 GHz-only WiFi | the ESP32 is 2.4 GHz |
| recording-only devices | a dashcam writing to a card transmits nothing |

So an empty list honestly means *nothing that announces itself is here*. That
is useful and it is narrower than *nothing is here*.

#### Filtering the list

The **left button** opens a filter. Toggle any of the ten kinds, and set a
confidence floor of everything, likely and up, or strong only.

It is worth knowing exactly what it does and does not do.

- **It only hides rows.** Everything is still detected, still counted, and
  still written to the card if logging is on. The filter is a view.
- **It resets when you leave the screen.** This is deliberate. A filter you
  forget is on is one that shows you an empty street you never actually
  looked at.
- **While it is on, the header counts both ways:** `hits 4/13` rather than
  `hits 13`. The second number is the one that has not changed.

Turning **VEHICLE** off is the common case. Modern cars advertise constantly
and most of those rows are graded weak for that reason, but in traffic there
can be a lot of them.

### RFID/NFC

**Card Reader**, **Card Clone**, **Erase**, **Dump**, **Decode Access**,
**Jam Reader**, **Tag Disrupt**, **Disrupt Emulate**.

All of these need the PN532, and all of them want the card within a couple
of centimetres of the coil. Read range on these modules is short and the
enclosure is built to preserve what there is, so hold the tag against the
back of the case rather than waving it nearby.

### GPS

**Wardriver** and **Satellite Scanner**.

**Wardriver** logs what it sees with positions, in a format you can upload
to WiGLE. **Satellite Scanner** shows the constellation and signal
strengths, which is how you tell a fix is coming before it arrives.

### System

**Serial Monitor**, **Update Firmware**, **Touch Calibrate**, **SD File
Manager**, **File Transfer**, **Settings**, **About**.

**Touch Calibrate** is worth running once on a new unit. **SD File Manager**
browses the card on the device, which saves pulling it out to check whether
a capture landed.

**File Transfer** is how you get a capture off without pulling the card. It
raises its own WiFi access point and serves the card over HTTP, read only,
opening on `/pueo/` where the captures are.
The screen shows three things: a network name, an eight digit password and a
URL. Join the network from a phone or a laptop, open the URL, tap a file.

The password is new every time you open the screen and is not stored
anywhere, so there is nothing to change later and nothing to leak. Exit
takes the access point down.

A few things worth knowing:

- It transmits, so Stealth Mode refuses it like any other transmitter.
- Nothing it serves can be written, renamed or deleted. That is SD File
  Manager's job, and it wants the panel in your hand rather than a password.
- A phone will usually warn you the network has no internet. That is
  correct; there is no internet behind it.
- Two devices at a time, and the screen stops updating while a file is going
  out. One radio and one SPI card reader is the whole of the throughput here,
  so expect a large PCAP to take a while. Nobody has timed one yet.

---

## What needs what

Nothing stops you opening a feature whose hardware is not fitted. Most will
open and then tell you the module is missing.

| Feature group | Needs |
|---|---|
| WiFi, Bluetooth, Detect | nothing extra, the ESP32 does it |
| NRF24 group | NRF24L01+PA+LNA |
| SubGHz group | CC1101 |
| RFID/NFC group | PN532 |
| GPS group | ATGM336H |
| anything that captures | an SD card, or the capture is lost |

---

## The SD card

FAT32, inserted in the slot in the side of the lid. The card can go in and
out without opening the case.

**A card over 32 GB will not mount until you reformat it.** exFAT is compiled
out of the core this is built against, `FF_FS_EXFAT 0` in its `ffconf.h`, and
exFAT is what every card above 32 GB arrives formatted as. The symptom does
not look like a filesystem: the card reads on a computer and does nothing on
the device, which reads as a dead slot. FAT16 mounts as well, if the card is
old and small enough to have it.

**Windows will not make a FAT32 volume above 32 GB** from its own Format
dialog. That is the dialog rather than FAT32, which goes to 2 TB. The SD
Association's [SD Card Formatter](https://www.sdcard.org/downloads/formatter/)
does it, and so does `mkfs.vfat -F 32` from anything unix. A 64 GB card is
fine once something other than Windows has formatted it.

Everything lives in one folder, `/pueo/`. The card is yours, and a tool
that scatters nine directories through your root has made it its own.

**What Pueo reads:**

| Path | What it is |
|---|---|
| `/pueo/ssids.txt` | your own network names for Beacon Spammer, one per line |
| `/pueo/config/settings.json` | your settings, written by the device |
| `/pueo/config/wigle.txt` | WiGLE upload credentials, if you use that |
| `/pueo/ducky/` | DuckyScript files |
| `/pueo/firmware.bin` | an image for Update Firmware, if you use it |

**What Pueo writes:**

| Path | What lands there |
|---|---|
| `/pueo/logs/` | feature logs, including the jamming detector's |
| `/pueo/captures/` | packet captures |
| `/pueo/esb/` | ESB sniffer captures |
| `/pueo/subghz/` | saved sub-GHz captures and profiles |
| `/pueo/config/` | settings, and the WiGLE upload working file |
| `/pueo/captive_portal/` | captive portal results |

**If your card already works, it keeps working.** The four things you put
there yourself are read from `/pueo/` first and the old location second:
`ssids.txt`, `ducky/`, `firmware.bin` and `config/settings.json`. Nothing
moves your files. Settings are read from wherever they are and saved to the
new path, so the first time you change a setting they migrate themselves.

Captures already on the card stay where they are, in `/logs`, `/captures`
and so on. Only new ones go under `/pueo`. Move the old folders in yourself
if you want them together.

For `ssids.txt`: one name per line, blank lines and lines starting with `#`
ignored, 32 characters maximum per name, first 64 used. There is a starter
list in [ssids.txt](ssids.txt) you can copy to the card.

---

## Settings

Under **System → Settings**.

| | |
|---|---|
| Brightness | screen backlight |
| Theme | dark or light |
| Accent colour | the highlight colour |
| Auto WiFi scan | scan on entering WiFi features |
| Auto BLE scan | scan on entering Bluetooth features |
| Stealth mode | see below |
| SD logging | master switch, plus one per feature group |

**SD logging has a master and per-feature switches**, and a feature logs
only when both say yes. Turning the master off leaves the card alone
entirely. It does not affect files you ask for by name, such as saving a
capture or a profile: those happen because you pressed the button.

Settings are saved to the card at `/pueo/config/settings.json`. Without a card
they last until reboot.

---

## Stealth mode

**Receive only.** Every tool whose job is to transmit refuses to start, and
scans that would transmit while looking passive are made genuinely passive.

It is off by default. When it is on and you open something it blocks, you
get a full-screen notice naming the feature rather than a silent failure.

What it refuses:

```
ARP Scanner         AirTag Spoofer      BLE Jammer          BLE Spoofer
Beacon Spammer      Captive Portal      De Bruijn / Brute   ESB Replay
File Transfer       Hidden SSID Rev.    Karma Attack        MouseJack Inject
Probe Req Flood     Proto Kill          Replay Attack       Saved Profile
Sour Apple          SubGHz Jammer       Web OTA             WPS Scanner
WiFi Deauther
```

Twenty-one tools, and with them the whole **RFID/NFC** menu. A PN532 reads a
card by energising a field and waiting for the card to answer, so even
**Card Reader** transmits; stealth gates that menu as one rather than entry
by entry, and nothing in it opens.

Two more things transmit without being tools in their own right, and both
refuse where they stand rather than closing what they sit inside:

- **Fast Pair**, the **Probe** button. The scan is passive and keeps
  running; the probe writes to the device and does not. The confirm screen
  says so before you confirm.
- **BLE Scanner**, the **Info** button. Reading the Device Information
  Service means opening a connection.

**Firmware Update** keeps its SD half. **Web OTA** joins a network and
serves HTTP over it, so it refuses; updating from a file on the card
transmits nothing and still works.

Everything else stays available, which is most of the passive side: the
scanners, the detectors, Surveillance, the drone detector and wardriving.

Use it when you want to be certain the device is only listening.

---

## When something says it is not there

**"No CC1101"** means the sub-GHz radio did not answer. The firmware reads
the chip's own part and version registers rather than assuming, so this is a
real answer and not a guess. Check the wiring and the chip select.

**A feature that opens and does nothing** is usually a module that is not
fitted. The status bar will not tell you: it shows the ESP32's own radios,
not the bolted-on ones.

**BLE Rubber Ducky refusing** is expected. It needs an ESP32-S3 and this is
an ESP32.

**Touch landing in the wrong place** wants **System → Touch Calibrate**.

**Captures not appearing** is worth checking in three places, in order: the
SD icon in the status bar, whether SD logging is on in Settings, and whether
the feature has its own switch turned off.

---

## A note on the 2.8 inch panel

Pueo is for the 3.5 inch ESP32-3248S035R and nothing else.

It used to build a 2.8 inch ESP32-2432S028R image as well. That image was
released every version and nobody ever booted one, so it was never supported in
any sense you could rely on, and it is gone after 0.4.13. Those releases are
still published if you want to try one, and the last of them is where to
start; nothing after it will run on that board at all.
