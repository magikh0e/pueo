#!/usr/bin/env bash
# Build a source archive of Pueo, plus a merged flash image.
#
#   tools/make_release.sh            -> dist/pueo-<version>-src.zip
#   tools/make_release.sh --with-bin -> also dist/pueo-<version>-merged.bin
#   tools/make_release.sh --force    -> re-cut a version already in dist/
#   tools/make_release.sh --publish-only -> publish what is already in dist/
#   tools/make_release.sh --no-publish -> cut into dist/ and copy nowhere
#
# The repo tracks 9 MB, down from 254. What is left is the firmware, the
# docs, the art, and the three files in Libraries/ that setup consumes.
#
# Removed along the way, all of it upstream's and none of it applicable to a
# CYD carrier design: PCB and schematic exports for their own boards, nine
# pre-compiled builds of their firmware, .elf and .map debug artifacts, the
# GLB board models, library zips for other board variants, their GitHub
# Pages site with its web flasher, and both flash tools. Everything is still
# in the upstream remote, recoverable with git show upstream/main:<path>.
#
# The archive is still an explicit include list rather than "everything
# except", because the list is the record of what belongs to this fork.
#
# Verifying one of these: extract it somewhere else, wipe the build tree, and
# build from scratch. The image should come back byte-identical, which is
# what -ffile-prefix-map in build.sh buys.
#
# Extract to a SHORT path -- C:\pv or similar -- and delete it afterwards.
# Somewhere deep like a temp directory fails: NimBLE's sources sit far enough
# down that a relative ../include/ resolves past Windows' 259-character
# limit, and the error is a header reported missing when it is right there.
# A junction from a short path to a deep one is not a way round it either:
# the compiler canonicalises through the junction, so the real path lands in
# the ELF's debug info, -ffile-prefix-map misses it, and the image quietly
# stops matching its published digest.
# None of that is needed to build the firmware and none of it is ours, so the
# archive is an explicit include list rather than "everything except".
set -euo pipefail

REPO="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
cd "$REPO"

WITH_BIN=0
FORCE=0
# Publish what dist/ already holds, without re-staging or rebuilding.
#
# Every release until this one was built twice: once to verify, once with
# --force to publish. That is not merely slow. This script is inside its own
# archive, so re-staging produces a zip with a different digest from the one
# that was verified -- the second cut is a different artefact wearing the
# same version number, and the verification then applies to a file nobody
# will download. It has to be redone against the published zip every time,
# and forgetting is silent.
#
# With this, the cut happens once and publishing moves those exact bytes.
PUBLISH_ONLY=0
# Cutting and publishing are two acts. .publish.local exists so that
# publishing does not need a path typed at it; this exists so that cutting
# does not need the file moved out of the way.
NO_PUBLISH=0
for arg in "$@"; do
  case "$arg" in
    --with-bin) WITH_BIN=1 ;;
    --force)    FORCE=1 ;;
    --publish-only) PUBLISH_ONLY=1 ;;
    --no-publish) NO_PUBLISH=1 ;;
    --allow-branch) ALLOW_BRANCH=1 ;;
    *) echo "unknown argument: $arg" >&2; exit 2 ;;
  esac
done

# ── Releases come off the release branch ────────────────────────────────────
#
# A release is three things that have to agree: the tag, the GitHub release,
# and the source archive somebody downloads. Nothing here reads git, so this
# script will happily cut and publish from whatever is checked out, and the
# result is a pueo-x.y.z-src.zip whose contents are not what the tag points
# at. That is not a failure anybody sees. The archive builds, reproduces its
# own digest, and is wrong only in the sense that matters: it is not the
# source the release claims to ship.
#
# It became reachable the moment a dev branch existed, which is why the guard
# arrives with it rather than after the first time it happens.
#
# --allow-branch is the deliberate case: a release cut from somewhere else on
# purpose, which should be a sentence somebody typed rather than a default.
RELEASE_BRANCH="${PUEO_RELEASE_BRANCH:-pueo}"
branch_guard() {
  # No .git is not an error. This script is inside its own archive, so
  # somebody verifying a release extracts the zip and runs it from a plain
  # directory. Refusing there would break the one workflow that proves a
  # release is what it says it is.
  git rev-parse --git-dir >/dev/null 2>&1 || return 0

  local on
  on=$(git rev-parse --abbrev-ref HEAD 2>/dev/null) || return 0
  [ "$on" = "$RELEASE_BRANCH" ] && return 0

  if [ "${ALLOW_BRANCH:-0}" = "1" ]; then
    echo "== cutting from '$on', not '$RELEASE_BRANCH' (--allow-branch) =="
    return 0
  fi

  cat >&2 <<EOF
refusing to cut: on branch '$on', not '$RELEASE_BRANCH'

A release is a tag, a GitHub release and a source archive that have to agree.
Cutting here would publish a pueo-*-src.zip holding this branch's source under
a version number pointing somewhere else, and nothing downstream would notice:
it would build, and it would reproduce its own digest.

  git switch $RELEASE_BRANCH          and cut from there
  tools/make_release.sh --allow-branch   if this is deliberate

PUEO_RELEASE_BRANCH overrides which branch counts as the release branch.
EOF
  exit 1
}
branch_guard

