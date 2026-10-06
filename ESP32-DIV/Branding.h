#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Branding — everything the boot screen and serial banner say.
 *
 * Upstream kept these strings as XOR-obfuscated byte arrays (OBF_PN, OBF_DN
 * and friends in shared.h) so they would not turn up in `strings` on a built
 * binary. That is CiferTech's choice about their build and it is a reasonable
 * one; there is no reason to carry it here. Pueo's own name is in plain text,
 * and so is the credit back to the project it is built on.
 *
 * ESP32-DIV is MIT licensed. The licence requires the copyright notice be
 * kept, which LICENSE does. The attribution below is not required by the
 * licence -- it is here because the code came from somewhere and saying so
 * costs nothing.
 * ──────────────────────────────────────────────────────────────────────────── */

/* Pueo's own version. The sketch still carries ESP32DIV_VERSION, which is
 * upstream's release this was forked from -- showing that on the splash
 * would claim to be an ESP32-DIV build it no longer is. Both appear in the
 * serial banner, which is the honest way round. */
#define PUEO_VERSION     "0.4.36"

#define PUEO_NAME        "Pueo"
#define PUEO_TAGLINE     "multi-radio field tool"
#define PUEO_AUTHOR      "magikh0e"

#define PUEO_URL         "pueo.magikh0e.pl"

/* Shown under the name on the splash, and in the serial banner. */
#define PUEO_UPSTREAM    "based on ESP32-DIV by CiferTech"
#define PUEO_UPSTREAM_URL "github.com/cifertech/ESP32-DIV"

/* ── Boot logo ───────────────────────────────────────────────────────────────
 * Define PUEO_LOGO_BITMAP to a 150x150 1-bpp array to draw a logo above the
 * name. Generate one with:
 *
 *   py -3 tools/make_bitmap.py art/pueo_owl_src.jpg --name pueo_logo  *       --size 180x180 --crop --invert
 *
 * and paste the output into icon.h.
 *
 * art/pueo_owl_src.jpg is in the repository but NOT in the source archive.
 * It is a 1.3 MB photograph -- sixteen times the size of every document in
 * docs/pueo put together -- and all it buys is the ability to regenerate a
 * bitmap that is already sitting in icon.h. Working from the archive, take
 * the logo from magikh0e.pl, or use docs/pueo/pueo-owl.svg, which is the
 * same artwork traced and ships with it.
 *
 * The size ceiling comes from the layout, not from taste. displayLogo() puts
 * the logo at y = 140 - H/2, so the text below it has to fit in what is left
 * of a 320px panel.
 *
 * Four lines sit under the artwork now -- byline, tagline, version, upstream
 * -- which is a 62px block, and H caps at 217. It was three lines and 46px,
 * capping at 249, before the byline was added. With the name line drawn as
 * well (PUEO_LOGO_HAS_WORDMARK 0) the block is 88px and H caps at 164.
 *
 * 200 leaves 8px of slack at the bottom of the panel. That is the number to
 * recompute before adding a fifth line, not the logo size.
 *
 * The ten-frame loading animation (bitmap_icon_skull_loading_1..10, 100x120)
 * is also upstream artwork and is still in use. Replace it the same way when
 * you have frames of your own. */
#define PUEO_LOGO_BITMAP bitmap_pueo_logo

/* Set when the artwork already contains the wordmark, so displayLogo() drops
 * its separate name line instead of printing "Pueo" twice. Dropping that line
 * also frees 26px, which is what lets the logo go to 200 -- see the layout
 * note above. */
#define PUEO_LOGO_HAS_WORDMARK 1

/* -- Feature marks ----------------------------------------------------------
 * Hunt and Spotter each open on their own artwork rather than on nothing.
 * 200x200 is where the detail survives one bit: at 160 the radio mast on the
 * Spotter mark smears and the Hunt mark's arcs merge. See icon.h. */
#define PUEO_MARK_W 200
#define PUEO_MARK_H 200
#define PUEO_MARK_HOLD_MS 800

/* -- Boot splash ------------------------------------------------------------
 * How long each mark holds. Neither was ever chosen: the skull ran at its
 * upstream timing of 2 passes and the owl got whatever displayLogo() was
 * called with, which was 500 ms -- four times as long on the inherited mark
 * as on this project's.
 *
 * One pass of the skull is still a nod to where this came from. The owl gets
 * the longer half now, which is the right way round for a fork that has its
 * own name on the box.
 *
 * The skull is repeats x 10 frames x 100 ms, so one pass is 1.0 s. */
#define PUEO_BOOT_SKULL_REPEATS 1
#define PUEO_BOOT_LOGO_MS       1500

#define PUEO_LOGO_W 200
#define PUEO_LOGO_H 200
