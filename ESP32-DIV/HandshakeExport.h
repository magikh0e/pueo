#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * HandshakeExport — turn a captured WPA 4-way handshake into a hashcat .hccapx
 * on the SD card.
 *
 * The assembly is the vendored risinek serializer under third_party/. The one
 * thing not taken from it is how the EAPOL frame is found: risinek's parser
 * reads fixed offsets without a length guard, which is the trap eapol-capture.md
 * was written to avoid. So Pueo's own Eapol, which bounds every read, is the
 * gatekeeper. A frame reaches the serializer only once Eapol has confirmed it
 * is a data frame carrying an EAPOL payload and that its header is one of the
 * two layouts risinek handles (24 bytes, or 26 with QoS). Anything with a
 * fourth address or an HT Control field is dropped here rather than misread
 * there.
 *
 * Split the way the pcap writer is: offer() runs in the promiscuous callback
 * and does nothing but copy bytes, pump() runs on the task side and does the
 * parsing, the SD write and everything that is too slow for the callback.
 *
 * The whole thing is behind PUEO_HANDSHAKE_EXPORT. With the flag off these are
 * not defined and nothing calls them, so the image is unchanged.
 * ──────────────────────────────────────────────────────────────────────────── */

#include "FeatureFlags.h"

#if PUEO_HANDSHAKE_EXPORT

#include <stdint.h>
#include "Eapol.h"

namespace HandshakeExport {

/* Callback side. Offer a frame that Eapol has already classified as an
 * EAPOL-Key message. Copies it into a small ring if, and only if, its layout
 * is one the serializer can read safely. Does no parsing and no I/O. Call it
 * outside the Eapol critical section: it takes its own lock. */
void offer(const uint8_t* frame, uint16_t len, Eapol::Msg m, uint32_t nowMs);

/* Task side. Drain whatever offer() has queued, drive the serializer, and when
 * a network's handshake becomes usable write it once to CAPTURE_DIR as
 * hs_NNNN.hccapx. Safe to call often; it does nothing when the ring is empty. */
void pump();

}  // namespace HandshakeExport

#endif  // PUEO_HANDSHAKE_EXPORT
