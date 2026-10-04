# Pueo

Open-source firmware for a handheld multi-radio field tool built on a
"cheap yellow display", a touchscreen ESP32 carrying a CC1101 for sub-GHz,
an NRF24L01+PA+LNA for 2.4 GHz, a PN532 for NFC and an ATGM336H for GPS, in
a printed enclosure zoned to keep the radios apart.

Two panels, one flash image each. The 3.5" ESP32-3248S035R is the reference
board. The one this has run on, the one with a dimensioned enclosure, and
the one that brings its SPI bus out on a connector rather than asking for
three joints on a microSD slot.

The 2.8" ESP32-2432S028R was supported up to 0.4.13 and is not any more. It
was built every release and never booted, so what was published for it was an
image that compiled rather than one that worked. Dropping it is what stops
this tree carrying a second set of pin numbers nobody has ever checked
against a board.

Pueo covers WiFi and BLE reconnaissance, sub-GHz capture and replay, NFC read and
clone, GPS wardriving and jam detection. It is a fork of CiferTech's
ESP32-DIV, which is MIT; this fork is GPL-3.0-or-later. It diverges mainly
in the parts
that decide whether the hardware works at all: a board profile that resolves
the pin conflicts in the stock CYD map, a single owner for the SPI bus that
the display, SD card and all three radios share, and a build that compiles
clean under `-Wall -Wextra` rather than suppressing its own warnings.

> **Status.** It runs on the reference board, and has since 2026-09-20.
> Everything here compiles clean under `-Wall -Wextra`, several of the fixes
> are verified at the symbol level, and the parsers carry model-based tests
> that run on a host. No radio module is soldered to a board yet, so the
> features that need one are compiled rather than exercised. Features inherited from upstream are
> inherited, not audited. Where one has been read closely, the changelog
> entry that did it says so.