VERSION=$(sed -n 's/^#define PUEO_VERSION *"\(.*\)"/\1/p' ESP32-DIV/Branding.h | head -1)
[ -n "$VERSION" ] || { echo "could not read PUEO_VERSION from Branding.h" >&2; exit 1; }

OUT="dist"
NAME="pueo-${VERSION}-src"
STAGE="$OUT/$NAME"

# What someone needs to build this, and nothing else.
#
# Libraries/ is trimmed to the three files setup actually consumes. The other
# zips there are for board variants Pueo does not target, and the "v1
# Libraries" folder is a duplicate set for the original ESP32-DIV hardware.
INCLUDE=(
  "ESP32-DIV"
  "libs"
  "PueoBeacon"
  "docs/pueo"
  "PUEO.md"
  "CHANGELOG.txt"
  "LICENSE"
  "LICENSE.MIT"
  ".gitignore"
  "tools/build.sh"
  "tools/make_release.sh"
  "tools/check_pinmap.py"
  "tools/check_status_bar.py"
  "tools/check_droneid.py"
  "tools/check_beacon.py"
  "tools/fuzz_ie_walk.py"
  "tools/check_spotter_merge.py"
  "tools/check_spotter_capture.py"
  "tools/check_eapol.py"
  "tools/check_airtag_parse.py"
  "tools/check_sub_parse.py"
  "tools/check_fastpair.py"
  "tools/check_fastpair_probe.py"
  "tools/check_ble_adv.py"
  "tools/check_screen_dims.py"
  "tools/check_menu_dispatch.py"
  "tools/check_nav_labels.py"
  "tools/check_settings.py"
  "tools/check_settings_documented.py"
  "tools/transcript_guard.py"
  "tools/check_control_bytes.py"
  "tools/check_stealth.py"
  "tools/check_render_sync.py"
  "tools/check_text_pitch.py"
  "tools/check_text_fits.py"
  "tools/check_centre_fits.py"
  "tools/tft_fonts.py"
  "tools/check_svg_labels.py"
  "tools/check_solder_pads.py"
  "tools/check_log_fields.py"
  "tools/check_button_waits.py"
  "tools/check_text_margins.py"
  "tools/check_doc_versions.py"
  "tools/check_doc_counts.py"
  "tools/check_ascii_strings.py"
  "tools/check_prose.py"
  "tools/check_touch_targets.py"
  "tools/check_font_size.py"
  "tools/check_adv_nonconn.py"
  "tools/check_grid_capacity.py"
  "tools/check_file_server.py"
  "tools/check_nrf_presence.py"
  "tools/check_sd_paths.py"
  "tools/trace_logo.py"
  "tools/inline_logo.py"
  "tools/check_logo_scale.py"
  "tools/check_menu_tables.py"
  "tools/check_nmea_checksum.py"
  "tools/check_tracker_follow.py"
  "tools/check_spotter_oui.py"
  "tools/check_spotter_prims.py"
  "tools/check_spotter_filter.py"
  "tools/check_sig_counts.py"
  "tools/check_sig_tables.py"
  "tools/check_site_menu.py"
  "tools/check_oui_registry.py"
  "tools/check_nrf24_rpd.py"
  "tools/check_band_tag.py"
  "tools/check_netlist_counts.py"
  "tools/render_screens.py"
  "tools/gen_netlist.py"
  "tools/make_bitmap.py"
  "tools/make_placeholder_logo.py"
  "Libraries/platform.txt"
  "Libraries/TFT_eSPI-master.zip"
  "Libraries/User_Setup cyd.h"
)

