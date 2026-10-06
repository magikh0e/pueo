# EAPOL capture

Scope for recognising WPA handshakes in frames this firmware can already
record. Steps 1 to 4 are built. Step 5, forcing a reassociation, is built as
far as the decision and stops there: **nothing transmits**, and the reason is
a blocker rather than a choice, see the end.

Split deliberately into a passive half and an active one, because they are
different decisions and only the first is in the spirit of what Spotter
already does.

## Why it is worth doing here

ESP32-DIV has no EAPOL anywhere in its tree, and neither does anything
derived from it that ships source. The feature exists in this lineage as a
claim on feature lists rather than as code, which is the gap this closes.

`ESP32Marauder` does implement it, is **MIT licensed** (Copyright (c) 2020
Just Call Me Koko), and is therefore usable here with attribution. Reading
it is where most of the design below comes from, along with the part not to
copy.

Worth correcting one thing while it is in view: Marauder does not extract
PMKIDs on the device. `force_pmkid` selects a deauth-active scan mode; the
extraction happens offline from the pcap. Anything claiming on-device PMKID
extraction in this family of firmware is overstating it.

## What already exists here

More than expected, which is the reason this is a small job.

`wifi.cpp` carries a **complete pcap writer**, owned by Packet Monitor: a
radiotap header (DLT 127) with channel, RSSI and MCS, pcap global and record
headers, a slot pool behind two FreeRTOS queues so the promiscuous callback
hands off rather than writing, SD output to `ptm_NNNN.pcap`, drop counting
and periodic flush.

And Packet Monitor already passes data frames to it. Its callback rejects
only `WIFI_PKT_MISC`, so `WIFI_PKT_DATA` goes straight to the queue. **A
handshake that happens while Packet Monitor is recording is probably already
in the pcap.** The gap is recognition and workflow, not capture.

## Two things in the way, one of them now fixed

Both found while scoping, both invisible until looked for.

**The promiscuous filter is global, and Packet Monitor never set it.**
*Fixed; kept here because the reasoning is the useful part.*
`esp_wifi_set_promiscuous_filter` was called in exactly two places, both in
`wifi.cpp`, and both set `WIFI_PROMIS_FILTER_MASK_MGMT`. Packet Monitor set
a callback and enabled promiscuous mode without touching the filter, so it
inherited whatever the last feature left behind. Enter the captive portal or
the probe sniffer, leave, then enter Packet Monitor, and it silently saw
management frames only: no data frames, no EAPOL, no error.

Every feature that reads frames now states what it wants each time it
starts. Packet Monitor asks for `MASK_ALL`; the deauth detector, the
hidden-SSID listener and Spotter ask for `MASK_MGMT`, which is all their
callbacks look at. The two beacon-spam sites are left alone deliberately:
they enable promiscuous mode in order to transmit and never install a
callback, so the filter does not reach them.

**The snapshot length is 512 on this board.** `ESP32DIV_PCAP_SNAP_LEN` is
2324 on the ESP32-S3 and 512 everywhere else, and Pueo is a CYD. That is
comfortably enough for EAPOL (the largest key message runs to roughly 200
bytes including headers), but it is a ceiling worth knowing about before
someone concludes frames are being mangled.

It is also not a snapshot length in the usual sense. `wifi_promiscuous` does
`if (ctrl.sig_len > SNAP_LEN) return;`, so a frame longer than the limit is
**dropped rather than truncated**. A pcap snaplen normally means "record this
much of it"; here it means "record it only if it is this small". The capture
is therefore missing every large frame, silently, which matters more for a
packet monitor than it does for EAPOL. Left alone for now, and recognition
is hooked in ahead of that check so the two are not coupled.

## Finding the EAPOL frame

This is the part where Marauder should not be copied.

It tests two fixed offsets for the `0x888E` ethertype:

```c
if (snifferPacket->payload[30] == 0x88 && snifferPacket->payload[31] == 0x8e)
  eapol_offset = 32;
else if (snifferPacket->payload[32] == 0x88 && snifferPacket->payload[33] == 0x8e)
  eapol_offset = 34;
```

Two problems. There is **no length guard**. Those four bytes are read
without checking that `sig_len` reaches 34, so a runt frame reads past the
buffer. And 30 and 32 are the only two header layouts it knows: a 24-byte
header plus LLC/SNAP, or a 26-byte QoS header plus the same. Anything else
is missed.

The header length is computable from the frame control field, so compute it:

| condition | bytes |
|---|---|
| base | 24 |
| To DS **and** From DS both set (4-address) | +6 |
| QoS data (subtype bit `0x08`) | +2 |
| Order bit set on a QoS frame (HT Control) | +4 |