Lineage: forked from [CiferTech's ESP32-DIV](https://github.com/cifertech/ESP32-DIV)
at `90f7967c4dc7bcd8c09ebdcf421886737516ddc2` (upstream main, 2026-09-01,
eleven commits past `v1.7.2`). Upstream is kept as a read-only `upstream`
remote for cherry-picking, but Pueo no longer tracks it.

## Licensing

This fork's own code is **GPL-3.0-or-later**; see `LICENSE`. Upstream is
**MIT**, Copyright (c) 2023 CiferTech, and that notice is kept verbatim in
`LICENSE.MIT`. Those portions remain available under MIT from upstream, and
the notice travels with any redistribution of this tree.

An earlier version of this section said that publishing binaries carries no
source-disclosure obligation. **That was wrong**, and the correction is worth
keeping visible. The merged image linked RF24, which is GPL-2.0-*only*,
alongside arduinoFFT (GPL-3.0-or-later) and NimBLE-Arduino (Apache-2.0).
GPLv2-only cannot lawfully share a binary with either, so no single licence
covered what was being published, and none had since 0.1.0.

**RF24 is gone as of `Nrf24Raw`.** Nothing else in the tree is GPLv2-only, so
the combined work is now distributable as GPL-3.0-or-later, with the ordinary
GPL obligation that whoever receives the binary can get the source, which is
what the archive beside it is for. Images published before that change
(0.1.0, 0.2.0, 0.2.1) still contain RF24 and are still in the conflicted
state. `docs/pueo/licensing.md` has the whole reasoning.

## Layout

```
ESP32-DIV/board_pueo.h   board profile: pin overrides + rationale
ESP32-DIV/BoardConfig.h       board selection (BOARD_PUEO is on)
tools/check_pinmap.py         resolves the pin macros, flags collisions
docs/pueo/hardware.md    pin map, wiring decisions, known upstream bugs
docs/pueo/ui.md          the display stack, and why there is no toolkit
```

The board profile is an *overlay*, not a fourth board branch. Every pin macro
in `shared.h` is wrapped in `#ifndef`, and `BoardConfig.h` is included before
those defaults are evaluated, so the overlay wins by defining pins first. It
also defines `BOARD_CYD`, so display, touch, SD and UI behaviour follow the
stock CYD path. Net effect: `git merge upstream/main` touches one line of
`BoardConfig.h` at worst, instead of conflicting across every pin block.

## Checking the pin map

```bash
python tools/check_pinmap.py
```

Runs a cut-down preprocessor over `shared.h` and reports the resolved GPIO for
every signal that drives a pad, then checks for three things: two signals on
one pin, a signal landing on CYD hardware that isn't consciously repurposed,
and an output assigned to an input-only pad (GPIO 34-39). Exit 1 on any hit.

It is not a substitute for compiling. It is a substitute for reading six nested
`#ifdef` blocks and hoping.

## Building

Upstream is an Arduino sketch, so `arduino-cli` is the path of least
resistance. PlatformIO would mean restructuring the tree first.

```bash
tools/build.sh setup
```

```bash
tools/build.sh
```

```bash
tools/build.sh upload COM7
```

`setup` is a one-time ~1 GB download. It installs everything into its own root
rather than a global Arduino install, so it cannot disturb other projects.

Current size at 0.4.32: **1,797,165 bytes, 57% of the app partition**, and
124,452 bytes of RAM, 37%. Both are what the toolchain reported building the
published image, not a figure carried forward.

The partition is a single 3.00 MB slot. 0.4.2 moved to it from the pair of
1.88 MB slots an over-the-air update needs, which nothing here uses, so a
percentage quoted from before that release is against a partition this build
no longer has. It was at 93% of the old slot before the IR module came out.
Pueo has no IR LED and no IR receiver, so `ir.cpp` and the IRremoteESP8266
protocol tables it pulled in were 140 KB of code that could never run on this
board. Trimming upstream
modules this hardware cannot reach is the cheapest headroom available, and
there is more of it: Ducky/BadUSB is the next-largest piece with no
corresponding hardware.

### Emulation

`tools/build.sh merge` produces a single flash image at offset 0. Padded to
4 MB it boots under Espressif's QEMU:

```bash
qemu-system-xtensa -nographic -machine esp32 -drive file=pueo-4mb.bin,if=mtd,format=raw
```

It gets as far as IDF core init and then stops:

```
assert failed: do_core_init startup.c:328 (flash_ret == ESP_OK)
```

That is `esp_flash_init_default_chip()` rejecting QEMU's emulated flash. The
QEMU shipped with the current IDF installer is built against the IDF 5/6 line
while this firmware is Arduino core 2.0.10, which is IDF 4.4. The flash chip
detection does not line up. Nothing to do with the firmware: the ROM loader,
the second-stage bootloader and the app image all load correctly first.

Worth being clear about the ceiling even if that were fixed. QEMU models the
CPU, RAM, UART and timers. It does not model the ILI9341, the XPT2046, the
CC1101, the NRF24, the PN532 or the WiFi radio, which is to say, all of the
things actually worth testing here. A perfect boot under QEMU would prove
`setup()` reaches the point where it touches hardware, and nothing beyond it.

### Things that will bite you

**esp32 core 2.0.10, exactly.** Upstream documents this and means it. The 3.x
line is IDF 5, which dropped `esp_event_loop.h`, `config.h` includes it, so
3.x fails on the first file.

**Library versions are pinned, and not for neatness.** Library Manager hands
you the newest, and three of these broke their APIs: ArduinoJson 7 dropped
`StaticJsonDocument` and `createNestedObject`, NimBLE 2.x dropped
`NimBLEAdvertisedDeviceCallbacks` and `NimBLESecurity` and renamed the
`NimBLEHIDDevice` accessors, and arduinoFFT 2.x replaced the `arduinoFFT`
class with `ArduinoFFT<T>`. Between them that is roughly twenty compile
errors that look like code bugs and are not.

**TFT_eSPI and the CC1101 driver must come from `Libraries/`.** Upstream
customised both. `User_Setup cyd.h` has to land as TFT_eSPI's `User_Setup.h`.

**Windows MAX_PATH.** The toolchain deliberately lives at `~/.pueo-esp32`, not
inside the repo. The esp32 core compiles with `-fno-rtti`, which selects the
`no-rtti` libstdc++ multilib, and with the core inside this repo the path to
`.../xtensa-esp32-elf/no-rtti/bits/error_constants.h` came to 259 characters,
one under the 260 limit. The compiler reported the header as missing while it
sat right there, and only that one multilib was affected, so the default build
worked and `-fno-rtti` did not. Override the location with `PUEO_ARDUINO_ROOT`
if you must, but keep it short.

**The patched `platform.txt`.** Upstream ships one and the build needs it.
`-DNFC_INTERFACE_SPI` puts the PN532 library into SPI mode. `-zmuldefs` lets
`wifi.cpp` override the IDF's `ieee80211_raw_frame_sanity_check` so raw 802.11
frames can be injected, which is load-bearing and cannot be done with
`--wrap`. It was also swallowing 30 unrelated duplicate symbols, one of them a
real bug; those are fixed during setup. See
[docs/pueo/zmuldefs.md](docs/pueo/zmuldefs.md). `-w` is **gone**,
`setup` strips it and the build runs `-Wall -Wextra`, which the sketch is
clean under. See [docs/pueo/warnings.md](docs/pueo/warnings.md).

**The vendored CC1101 driver is patched during setup.** It shipped a dead
copy-paste clone of itself (`..._JT_DRV.cpp`, a second `class
ELECHOUSE_CC1101` and 29 duplicate globals) and declared its hardware-SPI flag
as a global named `spi`, which collided with TFT_eSPI's `SPIClass spi`. See
the same document.

## Scope

Initial targets: SubGHz capture/replay, NFC read/clone, GPS wardriving, jam
detection. Everything else upstream ships stays compiled but untested on this
hardware.

Two deliberate calls on what to carry:

**IR is gone.** There is no IR LED and no receiver on this board, so it was
140 KB that could never run. See the size note under Building.

**Ducky/BadUSB stays.** It is 1476 lines and outside the initial targets, but
BLE HID needs no hardware Pueo does not already have, so unlike IR it
actually works here. Kept on purpose, not by neglect.

## Changes so far

Per-release notes, in the form that ships in the archive, are in
`CHANGELOG.txt`. What follows is the same ground with more of the reasoning.

New in 0.2.4, mostly reading what features already claimed and fixing what
turned out not to be true: the global promiscuous filter that let Packet
Monitor silently capture management frames only; handshake recognition in
the pcap it was already writing, with forcing one deliberately not wired up;
AirTag Sniffer telling a separated tag from a passing phone; BLE Scanner no
longer showing every Apple device as the letter `L`; the skimmer signatures
marked with the radio they actually speak; and one sub-GHz frequency list
where there were two. See `CHANGELOG.txt`.

New in 0.2.3, tooling and documentation only. The compiled image is
identical to 0.2.2 apart from the version string: `CHANGELOG.txt` ships in
the archive and sits beside the downloads; `tools/make_release.sh` takes
`PUEO_PUBLISH_DIR` to copy artifacts where they are served and verifies the
copies; and it refuses to re-cut a version already in `dist/`, because doing
so silently produced a different archive while the published digest stayed
as it was.

New in 0.2.2. Two threads: a detection path that survives a camera changing
its address, and the end of a licence conflict the project inherited.

- `ESP32-DIV/Nrf24Raw.{h,cpp}` @ the nRF24L01+ register interface, and the
  end of the RF24 dependency. RF24 is GPL-2.0-only and cannot lawfully share
  a binary with arduinoFFT (GPL-3.0-or-later) or NimBLE-Arduino
  (Apache-2.0), a conflict inherited from upstream's dependency set and
  carried since 0.1.0. It was the only GPLv2-only thing in the tree, so
  removing it resolves the whole thing and the merged image is distributable
  again. Almost nothing used it: MouseJack, the ESB paths and the skimmer
  detector already drove the registers directly, and RF24 survived only in
  the two jammers. 3,476 bytes smaller, and verified by deleting the library
  from the tree and rebuilding byte-identically
- **The jammers hop on purpose now.** They configured three RF24 objects
  over three channel groups, which reads as twelve channels across three
  radios and was neither: the board profile maps all three chip selects onto
  one module, and the loop inside `configureRadio` called
  `startConstCarrier` once per channel with each call replacing the last.
  The hopping that did work was in the feature loops, picking a random
  channel every iteration and writing it three times. Now round-robin on a
  4 ms dwell, written once
- **Relicensed to GPL-3.0-or-later**, with upstream's MIT notice kept
  verbatim in `LICENSE.MIT`. See `docs/pueo/licensing.md`, which has the
  dependency table, the grants it is read from, and why "version 2" and
  "version 2 or later" were the whole question
- `Spotter` **fingerprints the probe-request element set.** An FNV-1a over
  the information elements a device emits, which the chipset and driver
  decide rather than the network being looked for, so it survives the MAC
  randomisation that defeats the OUI table. Element ids always contribute;
  contents only where they belong to the device rather than to that
  particular probe, which means the SSID and the channel are deliberately
  left out. No signature table ships: the values need captures off real
  hardware. Four steps of `docs/pueo/ie-fingerprinting.md`, the fifth
  waiting on a camera
- **One camera is one row when it changes address.** `findOrAdd` folds a new
  address into an existing row on a fingerprint match, but only when both
  addresses are locally administered @ two cameras of one model share an
  element set, and merging on the fingerprint alone would report one where
  there are two. The row also shows whether the address is made up, how many
  times it has changed, and how long the device has been in range
- **Capture to SD**, on the Log button, off until it is switched on. One row
  per device rather than per frame, and every device rather than only the
  matched ones, because a fingerprint that already matched is one you
  already have. A capture of the air around you is a list of the people near
  you; it stays on the card
- `tools/fuzz_ie_walk.py`, `tools/check_spotter_merge.py`,
  `tools/check_spotter_capture.py` @ the parts that cannot be tried on
  hardware that does not exist, held down by model instead. 65,543 frames
  through the element walk with no out-of-bounds read, sixteen merge rules,
  and sixteen capture rules including that an SSID with a quote and a
  newline cannot forge rows in somebody's capture

New in 0.2.1, the first release since 0.1.0 whose compiled image actually
differs in something other than the version string:

- `SpotterSignatures.h`: Axon body cameras, and the community Flock OUI
  collections. `00:25:DF` is Axon Enterprise's own IEEE block, so it lands
  on `Strong` under a new `Bodycam` kind; it means Axon hardware in range
  rather than a camera specifically, since the same block covers their
  docks and Fleet systems. The Flock side is the collections, checked
  before being trusted. `flock-you` carries 32 WiFi prefixes and
  `flock-finder` 31, 30 of them shared, for a union of 33; all are in,
  along with the `Flock Camera net.` and bare `Flock` SSIDs, the
  `FS Ext Battery` and `DfuTarg` BLE names, XUNTONG's company ID on the
  Penguin pack, and the Raven GATT UUIDs. Every prefix was resolved against
  the IEEE MA-L registry first, which is why nearly all of them land on
  `Weak`: 23 belong to Liteon and 2 to Espressif, blocks that are in a very
  large amount of unrelated consumer hardware. Each carries the assignee
  the registry names rather than the word "Flock", and earns its place by
  corroborating a Flock SSID or a Penguin advertisement rather than by
  firing alone. 688 bytes of flash, no RAM.
- `docs/pueo/nrf24-fit-test.scad`: a test print for the one enclosure
  pocket with no nominal slack: a clearance ladder, and a slice of the base
  taken as an `intersection()` with `base()` so it cannot drift from the
  real part
- `tools/build.sh`: stopped shipping the build machine's home directory
  inside the firmware. NimBLE's assert macros bake `__FILE__` in, so the
  absolute path of every asserting source file was in the image: seventeen
  strings of `C:\Users\<name>\...` in the published 0.1.0 and 0.2.0
  binaries, still naming the folder the project used to be called.
  `-ffile-prefix-map` now rewrites those to `pueo\...` and `arduino\...`
  while compiling. The flags are passed per build rather than patched into
  `platform.txt`, which lives in the shared core directory and would
  otherwise leak one checkout's path into another's build. Side effect
  worth having: the merged image no longer depends on where it was built,
  so a rebuild at any path matches the published digest byte for byte. The
  macro form of the flag is not enough for that. It cleans `__FILE__` but
  leaves the paths in the ELF's debug info, and the app descriptor carries
  a SHA-256 of that ELF

New in 0.2.0, all of it design work rather than firmware. The compiled
image was unchanged from 0.1.0 apart from the version string:

- `docs/pueo/pcb-design.md`: grew from a sketch into the carrier-board
  design: netlist, power tree, load budget, placement and the module
  dimensions as datasheets arrived
- `docs/pueo/pueo-enclosure.scad`: the enclosure, which until now was an
  untracked file outside the repo while being what every dimension in the
  design notes is measured against
- `tools/gen_netlist.py`: generates the netlist, placement and BOM, and
  checks every signal GPIO against `board_pueo.h` so the two cannot drift

Carried over from 0.1.0, including several things the list here previously
missed:

- `board_pueo.h`: board profile, resolving all pin conflicts
- `gps.cpp`: `gpsPortOpen()`/`gpsPortClose()` bracket every UART open/close and
  hand GPIO 1 between the console and the GPS
- `tools/check_pinmap.py`: pin map checker
- `tools/build.sh`: pinned, isolated toolchain, build, and CC1101 patches
- `wifi.cpp`: removed an out-of-bounds write in both deauth frame builders
- `SpiBus.{h,cpp}`: single owner for the shared VSPI bus, and the fix for
  touch losing the bus to the radios
- `docs/pueo/zmuldefs.md`: what `-zmuldefs` was hiding
- `docs/pueo/warnings.md`: what `-w` was hiding
- `wifi.cpp`, `bluetooth.cpp`, `subghz.cpp`, `utils.cpp`: the per-screen UI
  macros are scoped constants now, so `-w` could come off
- `docs/pueo/spi-bus.md`: the bus map, and why touch was losing it
- `docs/pueo/ui.md`: that the UI is TFT_eSPI called directly, with no
  LVGL and no widget layer, what that costs, and which checks in
  `tools/` exist to pay for it
- `Spotter.{h,cpp}`, `SpotterSignatures.h`: passive detection of ALPR
  cameras and smart glasses from WiFi OUIs and BLE service UUIDs
- `Branding.h` and the boot screen: the fork's own name, version and logo
- `libs/SmartRC-CC1101-Driver-Lib/`: vendored, with `SpiEnd()` no longer
  calling `SPI.end()` after every register access and tearing the peripheral
  out from under touch and the SD card
- IR removed: no IR LED or receiver exists on this board, so 140 KB of
  protocol tables could never run. Flash went from 93% to 85%
- `tools/make_release.sh`: source archive and merged flash image
- `.github/FUNDING.yml`: fork funding, upstream's Patreon kept

That list predates the board. One was flashed and run on 2026-09-20, and the
boot screen, the menus, the packet monitor, Surveillance and Hunt all work on
a 3.5" ESP32-3248S035R. No module has been soldered to one yet, so everything
needing the CC1101, the nRF24, the PN532 or the GPS is still compiled and
reasoned about rather than seen. `docs/pueo/build-guide.md` tracks which step
that is up to.
