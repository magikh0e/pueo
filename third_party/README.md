# Vendored third-party source

These are upstream files I copied in verbatim so I can later port handshake
export (EAPOL/PMKID to hashcat `.hccapx` and Wireshark `.pcap`) into the
firmware. They are not built yet. Nothing here is under `ESP32-DIV/` or its
`src/`, so `arduino-cli` never compiles it and the firmware image is unchanged
by its presence. When I wire it up it will move into the sketch and carry a
build flag.

I kept every file byte-for-byte as upstream published it, including the
original header comments and copyright lines. The only thing I changed is where
the files sit: I flattened each upstream's `interface/` and component layout
into one folder per project. Content is untouched, which the checksums below
prove.

## esp32-wifi-penetration-tool (risinek)

Origin: https://github.com/risinek/esp32-wifi-penetration-tool, **MIT,
Copyright (c) 2021 risinek**. This tool ran on the classic ESP32, which is the
chip I run, so these are the cleanest base I have for the port. I took the
handshake serializer and the EAPOL parser from here, and the two headers they
need.

I deliberately did **not** take `frame_analyzer.c`. That file is the
promiscuous-capture driver, and I already capture frames my own way; I only
need the parser and the type definitions. `frame_analyzer.h` is here as a
header because `hccapx_serializer.c` includes it, but the matching `.c` stays
out.

| file | upstream path | git blob | sha256 |
|---|---|---|---|
| `hccapx_serializer.c` | `components/hccapx_serializer/hccapx_serializer.c` | `7f4d91f8` | `31182bece2cb324d738cbe42c99273627bca76dc230502d6d9a9cc7710bddb90` |
| `hccapx_serializer.h` | `components/hccapx_serializer/interface/hccapx_serializer.h` | `5857d7f2` | `5ac550e0c197425a5f1387187bb182bf10f9f879c964c68b689e26242b95ce36` |
| `frame_analyzer_parser.c` | `components/frame_analyzer/frame_analyzer_parser.c` | `3b213cfd` | `b1e7198b128035b9bcc6a45230fa5c99f8121001400bfbcf0d465273444f0595` |
| `frame_analyzer_parser.h` | `components/frame_analyzer/interface/frame_analyzer_parser.h` | `2f777a74` | `9ca4b092a1fed57e1d5cd2ef0e09dd13e883dae1a1ca67d16397fcdf9bca8a1f` |
| `frame_analyzer_types.h` | `components/frame_analyzer/interface/frame_analyzer_types.h` | `270ab657` | `a3c72c4b4e9b0ed8297a46e24229a16f0f92e48301da56c26bdacbdaf0c8e79f` |
| `frame_analyzer.h` | `components/frame_analyzer/interface/frame_analyzer.h` | `a3e4bc3b` | `493160bb88ad83b3a63d5ab2dcc15553dde26625a0ccae4903aae999c9f1de63` |

License text: `esp32-wifi-penetration-tool/LICENSE` (risinek's MIT, blob
`6f3ac3e3`).

## projectzero (C5Lab, pcap serializer only)

Origin: https://github.com/C5Lab/projectZero, file at
`ESP32C5/components/pcap_serializer/`. This started life as risinek's MIT file
and still carries his 2021 copyright header, but C5Lab rewrote most of it: it
grew bounds checking, a size cap, and a proper append-result enum
(`PCAP_SERIALIZER_APPEND_*`). That rewrite is better than risinek's original, so
for the PCAP writer I took C5Lab's version instead of his.

Two MIT grants cover it and I kept both. The file's own notice is risinek's
(MIT, 2021). C5Lab distribute projectZero under their own MIT license, which
covers their modifications; that is `projectzero/LICENSE` (Copyright (c) 2025
Janek). projectZero's `ESP32C5/THIRD_PARTY_NOTICES.md` is the authority on how
they layer this: the subproject is Apache-2.0 for their own new code, but files
that carry their own notice keep it, and this one carries risinek's.

| file | upstream path | git blob | sha256 |
|---|---|---|---|
| `pcap_serializer.c` | `ESP32C5/components/pcap_serializer/pcap_serializer.c` | `2569020e` | `b4b2733b104499f35eb59dedcb0d71a8b9b9fb47547e77b66c267c58689fe01e` |
| `pcap_serializer.h` | `ESP32C5/components/pcap_serializer/include/pcap_serializer.h` | `fac59396` | `97a50ed1646ee4aadaed5b8389ff2a7d9a865b5718767545d5a7b4e741995cbb` |

License text: `projectzero/LICENSE` (C5Lab's MIT, blob `417b2a45`).

## License, and why it is clean for this project

Everything here is MIT. Pueo is GPL-3.0-or-later. MIT folds into
GPL-3.0-or-later with nothing to reconcile: I keep each file's MIT header and
the LICENSE files in place, and the combined firmware stays GPL-3.0-or-later as
it always has. These files do not change my license.

What I left behind on purpose are the two database blobs projectZero ships.
`vendors.bin` is GPL-2.0-or-later and would be fine, but `wps_pin.bin` is
GPL-3.0-**only**, and compiling that into the image would drag the whole
release down to GPL-3.0-only and cost me the "or later". If I ever want the
vendor-OUI lookup, the blob rides on the SD card as runtime data the way
projectZero does it, never linked into the firmware.

## Status

Vendored on 2026-10-31. The hccapx serializer and the EAPOL parser are wired
into the sketch behind `PUEO_HANDSHAKE_EXPORT` (see `ESP32-DIV/FeatureFlags.h`
and `docs/pueo/eapol-capture.md`). The flag defaults to 0, so the shipped
firmware does not carry them; the files here stay pristine and are pulled in
through an include path only when the flag is on. `frame_analyzer.c`, the
promiscuous-capture driver, is not compiled: Pueo captures its own frames and
`Eapol` is the bounded gatekeeper that decides what reaches risinek's parser.

`pcap_serializer` is still only vendored, not wired. Pueo already writes its
own pcap from Packet Monitor, so this one waits until there is a reason to
prefer it. Nothing here has changed behaviour on the board, so no release was
cut; turning the flag on and capturing a handshake is what would.
