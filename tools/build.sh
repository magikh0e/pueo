#!/usr/bin/env bash
# Build the Pueo firmware with a pinned, isolated toolchain.
#
#   tools/build.sh setup    install core + libraries (once, ~1 GB)
#   tools/build.sh          compile
#   tools/build.sh upload COM7
#
# Nothing here touches a global Arduino install. The core lives under
# $PUEO_ARDUINO_ROOT (default ~/.pueo-esp32) and the libraries under .arduino/user
# in the repo.
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"

# ── Why the core does not live in the repo ───────────────────────────────────
# Windows MAX_PATH. Building with -fno-rtti (which the esp32 core does) selects
# the "no-rtti" libstdc++ multilib, and the resolved path to
#   .../xtensa-esp32-elf/include/c++/8.4.0/xtensa-esp32-elf/no-rtti/bits/error_constants.h
# came to 259 characters with the core inside the repo -- one under the 260
# limit, so the compiler reported the header as missing when it was right
# there. Everything else built. Keep this root short.
PUEO_ARDUINO_ROOT="${PUEO_ARDUINO_ROOT:-$HOME/.pueo-esp32}"

export ARDUINO_DIRECTORIES_DATA="$PUEO_ARDUINO_ROOT/data"
export ARDUINO_DIRECTORIES_USER="$REPO/.arduino/user"
export ARDUINO_DIRECTORIES_DOWNLOADS="$PUEO_ARDUINO_ROOT/downloads"

# One panel: the 3.5" ESP32-3248S035R. PUEO_PANEL and the -D it produced are
# gone along with the 2.8" support they selected.
# Which firmware. The detector is Pueo itself; the beacon is the bench
# transmitter that emits the things the detector looks for, so a receive path
# can be proved rather than assumed. They are separate images on purpose:
# Spotter and DroneScan both say they transmit nothing, and an image that
# also carried a surveillance-hardware imitator would make that untrue of the
# binary even while it stayed true of the feature.
PUEO_ROLE="${PUEO_ROLE:-detector}"
case "$PUEO_ROLE" in
  detector) SKETCH_DIR="ESP32-DIV";  SKETCH_NAME="ESP32-DIV.ino"  ;;
  beacon)   SKETCH_DIR="PueoBeacon"; SKETCH_NAME="PueoBeacon.ino" ;;
  *) echo "PUEO_ROLE must be detector or beacon, not '$PUEO_ROLE'" >&2; exit 2 ;;
esac

# Keyed on the tree, not just the role.
#
# This was build-$PUEO_ROLE alone, so every checkout built into the same
# directory and a release verification overwrote the working tree's build.
# The hash is of the resolved repo path, so trees cannot collide and each
# keeps its own incremental build across a verify.
_pueo_tree_id() {
  if command -v md5sum >/dev/null 2>&1; then
    printf '%s' "$REPO" | md5sum | cut -c1-8
  else
    printf '%s' "$REPO" | cksum | cut -d' ' -f1
  fi
}
BUILD_PATH="$PUEO_ARDUINO_ROOT/build-$PUEO_ROLE-$(_pueo_tree_id)"

# Each build directory records the tree it belongs to, and claiming one
# clears out any whose tree has gone. The verify tree is deleted after every
# release, so without this each verify would strand a directory for good.
#
# The record is a sibling file, not something inside the build directory.
# It was inside at first and arduino-cli deleted it: --build-path is wiped
# on a full rebuild, so the marker survived an incremental build and
# vanished exactly when a rebuild happened. That made the pruning quietly
# stop working for whichever tree had most recently rebuilt.
_pueo_claim_build() {
  local sidecar tree dir
  for sidecar in "$PUEO_ARDUINO_ROOT"/build-*.tree; do
    [ -f "$sidecar" ] || continue     # no match, or not ours to judge
    tree="$(cat "$sidecar" 2>/dev/null)"
    dir="${sidecar%.tree}"
    if [ -n "$tree" ] && [ ! -d "$tree" ]; then
      # and the warnings pass's directory, which arduino-cli makes from
      # $BUILD_PATH-warnings and which has no sidecar of its own
      rm -rf "$dir" "$dir-warnings" "$sidecar"
    fi
  done
  mkdir -p "$PUEO_ARDUINO_ROOT"
  printf '%s\n' "$REPO" > "$BUILD_PATH.tree"
}

