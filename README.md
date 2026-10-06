<p align="center">
  <img src="docs/img/pueo-header.webp" width="100%"
       alt="The Pueo mark: a stylised horned owl in green on near-black, with
            a WiFi arc, a Bluetooth rune and a satellite worked across its
            chest and the word PUEO beneath, standing in a sweep of concentric
            signal arcs.">
</p>

# Pueo

*Pueo* is the Hawaiian owl, a low, silent flier that hunts by listening.

**A handheld multi-radio field tool.** Open-source firmware for the
"cheap yellow display", a touchscreen ESP32 carrying a CC1101 for sub-GHz,
an NRF24L01+PA+LNA for 2.4 GHz, a PN532 for NFC and an ATGM336H for GPS, in
a printed enclosure zoned to keep the radios apart.

<p align="center">
  <img src="docs/img/pueo-assembled.webp" width="420"
       alt="The assembled Pueo: a purple 3D-printed case with a 3.5 inch screen
            showing the main menu, eight tiles in two columns, with two antennas
            standing off the top edge.">
</p>

One panel. The **3.5" ESP32-3248S035R** is the board this runs on, the one
it has run on, and the one with a dimensioned enclosure. It does not break
out the SPI bus, so six of the ten signals get soldered to the ESP-WROOM-32
module's own castellations; what put it ahead of the 2.8" was that all three
CC1101 control lines land on one 4-pin connector rather than two.

The 2.8" **ESP32-2432S028R** was supported up to **0.4.13** and is not any
more. It was built every release and never booted, so what shipped for it was
an image that compiled rather than one that worked. Those releases are still
published and still carry their digests.

It covers Wi-Fi and BLE reconnaissance, sub-GHz capture and replay, NFC read
and clone, GPS wardriving, and jam detection.

> [!NOTE]
> Feature claims inherited from upstream are inherited, not audited. Where
> one has been read closely, the changelog entry that did it says so. See the
> status note in [PUEO.md](PUEO.md).

> [!CAUTION]
> For **educational and research use only**. Use only on networks and devices
> you own or have written permission to test. Several of these tools transmit,
> jam, or impersonate. Unauthorised use is illegal in most jurisdictions.

## Lineage

