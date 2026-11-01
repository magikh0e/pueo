#pragma once
/* ─────────────────────────────────────────────────────────────────────────────
 * Build flags that turn whole features on or off at compile time.
 *
 * Plain macros only, so a C translation unit can include this as happily as a
 * C++ one. Default every flag to off here; turn one on by editing its value to
 * 1 or by passing -DNAME=1 to the compiler. A flag that is off must leave the
 * firmware image byte-for-byte what it was, so guard the call sites too, not
 * just the implementation.
 * ──────────────────────────────────────────────────────────────────────────── */

/* Handshake export: write a captured WPA 4-way handshake to the SD card as a
 * hashcat .hccapx record, so it can be cracked off the device. This compiles
 * the vendored serializer under third_party/ and the bridge in
 * HandshakeExport.cpp. Off by default: the shipped firmware does not carry it.
 * See docs/pueo/eapol-capture.md and third_party/README.md. */
#ifndef PUEO_HANDSHAKE_EXPORT
#define PUEO_HANDSHAKE_EXPORT 0
#endif
