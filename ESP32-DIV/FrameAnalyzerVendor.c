/* ─────────────────────────────────────────────────────────────────────────────
 * The vendored risinek EAPOL parser, compiled into the sketch as its own
 * translation unit. See HccapxVendor.c for why this is a separate shim and not
 * a second include there (each vendored .c has its own file-static TAG).
 *
 * Resolves through VENDOR_INC (see build.sh). Pristine upstream file. Behind
 * the build flag, so with it off this compiles to nothing.
 * ──────────────────────────────────────────────────────────────────────────── */

#include "FeatureFlags.h"

#if PUEO_HANDSHAKE_EXPORT
#include "frame_analyzer_parser.c"
#endif