# The beacon includes the detector's headers rather than copying the
# constants it has to match. arduino-cli compiles a sketch from a staging
# directory, so a relative ../ include does not resolve; the directory goes
# on the include path instead. A decoy built from a second copy of the
# numbers would test the copy, not the detector.
ROLE_INC=""
if [ "$PUEO_ROLE" = "beacon" ]; then
  # cygpath, because this is embedded inside a longer --build-property
  # string. MSYS rewrites a bare path-shaped argument on its way to a Windows
  # binary but not one buried in the middle of one, so an -I/c/... reaches
  # the compiler verbatim and finds nothing.
  ROLE_INC="-I$(cygpath -m "$REPO/ESP32-DIV" 2>/dev/null || echo "$REPO/ESP32-DIV")"
fi

# arduino-cli's winget install does not land on the Git Bash PATH.
if ! command -v arduino-cli >/dev/null 2>&1; then
  PATH="$PATH:/c/Program Files/Arduino CLI"
fi

CORE_VERSION="2.0.10"
ESP32_INDEX="https://raw.githubusercontent.com/espressif/arduino-esp32/gh-pages/package_esp32_index.json"
CORE_DIR="$ARDUINO_DIRECTORIES_DATA/packages/esp32/hardware/esp32/$CORE_VERSION"

# CYD is a plain ESP32 dev module.
#
# huge_app, not min_spiffs. min_spiffs splits the flash into two 1.88 MB app
# slots so an over-the-air update can be written into the one that is not
# running; huge_app gives a single 3.00 MB slot instead. The sketch was at
# 89% of 1.88 and is at about 56% of 3.00.
#
# The second slot was buying exactly one thing: Tools > Update Firmware,
# which writes a .bin from the SD card into the spare slot. That feature is
# upstream's, is not mentioned anywhere in this changelog because nobody
# here has ever run it, and duplicates what a USB cable does. It now refuses
# with an explanation rather than failing obscurely -- see performSDUpdate.
#
# NVS keeps its offset in both tables, so settings survive the change, and
# the merged image rewrites the partition table at 0x8000 anyway.
FQBN="esp32:esp32:esp32:PartitionScheme=huge_app"

# Library versions are pinned because several of these broke their APIs and
# Library Manager hands you the newest by default:
#   ArduinoJson 7      dropped StaticJsonDocument / createNestedObject
#   NimBLE 2.x         dropped NimBLEAdvertisedDeviceCallbacks, NimBLESecurity,
#                      renamed the NimBLEHIDDevice accessors
#   arduinoFFT 2.x     replaced the arduinoFFT class with ArduinoFFT<T>
LIBS=(
  "ArduinoJson@6.21.5"
  "NimBLE-Arduino@1.4.3"
  "arduinoFFT@1.6.2"
  "rc-switch@2.6.4"
  "XPT2046_Touchscreen@1.4.0"
  "Adafruit PN532@1.3.4"
  "PCF8574@0.4.5"
)