# The one-shot refactor scripts are history rather than tooling: each records
# how a specific change was made and refuses to run twice. Shipped under
# tools/history/ so the archive explains itself without implying they are
# things to run.
HISTORY=(
  "tools/drop_dead_functions.py"
  "tools/drop_ir_module.py"
  "tools/fix_format_truncation.py"
  "tools/macro_containment_check.py"
  "tools/macro_scope_report.py"
  "tools/macro_value_check.py"
  "tools/route_bus_claims.py"
  "tools/scope_ui_constants.py"
  "tools/silence_unused.py"
  "tools/tidy_scoped_constants.py"
)

# Publishing on its own: everything between here and the publish step is
# what makes a release. Skip it, having first checked that there is one.
if [ "$PUBLISH_ONLY" = "1" ]; then
  missing=0
  for f in "$OUT/${NAME}.zip" "$OUT/pueo-${VERSION}.sha256"; do
    [ -f "$f" ] || { echo "no $f -- cut it first" >&2; missing=1; }
  done
  [ "$missing" = "0" ] || exit 1
  # The digests are the point of the exercise, so they are checked against
  # the files here rather than assumed to still match them.
  ( cd "$OUT" && sha256sum -c "pueo-${VERSION}.sha256" >/dev/null ) || {
    echo "dist/ no longer matches pueo-${VERSION}.sha256 -- re-cut it" >&2
    exit 1
  }
  echo "publishing ${VERSION} from $OUT, not rebuilding it"
fi

# Refuse to quietly re-cut a version that has already been made.
#
# Rebuilding an existing version does not reproduce it once the tree has moved
# on, because this script is itself inside the archive: change anything and
# the zip's digest changes, while the one published beside it does not. The
# failure is silent and the symptom turns up much later, in somebody else's
# checksum.
#
# Bump PUEO_VERSION, or pass --force when the release has not gone anywhere.
if [ -f "$OUT/pueo-${VERSION}.sha256" ] && [ "$FORCE" != "1" ] \
   && [ "$PUBLISH_ONLY" != "1" ]; then
  echo "dist/ already holds $VERSION. Bump PUEO_VERSION in ESP32-DIV/Branding.h," >&2
  echo "or pass --force if that release has not been published anywhere." >&2
  exit 1
fi

# The include list is the record of what belongs to this fork, which only
# works while it is complete. It was not: check_screen_dims.py,
# check_menu_dispatch.py and check_nav_labels.py were written, used, relied
# on, and left out of every archive from 0.3.x to 0.4.2, so those releases
# ship a tree that cannot run its own checks.
#
# An explicit list is still right -- "everything except" is how the .scad
# files would have escaped -- but a list nobody diffs against reality is a
# list that drifts. This diffs it.
for f in tools/check_*.py; do
  case " ${INCLUDE[*]} " in
    *" $f "*) ;;
    *) echo "refusing to cut: $f is not in the archive include list" >&2
       MISSING_CHECKS=1 ;;
  esac
done
[ -z "${MISSING_CHECKS:-}" ] || exit 1

if [ "$PUBLISH_ONLY" != "1" ]; then

rm -rf "$STAGE"
mkdir -p "$STAGE/tools/history"

for path in "${INCLUDE[@]}"; do
  [ -e "$path" ] || { echo "missing: $path" >&2; exit 1; }
  mkdir -p "$STAGE/$(dirname "$path")"
  cp -r "$path" "$STAGE/$(dirname "$path")/"
done

for path in "${HISTORY[@]}"; do
  [ -e "$path" ] && cp "$path" "$STAGE/tools/history/"
done

# Dragged in by cp -r, and not wanted in a source archive.
rm -rf "$STAGE/.arduino"