Then LLC/SNAP is 8 bytes, with the ethertype in the last 2: the check is at
`hdr + 6`, and the 802.1X payload starts at `hdr + 8`. Every step bounded
against `sig_len` before it is read, the same discipline as the information
element walk, and for the same reason. This is a buffer off the air.

Only type Data (frame control type bits `0b10`) needs considering. Null-data
subtypes carry no payload and can be dropped early.

## Telling the four messages apart

This part of Marauder is sound, is about twenty lines, and is the
non-obvious bit. The 802.1X Key Information field sits at `eapol + 5`, big
endian, and three of its bits separate the messages:

| | Key Ack (bit 7) | Key MIC (bit 8) | Secure (bit 9) |
|---|---|---|---|
| M1 | yes | no | no |
| M2 | no | yes | no |
| M3 | yes | yes | yes |
| M4 | no | yes | yes |

Marauder does bounds-check this one (`key_info_offset + 1 < len`). Credit
where it is due, and attribution where it is required.

One thing that table needs which Marauder does not do: **check the Key Type
bit**. A group rekey carries Key Ack, Key MIC and Secure together, which is
precisely the M3 pattern, so without that check a rekey on an idle network
reads as two thirds of a handshake. Key Type set means pairwise; clear means
group, and group is not what any of this is for.

## What to keep, and where

Per access point rather than per frame, because the useful question is "do I
have a usable handshake for this network", not "how many EAPOL frames went
past".

- which of M1 to M4 have been seen, as four flags
- the station address the handshake was with
- when the last message arrived, so a stale partial can be distinguished
  from one in progress

M2 plus M3 is the pair worth having. M1 alone is worth keeping too, because
that is where a PMKID would be if there is one, but see the note above about
extraction being an offline job.

**The beacon has to be in the capture as well.** The tools that consume
these want the ESSID, and it does not appear in the handshake. Packet
Monitor records beacons already; a dedicated mode has to be careful not to
filter them out in the name of tidiness.

## The active half, which is a separate decision

Handshakes happen at association, so waiting for one passively means waiting
for somebody to connect. Every tool in this space forces the issue by
deauthenticating a client so it reconnects, Marauder sends five deauth
frames each time it sees a beacon of the target.

Pueo already has a deauther, so the capability is not new. What would be new
is wiring it to a capture, and that is worth deciding rather than defaulting
into. The passive half is a receiver. The active half transmits at other
people's equipment to make it drop off its network, which is a different act
legally and a different act ethically, and this project's own pages tell
people to use it only on networks they are allowed to touch.

Recommendation: build the passive recogniser first, and keep any deauth
pairing behind an explicit mode with its own confirmation, rather than a
setting that quietly changes what the feature does.

### What was built, and what stopped it

The decision is built and tested; the transmitting is not wired, because it
cannot work where it would have gone.

`assistDue()` answers "should a burst go out now, and at which network".
Its constraints are in the code rather than in a warning:

- disarmed on every entry to the feature, with no setting that remembers it
- it can only target a network whose handshake is **already in progress and
  incomplete**, because the table it picks from gets a row only once an
  EAPOL frame from that network has been received. It cannot be aimed at an
  arbitrary access point
- it stops the moment that network has M2 and M3
- it stops after six bursts regardless, so a handshake that never completes
  cannot leave it transmitting
- two frames per burst, so an arming costs at most twelve frames

For comparison, Marauder sends five frames every time it sees a beacon of
the target, which on a normal network is tens per second for as long as it
is running.

**The blocker: Packet Monitor runs in `WIFI_MODE_NULL`.** Every path through
`ptmStartRadioAndPcapOnce` sets it, and `esp_wifi_80211_tx(WIFI_IF_AP, ...)`
needs an interface that is up. From this mode it returns `ESP_ERR_WIFI_IF`
and sends nothing.

So wiring a button to it would have produced a control that looks like it is
doing something and is not, which is worse than not having it. The three ways
out, each of which needs a board to measure against rather than reason about:

- **Put Packet Monitor in `WIFI_MODE_AP`.** This is what Marauder does: it
  keeps an AP interface up and sniffs at the same time. It also changes the
  radio state of a capture feature, which is the sort of change that needs
  measuring rather than reasoning about.
- **A separate feature** that brings up both an AP interface and promiscuous
  mode for itself, leaving Packet Monitor alone.
- **Leave it.** The logic is there, tested, and costs nothing until something
  calls it.

## Testing it on a host

The same approach as `tools/fuzz_ie_walk.py`, and for the same reason: the
malformed frames that matter here are far easier to synthesise than to
capture.

- synthesise frames for each header layout (3-address, 4-address, QoS,
  QoS with HT Control), and assert the ethertype is found at the right
  offset in each
