/* ─────────────────────────────────────────────────────────────────────────────
 * The vendored risinek hccapx serializer, compiled into the sketch.
 *
 * arduino-cli compiles the sketch directory and nothing else, so it never
 * reaches third_party. This shim pulls the one upstream .c in. It resolves
 * through the -I that build.sh adds for the vendored tree (VENDOR_INC); the
 * file stays pristine where it lives.
 *
 * frame_analyzer_parser.c is a second shim (FrameAnalyzerVendor.c) rather than
 * another include here: each vendored .c has its own file-static TAG, so
 * pulling both into one translation unit collides. Separate shims keep them
 * separate units, exactly as the upstream build compiled them.
 *
 * frame_analyzer.c, the promiscuous-capture driver, is deliberately absent.
 * Pueo captures frames its own way and wants only the parser and the hccapx
 * assembly.
 *
 * Behind the build flag, so with it off this compiles to nothing.
 * ──────────────────────────────────────────────────────────────────────────── */

#include "FeatureFlags.h"

#if PUEO_HANDSHAKE_EXPORT
#include "hccapx_serializer.c"
#endif