# The enclosure sources are not published, and neither are the STLs now.
# This used to say the .scad files "stay in the repository", which was true
# of archives and not of the repository: this one is public, so sitting in
# docs/pueo was publication. They are untracked and ignored now, and the
# site no longer offers the printable models either. What remains is the
# enclosure page, which describes the design in full.
#
# docs/pueo still ships wholesale, so this stays: a .scad that reappears
# there must not ride along into an archive.
#
# Asserted rather than assumed: a file added to docs/pueo later should not
# quietly reinstate this.
rm -f "$STAGE"/docs/pueo/*.scad
if find "$STAGE" -name '*.scad' -print -quit | grep -q .; then
  echo "refusing to cut: a .scad reached the archive" >&2
  find "$STAGE" -name '*.scad' >&2
  exit 1
fi

cat > "$STAGE/BUILDING.txt" <<TXT
Pueo ${VERSION} - source archive

  tools/build.sh setup      one-time, installs a pinned toolchain (~1 GB)
  tools/build.sh            compile
  tools/build.sh merge      single flash image at offset 0
  tools/build.sh upload COM7

Builds for the 3.5" ESP32-3248S035R, which is the only panel this supports.

The 2.8" ESP32-2432S028R was supported up to 0.4.13 and is not any more. It
was built every release and never booted, so what was published for it was
an image that compiled, not one that worked. Those releases are still there
and still carry their digests, and 0.4.13 is the last of them.

Requires arduino-cli and Python 3 with Pillow (only for the bitmap tools).

Everything is installed into its own root rather than a global Arduino
install, so it cannot disturb another project. Read the "Things that will
bite you" section of PUEO.md before deviating from the script -- the esp32
core version, the pinned library versions and the toolchain path length are
all load-bearing.

Features inherited from upstream are inherited, not audited. See the status
note in PUEO.md.

tools/history/ holds the one-shot scripts that performed specific refactors,
kept for provenance. They are not meant to be run again and will refuse.
TXT

mkdir -p "$OUT"
# Git Bash on Windows has no zip(1); Python's zipfile is always there.
rm -f "$OUT/${NAME}.zip"
python -c "
import shutil, sys
shutil.make_archive(sys.argv[1], 'zip', root_dir=sys.argv[2], base_dir=sys.argv[3])
" "$OUT/${NAME}" "$OUT" "$NAME"
rm -rf "$STAGE"

SIZE=$(du -b "$OUT/${NAME}.zip" | cut -f1)
echo "$OUT/${NAME}.zip  ($(( SIZE / 1024 )) KB)"

# One detector image, for the 3.5" ESP32-3248S035R.
#
# THE PLAIN NAME CHANGED MEANING. From 0.1.0 until the 2.8" was dropped,
# pueo-<version>-merged.bin was the 2.8" image and the 3.5" carried a -35
# suffix. It is now the 3.5", and there is no 2.8" image at all.
#
# Repointing a published filename is normally exactly the wrong thing to do:
# a digest somebody kept starts failing for a reason nobody can reconstruct.
# It is survivable here only because every published digest stays in
# CHANGELOG.txt beside the downloads, the old releases are not rewritten, and
# the alternative was a -35 suffix that no longer distinguishes anything.
# The release notes have to say it in as many words. Anyone who flashes the
# plain name out of habit on a 2.8" board gets a dark screen, not an error.
#
# Build path comes from build.sh rather than being spelled again here.
if [ "$WITH_BIN" = "1" ]; then
  out="$OUT/pueo-${VERSION}-merged.bin"
  bash tools/build.sh >/dev/null
  bash tools/build.sh merge >/dev/null
  cp "$(bash tools/build.sh path)/pueo-merged.bin" "$out"
  echo "$out  ($(( $(du -b "$out" | cut -f1) / 1024 )) KB)  3.5\" panel"

  # The bench beacon.
  #
  # It is a transmitter, and everything else built here is a receiver, so it
  # carries the role in its name: "beacon" has to be in the filename of
  # anything somebody might flash by reaching for the wrong line. The -35 is
  # kept although it no longer distinguishes a panel, because this exact
  # filename is already published against three releases' digests and the
  # suffix costs nothing.
  bout="$OUT/pueo-${VERSION}-beacon-35-merged.bin"
  PUEO_ROLE=beacon bash tools/build.sh >/dev/null
  PUEO_ROLE=beacon bash tools/build.sh merge >/dev/null
  cp "$(PUEO_ROLE=beacon bash tools/build.sh path)/pueo-merged.bin" "$bout"
  echo "$bout  ($(( $(du -b "$bout" | cut -f1) / 1024 )) KB)  bench TRANSMITTER"
fi

( cd "$OUT" && sha256sum pueo-${VERSION}-* > "pueo-${VERSION}.sha256" )

fi   # PUBLISH_ONLY

echo
cat "$OUT/pueo-${VERSION}.sha256"

# Optional publish step.
#
# PUEO_PUBLISH_DIR names a directory to copy the finished artifacts into: a
# website tree, a USB stick, wherever. Unset, this does nothing.
#
# It is an environment variable rather than a path written in here on
# purpose. This file ships inside the source archive, so a path from one
# machine would be published to everyone who downloads it -- which is the
# same mistake -ffile-prefix-map was added to stop the compiler making.
#
# It copies files and verifies the copies. It does not commit, push, or go
# near version control; whatever the destination is, publishing it stays a
# deliberate act somewhere else.
#
#   PUEO_PUBLISH_DIR=/path/to/site tools/make_release.sh --with-bin
#
# Typing that every time is how a release eventually gets published to the
# wrong directory, so .publish.local beside this repo is read first when it
# exists. That file is gitignored and is not in the include list above, so it
# never reaches the archive -- the path stays on the machine that owns it,
# which is the whole point of not writing one in here.
#
#   echo 'PUEO_PUBLISH_DIR=/path/to/site' > .publish.local
#
# An explicit PUEO_PUBLISH_DIR in the environment still wins.
if [ "$NO_PUBLISH" = "1" ]; then
  PUEO_PUBLISH_DIR=""
  echo
  echo "cut only; nothing published (--no-publish)"
elif [ -z "${PUEO_PUBLISH_DIR:-}" ] && [ -f "$REPO/.publish.local" ]; then
  # Only KEY=value lines, and only the one key. Sourcing a file to get a
  # string is how a stray command in it gets run.
  PUEO_PUBLISH_DIR=$(
    sed -n 's/^[[:space:]]*PUEO_PUBLISH_DIR[[:space:]]*=[[:space:]]*//p'         "$REPO/.publish.local" | tail -1 | sed 's/^["'"'"']//; s/["'"'"']$//'
  )
  [ -n "$PUEO_PUBLISH_DIR" ] && echo "publish dir from .publish.local: $PUEO_PUBLISH_DIR"