Pueo is a fork of **[ESP32-DIV](https://github.com/cifertech/ESP32-DIV) by
CiferTech**, which is MIT. This fork is GPL-3.0-or-later. Nearly every
feature here is theirs; go star the original, and if you want to support the
work this is built on, [their Patreon](https://www.patreon.com/cifertech) is
the place rather than this repo's sponsor button.

Forked at `90f7967c4dc7bcd8c09ebdcf421886737516ddc2` (upstream main,
2026-09-01, eleven commits past `v1.7.2`). Upstream is kept as a read-only
`upstream` remote for cherry-picking, but Pueo no longer tracks it.

The fork exists because upstream targets its own hardware and the ESP32-S3,
while this targets a stock CYD with modules bolted on. That board has real
pin conflicts and a shared SPI bus, and resolving them meant changes too
invasive to send back as a patch.

## Hardware

A stock CYD (ESP32-WROOM-32, XPT2046 touch, SD slot) plus four modules.

| Module | Interface | Notes |
|---|---|---|
| CC1101 (HW-863) | VSPI, CS 21 | sub-GHz, board-mounted SMA |
| NRF24L01+PA+LNA | VSPI, CSN 25 / CE 16 | 2.4 GHz, needs its own 3.3 V rail |
| PN532 V3 | VSPI, SS 17 | NFC, SPI mode |
| ATGM336H | UART, TX into GPIO 1 | GPS, antenna on a u.FL pigtail |

Both chip selects are where they are because the obvious pin was taken: 27 is
the backlight, and the stock CYD profile puts a chip select on 25, which is
the touch clock on the smaller board this used to build for as well.

<p align="center">
  <img src="docs/img/pueo-inside-lid.webp" width="360"
       alt="The inside of the printed lid with the display board screwed into
            it, a USB-C cable running out through an opening in the end wall
            and a microSD card seated in its socket.">
  <img src="docs/img/pueo-enclosure.webp" width="440"
       alt="The enclosure rendered from its OpenSCAD source: the base with its
            module pockets on the left, the lid with its display window on the
            right.">
</p>

<p align="center"><sub>Left: the board in the lid, both openings in use.
Right: the two printed parts, rendered from source.</sub></p>

Three of those pins are the CYD's onboard RGB LED, which Pueo gives up. The
GPS lands on GPIO 1, UART0's *transmit* pin, which looks wrong and is
deliberate; [docs/pueo/hardware.md](docs/pueo/hardware.md) explains why, and
`gpsPortOpen()`/`gpsPortClose()` hand the pin between the console and the GPS.

Run `python tools/check_pinmap.py` to print the map and check for collisions.

## Features

Inherited from ESP32-DIV unless marked.

| Wi-Fi | NRF24 | Sub-GHz | BLE | Detect | GPS / RFID / System |
|---|---|---|---|---|---|
| Packet Monitor | Scanner | Replay Attack | BLE Jammer | **Surveillance** *(new)* | Wardriver |
| Beacon Spammer | Proto Kill | SubGHz Jammer | BLE Spoofer | **Drone Detector** *(new)* | Satellite Scanner |
| Wi-Fi Deauther | ESB Sniffer | De Bruijn / Brute | Sour Apple | | Card read / clone / erase |
| Probe Req Flood | ESB Replay | Jamming Detector | AirTag Spoofer | | Dump, Decode Access |
| Deauth Detector | MouseJack Scan | Saved Profile | AirTag Sniffer | | Jam Reader, Tag Disrupt, Disrupt Emulate |
| Wi-Fi Scanner | MouseJack Inject | | Sniffer | | Serial Monitor |
| Captive Portal | | | BLE Scanner | | Update Firmware |
| Hidden SSID | | | BLE Rubber Ducky | | Touch Calibrate |
| WPS Scanner | | | Skimmer Detect | | SD File Manager |
| ARP Scanner | | | **Hunt** *(new)* | | **File Transfer** *(new)* |
| Karma Attack | | | **Fast Pair** *(new)* | | Settings, About |
| **AP Tracker** *(new)* | | | | | |

### What Pueo adds

Six features are Pueo's own. Four of them only listen.

**Surveillance** (called Spotter until 0.4.0) is passive detection of
surveillance and tracking hardware that announces itself: plate readers and
their accessories, body cameras, fixed cameras and doorbells, smart glasses,
item trackers, vehicle modules and pentest kit. It matches whole MAC
addresses, Wi-Fi OUIs, network names from the start or anywhere in the middle,
BLE device names, 16- and 128-bit service UUIDs, and the bytes past a company
ID or a service UUID, against 274 signatures across nine kinds, and grades
what it finds rather than asserting it.

The signatures are published separately at
[magikh0e/surveillance-signatures](https://github.com/magikh0e/surveillance-signatures),
as Markdown, CSV and JSON, with a writeup on what the grades mean and how to
contribute one. They are extracted from `ESP32-DIV/SpotterSignatures.h` here,
so that repository is where to send a correction or a vendor this does not
know about.

Two of those are not surveillance gear and are there because knowing they
are nearby is worth something on its own. **Pwnagotchi** beacons from a fixed
`de:ad:be:ef:de:ad` with its stats in the payload, so it announces itself
more clearly than most of the things it is looking for. **Meshtastic** nodes
advertise their own 128-bit BLE service, and get a kind of their own rather
than being filed under something they are not.

**Drone Detector** reads ASTM F3411 Broadcast Remote ID on both the Wi-Fi
and BLE paths: the UAS ID, position, altitude, speed, and the operator's own
location, which is the field that makes it different from watching an
aircraft. The parser is checked against opendroneid-core-c rather than
against a reading of the standard.

**Hunt** is for after Surveillance says yes. Pick a tracker, Find My, Tile,
SmartTag or Eddystone, and a needle swings with signal strength so the thing
can be walked down. It shows strength rather than distance, because RSSI is
not distance; what it is good for is which way the needle moves when you do.

**AP Tracker** is the same needle against a Wi-Fi access point. It parks on
the AP's channel and reads its beacons, which arrive about ten times a
second, where a scan sweep samples once every 1.7 s and transmits to do it.

**Fast Pair** is a scanner for Google's side of the BLE world, which this
device had never looked at, plus a probe for CVE-2025-36911. The probe is
the one feature here that transmits at a single named target, and it sits
behind a confirm screen that names the address.

**File Transfer** gets a capture off the card without pulling the card. It
raises its own WPA2 access point with an eight digit password shown on
screen, serves the card over HTTP read only, and takes the AP down on exit.
The password is generated when the screen opens and stored nowhere.

It began as a request for Bluetooth, which does not survive contact with the
stack: there is no standard BLE profile for sending a file, the thing phones
actually speak is OBEX over Classic Bluetooth, and Classic needs Bluedroid
where this firmware is built on NimBLE. A custom GATT service would transfer
files to an app nobody has written. Every phone already has an HTTP client.

### Additions to tools that were already there

**BLE Scanner** reads its detail view rather than dumping it. Appearance is
decoded to a category and subcategory instead of a number, service UUIDs are
named where the SIG assigned them, and **Info** connects to the device on
screen and reads its Device Information Service: manufacturer, model, serial,
and the firmware, hardware and software revision strings.

That read is the only thing in the scanner that opens a connection. The scan
was never silent: a BLE scan is active unless told otherwise and sends a scan
request to every advertiser it hears, which is one of the things Stealth Mode
makes passive. A connection is a different order of loud, so the read is one
device, chosen by the operator, reads only, nothing outside `0x180A`, and
refused under Stealth Mode. What it is for is the question an advertisement never answers:
not what a device is, but which build it is running, which is the question
underneath whether something has been patched.

**Beacon Spammer** takes its network names from `/pueo/ssids.txt` on the card when
there is one, and falls back to the list built into the firmware, which is
the same in every copy. The screen says which list it loaded.

### Three settings the device did not have

**Stealth Mode** makes it receive only across the whole device. Twenty-one
features that transmit refuse to start and say so; scans that were quietly
active, and there were twelve, are made passive instead of blocked. Two
transmit paths have no menu entry to gate, the Fast Pair probe and the BLE
scanner's Info read, so the check sits inside the function that would
transmit and hands the refusal back to whatever called it.

**SD Logging** is a master switch plus one per feature, and a feature says
in its own words when it cannot log rather than failing quietly.

**Boot Lock** is a password before the menu, salted SHA-256 and stretched,
stored in NVS and never on the card. It stops someone who picks the device
up and nothing else, and it is documented that way in its own source.

### A second firmware

**PueoBeacon** is built with `PUEO_ROLE=beacon` and flashed to a second
board. It emits every signal the detectors look for, so an empty list can be
told apart from an empty room. It found four bugs on its first evening.

**IR is gone.** There is no IR LED or receiver on this board, so its
protocol tables could never run here. Removing them freed **139,752 bytes**
of flash and 4,472 of RAM.

## Building

```sh
tools/build.sh setup      # once, installs a pinned toolchain (~1 GB)
tools/build.sh            # compile
tools/build.sh merge      # single flash image, write to offset 0
tools/build.sh upload COM7
```

Needs `arduino-cli` and Python 3. Everything installs into its own root
rather than a global Arduino install, so it cannot disturb another project.

**Read "Things that will bite you" in [PUEO.md](PUEO.md) before deviating
from the script.** The esp32 core version, the pinned library versions and
the toolchain path length are all load-bearing. On Windows the last one is
a `MAX_PATH` trap that reports a header as missing when it is right there.

The build compiles clean under `-Wall -Wextra`. Upstream's `platform.txt`
carries `-w` and `-zmuldefs`; `build.sh` strips both, and
[docs/pueo/warnings.md](docs/pueo/warnings.md) and
[zmuldefs.md](docs/pueo/zmuldefs.md) record what each was hiding, including
one linker override that turned out to be load-bearing.

## Documentation

| | |
|---|---|
| [docs/pueo/user-guide.md](docs/pueo/user-guide.md) | using one: the menus, the card, settings, stealth |
| [docs/pueo/build-guide.md](docs/pueo/build-guide.md) | building one: parts, soldering, the ten signals |
| [PUEO.md](PUEO.md) | build, layout, scope, and what will bite you |
| [docs/pueo/hardware.md](docs/pueo/hardware.md) | pin map and the GPIO 1 handover |
| [docs/pueo/spi-bus.md](docs/pueo/spi-bus.md) | the shared bus, and why touch was losing it |
| [docs/pueo/warnings.md](docs/pueo/warnings.md) | what `-w` was hiding |
| [docs/pueo/zmuldefs.md](docs/pueo/zmuldefs.md) | what `-zmuldefs` was hiding |
| [docs/pueo/pcb-design.md](docs/pueo/pcb-design.md) | carrier board: netlist, power tree, placement |
| [docs/pueo/pueo-enclosure.scad](docs/pueo/pueo-enclosure.scad) | the printed enclosure |

## Issues or discussions

Both are open, and on a hardware project they overlap enough to be worth
splitting on purpose.

**[Issues](https://github.com/magikh0e/pueo/issues)** for something broken
and reproducible: wrong behaviour, a build that fails, a crash, a feature
that does nothing. Say which panel you are on, the version from the status
bar, and what you did. A pin map or a serial log beats a description.

**[Discussions](https://github.com/magikh0e/pueo/discussions)** for
everything else. *Q&A* for whether a particular CYD variant will work,
*Show and tell* for builds and prints, *Ideas* for features.

One thing is especially useful, because nobody here can produce it:

- **A board that is nearly but not quite this one.** The
  3248S035**C** with capacitive touch is the obvious case: GPIO 25 goes to
  its GT911, and this image drives a chip select into that pin. If you have
  one, the answer is interesting either way.

## What changed from upstream

Mostly the parts that decide whether the hardware works at all:

- **`board_pueo.h`**: board profile resolving every pin conflict in the
  stock CYD map
- **`SpiBus.{h,cpp}`**: a single owner for the VSPI bus the display, SD card
  and all three radios share
- **`wifi.cpp`**: removed an out-of-bounds write present in both deauth
  frame builders
- **CC1101 driver**: vendored; `SpiEnd()` no longer calls `SPI.end()` after
  every register access, which was tearing the peripheral out from under
  touch and the SD card
- **Warnings**: `-w` and `-zmuldefs` removed from the build, and the
  underlying issues fixed rather than silenced

And the ones that were giving wrong answers rather than crashing. Where a
fix applies to upstream too it has been sent back, and the pull request is
linked; the rest are CYD-specific or were already covered there.

- **GPS**: NMEA sentences are checked against their own checksum before
  anything parses them. `stripChecksum()` truncated at the `*` and threw the
  two hex digits away, so a corrupted RMC still looked like a well formed
  RMC, with a plausible latitude and timestamp, and the wardriver wrote it
  to the card as a real fix. A sentence with no checksum at all is rejected
  too, because truncation is the likely corruption here and a sentence cut
  before its `*` would otherwise pass with its remaining fields intact and
  wrong.
- **CC1101 presence**: read back `PARTNUM` and `VERSION` over SPI and check
  the answer is one a CC1101 would give, twice, because a floating line can
  look like a version once. Before this the sub-GHz tools started against a
  radio that was not there and hung. All five features that use the radio go
  through the guard: Replay Attack, Sub-GHz Jammer, Sub-GHz Brute, Jamming
  Detector and Saved Profiles. Sent upstream as
  [#257](https://github.com/cifertech/ESP32-DIV/pull/257).
- **Scratch AP**: the interface the WiFi tools raise to push raw frames had
  a fixed WPA2 passphrase written into the source, so every unit running
  this brought up a visible network with a published key. It is hidden and
  open now and carries no key at all. Sent upstream as
  [#258](https://github.com/cifertech/ESP32-DIV/pull/258).
- **BLE spoofers**: the random MAC each round generated was never applied
  to the adapter, so Sour Apple and the spoofer transmitted from the same
  address every time. Not sent: upstream already had a patch for it.
- **Sub-GHz jammer**, leaving the jammer while it was running did not stop
  it transmitting. `jammingRunning` was only ever cleared by the toggle, so
  backing out of the feature left the radio keyed. Sent upstream as
  [#255](https://github.com/cifertech/ESP32-DIV/pull/255).
- **Menu tables**: a checker that the parallel label and icon arrays are
  fully initialised. They are walked by the same index, so a short icon
  table is a null pointer dereference at a menu position nobody visits in
  testing. Sent upstream as [#256](https://github.com/cifertech/ESP32-DIV/pull/256).

## License

**GPL-3.0-or-later** for this fork's own code. See [LICENSE](LICENSE).

Upstream is MIT, Copyright (c) 2023 CiferTech, kept verbatim in
[LICENSE.MIT](LICENSE.MIT). Those portions stay available under MIT from
upstream, and the notice travels with any redistribution of this tree.

> Images published before the RF24 removal, 0.1.0, 0.2.0 and 0.2.1. Link
> RF24, which is GPL-2.0-*only* and cannot lawfully share a binary with
> arduinoFFT or NimBLE-Arduino. That conflict was inherited from upstream's
> dependency set and had been there since 0.1.0. RF24 is gone now, nothing
> else in the tree is GPLv2-only, and the combined work is distributable as
> GPL-3.0-or-later. See [docs/pueo/licensing.md](docs/pueo/licensing.md).

## Credits

**[CiferTech](https://github.com/cifertech)** wrote ESP32-DIV, which is
almost all of this. Support the original project:
[patreon.com/cifertech](https://www.patreon.com/cifertech).