# ── Why -zmuldefs is no longer needed ───────────────────────────────────────
# wifi.cpp defines ieee80211_raw_frame_sanity_check to return 0, overriding
# the IDF's copy so esp_wifi_80211_tx accepts hand-built frames. Without that
# the deauth and beacon features do nothing.
#
# The call is linker-resolved -- objdump shows .text.esp_wifi_80211_tx
# referencing the symbol through a literal-pool R_XTENSA_32 plus an
# ASM_EXPAND -- so the override does not need the whole link to tolerate
# duplicate symbols. Weakening the IDF's definition is enough: a strong
# definition beats a weak one, and everything else stays under normal
# duplicate-symbol rules.
#
# (nm --undefined-only does NOT show this reference, because the symbol is
# defined in the same object that calls it. Looking only at undefined imports
# suggests nothing calls it, which is wrong.)
#
# Verify after a build: the linked symbol should be 7 bytes, our `return 0`,
# not the IDF's ~200-byte original.
weaken_ieee80211_symbol() {
  local sdk="$CORE_DIR/tools/sdk/esp32/lib"
  local lib="$sdk/libnet80211.a"
  local objcopy
  objcopy=$(ls "$ARDUINO_DIRECTORIES_DATA"/packages/esp32/tools/xtensa-esp32-elf-gcc/*/bin/xtensa-esp32-elf-objcopy.exe 2>/dev/null | head -1)
  [ -n "$objcopy" ] || { echo "objcopy not found" >&2; return 1; }
  [ -f "$lib.orig" ] || cp "$lib" "$lib.orig"
  cp "$lib.orig" "$lib"
  "$objcopy" --weaken-symbol=ieee80211_raw_frame_sanity_check "$lib"
  echo "== libnet80211.a: ieee80211_raw_frame_sanity_check weakened =="
}

setup() {
  mkdir -p "$ARDUINO_DIRECTORIES_DATA" "$ARDUINO_DIRECTORIES_USER/libraries" \
           "$ARDUINO_DIRECTORIES_DOWNLOADS"

  echo "== esp32 core $CORE_VERSION =="
  arduino-cli core update-index --additional-urls "$ESP32_INDEX"
  arduino-cli core install "esp32:esp32@$CORE_VERSION" --additional-urls "$ESP32_INDEX"

  # Upstream ships a patched platform.txt. The delta is three things:
  #   -DNFC_INTERFACE_SPI   puts the Adafruit PN532 library in SPI mode
  #   -zmuldefs             tells the linker to tolerate duplicate symbols
  #   -w                    silences every compiler warning
  #
  # Both -w and -zmuldefs are stripped below. See weaken_ieee80211_symbol and
  # the warning-flag block for why each can go.
  if [ ! -f "$CORE_DIR/platform.txt.orig" ]; then
    cp "$CORE_DIR/platform.txt" "$CORE_DIR/platform.txt.orig"
  fi
  cp "$REPO/Libraries/platform.txt" "$CORE_DIR/platform.txt"

  # Drop upstream's -w. It lived in build.extra_flags, which the compile
  # recipe appends *after* compiler.warning_flags, so it overrode whatever
  # --warnings asked for. Nothing needs hiding now.

  # Drop -zmuldefs too. weaken_ieee80211_symbol below makes the one duplicate
  # that was load-bearing resolve on its own, and without the blanket flag the
  # linker goes back to catching accidental duplicates -- which is how the
  # TFT_eSPI/CC1101 `spi` collision hid for so long.
  sed -i 's|^compiler\.c\.elf\.libs\.esp32=-zmuldefs |compiler.c.elf.libs.esp32=|'     "$CORE_DIR/platform.txt"
  sed -i 's|^build\.extra_flags\.esp32=-w |build.extra_flags.esp32=|' "$CORE_DIR/platform.txt"

  # The platform bakes -Werror=all into both raised levels, so `--warnings`
  # turned the first unused function into a failed build instead of a report.
  # Both builds use `all` (-Wall -Wextra); `more` is left as plain -Wall for
  # anyone who wants to drop -Wextra temporarily.
  sed -i 's|^compiler\.warning_flags\.more=.*|compiler.warning_flags.more=-Wall|' \
    "$CORE_DIR/platform.txt"
  sed -i 's|^compiler\.warning_flags\.all=.*|compiler.warning_flags.all=-Wall -Wextra|' \
    "$CORE_DIR/platform.txt"
  echo "== platform.txt patched (stock kept as platform.txt.orig) =="

  # TFT_eSPI and the CC1101 driver must come from the repo, not Library
  # Manager: upstream customised both.
  local LIB="$ARDUINO_DIRECTORIES_USER/libraries"
  echo "== repo libraries =="
  rm -rf "$LIB/TFT_eSPI" "$LIB/SmartRC-CC1101-Driver-Lib"
  unzip -q -o "$REPO/Libraries/TFT_eSPI-master.zip" -d "$LIB"
  mv "$LIB/TFT_eSPI-master" "$LIB/TFT_eSPI"
  sync_user_setup

  # The CC1101 driver is vendored rather than unzipped and sed'd. Its changes
  # are real source edits now -- see libs/SmartRC-CC1101-Driver-Lib/VENDORED.md.
  cp -r "$REPO/libs/SmartRC-CC1101-Driver-Lib" "$LIB/"

  weaken_ieee80211_symbol

  echo "== pinned libraries =="
  arduino-cli lib install "${LIBS[@]}"

  echo
  echo "setup complete. core: $CORE_DIR"
}

# __FILE__ ends up in the firmware. NimBLE's assert macros put the absolute
# path of every asserting source file into the image, which is how the build
# machine's home directory came to be inside the published 0.1.0 and 0.2.0
# binaries -- seventeen strings of it, including the old name of the
# workspace folder. -fmacro-prefix-map rewrites the prefix while
# preprocessing, so __FILE__ comes out under pueo/ and arduino/ instead.
#
# -ffile-prefix-map rather than -fmacro-prefix-map: the macro form rewrites
# __FILE__ only, which cleans the firmware but leaves the absolute paths in
# the ELF's debug info. The app descriptor carries a SHA-256 of that ELF, so
# two builds of identical code at different paths still produced different
# images. The file form covers debug info too, and with it the merged image
# is identical wherever it was built.
#
# Passed per build rather than patched into platform.txt on purpose:
# platform.txt lives in the shared core directory, while these two paths
# belong to this checkout. Baking them in there would leak one checkout's
# path into another checkout's build.
#
# Each root is mapped in all three spellings it can arrive in. The toolchain
# is a MinGW build and __FILE__ preserves whatever arduino-cli handed the
# compiler, which today is the backslashed C: form.
prefix_maps() {
  local flags="" root tag win mixed
  # $BUILD_PATH last, after $PUEO_ARDUINO_ROOT which contains it.
  #
  # The build path reaches the ELF through the debug info, and the ELF's
  # SHA-256 is in the app descriptor, so two trees building identical source
  # at different paths produce images that differ in 64 bytes. That used not
  # to matter because every tree built into build-$PUEO_ROLE; it matters now
  # that each tree has its own. Mapping the root alone is not enough, since
  # the part of the path that differs comes after it.
  #
  # Last, not first: gcc applies the last matching -ffile-prefix-map, which
  # is why listing it first left arduinouild-detector-<hash>\sketch\... in
  # the debug info. Established by looking in the ELF, not by reading the
  # manual.
  for root in "$REPO" "$PUEO_ARDUINO_ROOT" "$BUILD_PATH"; do
    case "$root" in
      "$BUILD_PATH") tag="build" ;;
      "$REPO") tag="pueo" ;;
      *) tag="arduino" ;;
    esac
    win="$(cygpath -w "$root" 2>/dev/null || echo "$root")"
    mixed="$(cygpath -m "$root" 2>/dev/null || echo "$root")"
    flags="$flags -ffile-prefix-map=$win=$tag"
    flags="$flags -ffile-prefix-map=$mixed=$tag"
    flags="$flags -ffile-prefix-map=$root=$tag"
  done
  echo "$flags"
}

# TFT_eSPI reads its configuration from a copy inside the installed library,
# not from the file in this repo. Editing the repo's copy therefore changes
# nothing until something re-installs it, and `setup` is a one-time step
# nobody re-runs.
#
# That is not a hypothetical. Back when this tree built two panels, 0.3.4's
# first cut shipped a 2.8" image carrying the 3.5"'s driver, size and
# backlight pin: the fix went into Libraries/User_Setup cyd.h and the stale
# installed copy was what the compiler read. The bug the fix existed to
# prevent was still in the artifact that claimed to fix it, and invisible to
# any check that reads the repo. One panel removes that particular pair of
# wrong values, not the mechanism that served them.
#
# So every compile re-syncs it. cmp first, so an unchanged file is not
# rewritten: touching it would rebuild the whole library on every run.
sync_user_setup() {
  local src="$REPO/Libraries/User_Setup cyd.h"
  local dst="$ARDUINO_DIRECTORIES_USER/libraries/TFT_eSPI/User_Setup.h"
  [ -f "$src" ] || return 0
  [ -d "$(dirname "$dst")" ] || return 0
  if ! cmp -s "$src" "$dst"; then
    cp "$src" "$dst"
    echo "== User_Setup.h re-synced from Libraries/User_Setup cyd.h =="
  fi
}

# -Wall -Wextra, and the sketch is expected to stay clean under both. If this
# prints a warning, that is the whole point -- fix it rather than lowering the
# level again.
#
# Warnings from TFT_eSPI and the ESP-IDF headers are filtered out. They are
# not ours to fix, they repeat once per translation unit, and a build that
# always prints noise is a build nobody reads. `tools/build.sh warnings`
# shows everything.
compile() {
  sync_user_setup
  python "$REPO/tools/check_pinmap.py"
  echo
  local log="$PUEO_ARDUINO_ROOT/compile.log"
  mkdir -p "$PUEO_ARDUINO_ROOT"
  local rc=0
  _pueo_claim_build
  local maps; maps="$(prefix_maps)"
  arduino-cli compile --warnings all -b "$FQBN" \
    --build-property "compiler.c.extra_flags=$maps $ROLE_INC" \
    --build-property "compiler.cpp.extra_flags=$maps $ROLE_INC" \
    --build-path "$BUILD_PATH" "$REPO/$SKETCH_DIR" >"$log" 2>&1 || rc=$?

  grep -E "ESP32-DIV[\\/][A-Za-z_]+\.(cpp|h|ino).*(warning|error):" "$log" || true
  grep -E "^(Sketch uses|Global variables)" "$log" || true

  local ours external
  ours=$(grep -cE "ESP32-DIV[\\/][A-Za-z_]+\.(cpp|h|ino).*warning:" "$log" || true)
  external=$(( $(grep -c "warning:" "$log" || true) - ours ))
  if [ "$ours" -eq 0 ]; then
    echo "sketch is -Wall -Wextra clean ($external library/core warnings filtered)"
  else
    echo "$ours sketch warning(s) above -- these are ours"
  fi

  if [ "$rc" -ne 0 ]; then
    # Compiler errors carry "error:". Linker diagnostics mostly do not, and
    # the one line that does says the least: a DRAM overflow prints three
    # useful lines naming the section, the region and the byte count, then
    # "collect2.exe: error: ld returned 1 exit status". Grepping for "error:"
    # alone kept only the last of those, so a build that ran out of static
    # memory reported nothing but a failed exit status and sent the reader
    # to the log file. Both classes are matched now.
    grep -E "error:|Error during build|undefined reference|multiple definition|will not fit in region|overflowed by|does not fit|ld returned" "$log" \
      | sed -E "s#^.*/ld(\.exe)?: #ld: #" \
      | sort -u | head -20
    return "$rc"
  fi
}

# Same warning level as the normal build, but nothing filtered: library and
# core warnings included. Use it when a warning is suspected to come from a
# library rather than the sketch.
warnings() {
  rm -rf "$BUILD_PATH-warnings"
  _pueo_claim_build
  local maps; maps="$(prefix_maps)"
  arduino-cli compile --warnings all -b "$FQBN" \
    --build-property "compiler.c.extra_flags=$maps $ROLE_INC" \
    --build-property "compiler.cpp.extra_flags=$maps $ROLE_INC" \
    --build-path "$BUILD_PATH-warnings" "$REPO/$SKETCH_DIR" 2>&1 \
    | grep -E "warning:|Sketch uses|Global variables"
}

# Single flash image at offset 0: bootloader + partition table + app.
# This is what QEMU wants as its flash drive, and it is also the one-file
# artifact to hand someone who just wants to flash the thing.
merge() {
  local esptool
  esptool=$(ls "$ARDUINO_DIRECTORIES_DATA"/packages/esp32/tools/esptool_py/*/esptool.exe 2>/dev/null | head -1)
  [ -n "$esptool" ] || { echo "esptool not found; run setup first" >&2; return 1; }
  "$esptool" --chip esp32 merge_bin -o "$BUILD_PATH/pueo-merged.bin"     --flash_mode dio --flash_freq keep --flash_size 4MB     0x1000  "$BUILD_PATH/$SKETCH_NAME.bootloader.bin"     0x8000  "$BUILD_PATH/$SKETCH_NAME.partitions.bin"     0x10000 "$BUILD_PATH/$SKETCH_NAME.bin"
  echo "merged image: $BUILD_PATH/pueo-merged.bin"
}

upload() {
  local port="${1:?usage: tools/build.sh upload <port>}"

  # Say which firmware is going onto which port, before it goes.
  #
  # There are two roles, and the port number does not change when the board
  # on the end of the cable does. A detector was
  # overwritten with the beacon image this way: same COM port, different
  # board, and nothing in the output said which of the two was being sent.
  # The upload succeeded and reported success, because it was a successful
  # upload of the wrong thing.
  # Compile first, always.
  #
  # This uploaded whatever was last left in the build directory. Twice in one
  # session that was a build from before the edit being tested: the flash
  # reported four verified hashes, the board booted the old firmware, and the
  # symptom was a change that "did not take" -- which sends you debugging the
  # change rather than the flash. arduino-cli skips unchanged translation
  # units, so this costs seconds when there is nothing to do.
  compile

  echo "== uploading $PUEO_ROLE firmware to $port =="
  if [ "$PUEO_ROLE" = "beacon" ]; then
    echo "   (this is the bench TRANSMITTER, not Pueo)"
  fi

  arduino-cli upload -b "$FQBN" -p "$port" --input-dir "$BUILD_PATH" "$REPO/$SKETCH_DIR"
}

case "${1:-compile}" in
  setup)    setup ;;
  compile)  compile ;;
  path)     echo "$BUILD_PATH" ;;
  warnings) warnings ;;
  merge)    merge ;;
  upload)   shift; upload "$@" ;;
  *) echo "usage: tools/build.sh [setup|compile|warnings|merge|path|upload <port>]" >&2; exit 2 ;;
esac