fi

if [ -n "${PUEO_PUBLISH_DIR:-}" ]; then
  DEST="$PUEO_PUBLISH_DIR"
  if [ ! -d "$DEST" ]; then
    echo "PUEO_PUBLISH_DIR is set but is not a directory: $DEST" >&2
    exit 1
  fi

  cp "$OUT/${NAME}.zip" "$DEST/"
  # Both panel images, each only if it was built -- a run without --with-bin
  # publishes the archive and the digests alone, as it always has.
  for img in "pueo-${VERSION}-merged.bin" "pueo-${VERSION}-35-merged.bin" \
             "pueo-${VERSION}-beacon-35-merged.bin"; do
    [ -f "$OUT/$img" ] && cp "$OUT/$img" "$DEST/"
  done
  cp "$OUT/pueo-${VERSION}.sha256" "$DEST/"

  # The changelog goes under the name the site links, so the copy beside the
  # downloads and the copy inside the archive cannot drift apart.
  cp "$REPO/CHANGELOG.txt" "$DEST/pueo-changelog.txt"

  # Check what landed rather than trusting cp. A half-written binary beside a
  # correct digest is worse than no binary at all.
  ( cd "$DEST" && sha256sum -c "pueo-${VERSION}.sha256" ) || exit 1
  if ! cmp -s "$REPO/CHANGELOG.txt" "$DEST/pueo-changelog.txt"; then
    echo "changelog copy differs from the repo copy" >&2
    exit 1
  fi

  # ── screenshots ──────────────────────────────────────────────────────────
  #
  # The site's screenshots are rendered from this firmware's own source by
  # tools/render_screens.py, not photographed, so they are an artifact of the
  # release exactly as the binaries are. The version is on the boot screen and
  # in the status bar, which means most of them change every time it does.
  #
  # Nothing regenerated them, so 0.4.24 published a site showing 0.4.23 on
  # nine screens. The release step is the only place that knows a release is
  # happening, so it is where this belongs.
  #
  # Only files already in the site are replaced. A render with no counterpart
  # there is reported rather than copied: it is a screen nothing links yet,
  # and quietly adding an unreferenced image is not this script's decision.
  if [ -d "$DEST/assets" ]; then
    SHOTS="$(mktemp -d)"
    if ! python tools/render_screens.py --out "$SHOTS" >/dev/null 2>&1; then
      echo "render_screens.py failed; the site would keep the previous" >&2
      echo "release's screenshots. Needs Python 3 with Pillow." >&2
      echo "Cut without publishing with --no-publish if that is what you want." >&2
      rm -rf "$SHOTS"
      exit 1
    fi
    shots_new=0
    shots_same=0
    shots_orphan=""
    for f in "$SHOTS"/*@3x.png; do
      [ -f "$f" ] || continue
      base="$(basename "$f" | sed 's/^pueo-screen-/screen-/; s/@3x//')"
      dst="$DEST/assets/$base"
      if [ ! -f "$dst" ]; then
        shots_orphan="$shots_orphan $base"
        continue
      fi
      if cmp -s "$f" "$dst"; then
        shots_same=$((shots_same + 1))
      else
        cp "$f" "$dst"
        cmp -s "$f" "$dst" || { echo "screenshot copy failed: $base" >&2; exit 1; }
        shots_new=$((shots_new + 1))
      fi
    done
    rm -rf "$SHOTS"
    echo "screenshots: $shots_new updated, $shots_same already current"
    [ -n "$shots_orphan" ] && \
      echo "  rendered but not on the site, so not copied:$shots_orphan"
    [ "$shots_new" != "0" ] && \
      echo "  the site repo needs a deploy for these to reach the web"
  fi

  # ── retention ────────────────────────────────────────────────────────────
  #
  # Keep the release just cut, the one before it, and the two pinned below.
  # Everything else goes, so the download list stays short and the directory
  # does not fill with images nobody will flash.
  #
  # Both pins are there for the same reason: each is the last release of a
  # state this project no longer occupies, so the digest somebody is holding
  # is the only remaining evidence of what that state produced.
  #
  # 0.2.1 is a licence boundary. It is the last release that still contained
  # RF24, which is GPL-2.0-only and could not share a binary with arduinoFFT
  # or NimBLE. 0.2.2 is where that ended. See docs/pueo/licensing.md.
  #
  # 0.4.13 is a hardware boundary. It is the last release with a 2.8"
  # ESP32-2432S028R image in it. That image was never booted by anyone here,
  # which is why the panel was dropped, and it is exactly why the release
  # stays reachable: if somebody does put it on a board, this is the only
  # build they can put there, and it would be a poor answer to say it used to
  # be downloadable.
  #
  # Nothing is lost by pruning the rest: every release's digests are in
  # CHANGELOG.txt, which is published beside the downloads and ships inside
  # every archive.
  #
  # Only ever four exact filenames per version. No globbing over the
  # directory, because this runs against somebody's website tree. The -35
  # image exists only from 0.3.4 on; rm -f makes its absence a no-op for
  # every earlier version, and leaving the name out instead would make it
  # the one artefact that is never pruned.
  PUBLISH_KEEP_ALWAYS="0.2.1 0.4.13"

  published=$(ls "$DEST" 2>/dev/null \
    | sed -n 's/^pueo-\([0-9][0-9.]*\)\.sha256$/\1/p' | sort -V)
  keep=$(printf '%s\n' $published | tail -2)
  # Unquoted on purpose: PUBLISH_KEEP_ALWAYS holds more than one version now,
  # and the membership test below is grep -qx, which matches whole lines. Two
  # versions on one line match nothing and both pinned releases get pruned.
  keep=$(printf '%s\n' $keep $PUBLISH_KEEP_ALWAYS | sort -V -u)

  pruned=0
  for v in $published; do
    if printf '%s\n' $keep | grep -qx -- "$v"; then
      continue
    fi
    # The beacon image belongs on this list too. It was added to the
    # deploy's prune patterns and not to this one, so the server dropped
    # 0.4.4 and 0.4.5's transmitters while the local tree kept them -- and
    # the next deploy offered to upload them straight back. A prune that the
    # other half of the pipeline undoes is not a prune, and nothing said so:
    # both halves reported success. Found by a dry run reporting two "new
    # local files" that were four releases old.
    rm -f "$DEST/pueo-$v-src.zip" \
          "$DEST/pueo-$v-merged.bin" \
          "$DEST/pueo-$v-35-merged.bin" \
          "$DEST/pueo-$v-beacon-35-merged.bin" \
          "$DEST/pueo-$v.sha256"
    echo "pruned $v"
    pruned=$((pruned + 1))
  done
  echo "keeping: $(printf '%s ' $keep)"
  [ "$pruned" -gt 0 ] && echo "($pruned older release(s) removed; their digests stay in CHANGELOG.txt)"

  echo
  echo "published to $DEST"
fi