- synthesise the four key messages and assert each classifies correctly
- fuzz truncated and malformed frames through a buffer that refuses any read
  outside them, and assert no out-of-bounds access
- assert a frame that is 33 bytes long does not crash the thing, which is
  the case the fixed-offset version gets wrong

## Unknowns

**Whether ESP32 promiscuous mode delivers data frames whole.** This is the
one that decides whether any of it works, and it cannot be answered from
here. `sig_len` is trusted throughout this firmware, but data frames take a
different path through the driver than management frames and the reported
length may not be the whole MPDU. Worth confirming against a real capture
before writing the recogniser, not after.

**Whether the pcap the existing writer produces is accepted by the offline
tools.** Radiotap DLT 127 is right, but the header this project emits is a
fixed 19 bytes with five presence bits, and a consumer that dislikes it will
say so in a way that is easy to fix and hard to guess.

## Phasing

1. **Done.** Set the promiscuous filter explicitly wherever frames are read.
   A live hazard fixed whether or not the rest is built. 72 bytes.
2. **Done.** `ESP32-DIV/Eapol.{h,cpp}`: header-length parsing and the
   ethertype check, bounds-checked, with `tools/check_eapol_locate.py`
   holding it to account, 80,134 checks, every header layout, every
   truncation, and 80,000 random frames, with no read outside a frame.
   Payload offsets come out 32, 34, 38 and 38 for 3-address, QoS,
   4-address and QoS-with-HT-Control; the fixed-offset version knows the
   first two. Nothing calls it yet.
3. **Done.** `classify`, `addresses` and the per-AP tracker, in the same
   file. `tools/check_eapol_locate.py` became `tools/check_eapol.py` and
   covers all of it: 124,344 checks.
4. **Done.** Packet Monitor offers every frame to the tracker and shows
   `HS <usable>/<total>` beside the packet counter, amber until a network
   has M2 and M3 and green once one does, with a line on the serial console
   when that first happens. 1,224 bytes of flash and 392 of RAM, the latter
   being sixteen rows of twenty-four bytes.

   This said the frames were already going to the pcap and that was false.
   The tracker runs before the frame-size check and before the capture
   queue, which is the right order and means the indicator answers a
   different question from the file. With a three-slot pool the answers
   disagreed: measured on hardware, five key frames counted and none
   written. The pool is sixteen now, and a key frame the writer could not
   take is counted and shown as `HS u/t -n` in red, on the screen and on
   both serial lines. `tools/check_pcap_pool.py` holds the pool depth and
   insists every early return between the tracker and the write accounts
   for a key frame, because a new return added above the write is how this
   comes back.
5. **Done.** The same parser, over captures rather than frames this
   repository wrote. Synthetic frames can only ask about a layout somebody
   thought of, and one missing from the parser is missing from the test for
   the same reason, because one hand chose the question and the answer. So
   `check_eapol.py` ends by reading a classic pcap, stripping the 19-byte
   radiotap header the monitor writes, and running the transcribed
   `findPayload` and `classify` over every frame in
   `tools/fixtures/`. `manifest.json` declares what each file should yield
   and every entry has to name a source and a licence.

   One fixture has no handshake in it, which is the case that cannot be
   synthesised honestly: 658 frames off a real radio, malformed ones
   included, read without a single out-of-bounds access, reporting no EAPOL
   and refusing 109 encrypted data frames. A false positive there would be
   a row claiming a handshake nobody sent.

   The other is a complete four-way exchange, six frames because the access
   point retried M1 and M3, every one of them QoS data with the payload at
   offset 26 rather than 24. That is the layout a fixed-offset parser reads
   two bytes late, and it took the capture-pool reserve in 0.4.37 to record
   one at all: before that the burst was crowded out by the traffic around
   it every time.

   Addresses and SSIDs are replaced in place by
   `tools/anonymise_capture.py`, which keeps every length, frame control
   field and header layout and therefore everything the checks read. It
   invalidates any handshake MIC, which nothing here verifies; if something
   ever does, that script stops being suitable.

   The tracker has no locking of its own, deliberately. That is what lets
   the checker run it on a host. The promiscuous callback and the UI are
   different tasks, so the critical section lives in `wifi.cpp` around both,
   with the counting done under it and the drawing outside.
3. Key Information classification and the per-AP flags.
4. Show the flags, and record to the existing pcap writer.
5. **Decision built, transmit not wired.** `assistArm`/`assistDue` with the
   stop conditions above, and 4,000 randomised runs asserting the burst cap
   is never exceeded and that it never fires while disarmed. Nothing calls
   it: Packet Monitor is in `WIFI_MODE_NULL` and cannot transmit, and
   changing that is a radio-state change wanting hardware. See above.
