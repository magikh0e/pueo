# Reading Flipper `.sub` files

Pueo's sub-GHz captures live in a packed struct in EEPROM and export as a
binary blob with a magic number, which interoperates with exactly nothing.
`.sub` is what the rest of the sub-GHz world trades in. Being able to read
one is the difference between a capture that travels and a capture that
stops at this device.

`ESP32-DIV/SubFile.{h,cpp}` parses the text. It touches no SD card, no
display and no radio, which is what lets `tools/check_sub_parse.py` run it on
a host.

## The format

A key file, which is the case worth handling:

```
Filetype: Flipper SubGhz Key File
Version: 1
Frequency: 433920000
Preset: FuriHalSubGhzPresetOok650Async
Protocol: Princeton
Bit: 24
Key: 00 00 00 00 00 12 34 56
TE: 403
```

Key-value text, one field per line, split on the first colon. `Key` is eight
space-separated hex bytes, big-endian, zero-padded on the left. Unknown
lines are ignored rather than refused, because Flipper firmwares add fields.
CRLF is accepted and the file need not end in a newline.

## RAW is refused, on purpose

A RAW file has the same `Filetype` prefix, the same `Frequency` and the same
`Preset`, and then carries microsecond timings where the key would be:

```
Filetype: Flipper SubGhz RAW File
Protocol: RAW
RAW_Data: 331 -179 337 -181 332 -180
```

A `SubGhzProfile` holds a value and a bit count. There is nowhere for
timings to go. Parsing one anyway would produce a profile that looks valid
in the list and transmits nonsense, which is worse than a file that will not
open, so RAW is refused by name, and by three separate signals: the
`Filetype`, a `Protocol: RAW` line, and the presence of `RAW_Data`. Any one
of them is enough.

Supporting RAW properly means storing timing arrays and driving the CC1101
from them. That is a different feature, not a bigger parser.

## Protocol names, and the one mapping that is claimed

`SubGhzProfile::protocol` is an rc-switch protocol number. Flipper's
`Protocol` field is a name from a different library with different timings.
The two do not line up, so only one mapping is made:

| Flipper name | rc-switch | why |
|---|---|---|
| `Princeton` | 1 | PT2262/EV1527, and rc-switch protocol 1 is the same thing |
| a rolling code | refused | `RollingCode`, see below |
| everything else | **0** | the name is kept in `protocolName`; no number is guessed |

CAME, NICE FLO, Holtek, Linear and the rest are fixed-code and parse fine,
coming back with `protocol = 0`, meaning "here is the name, decide for
yourself". A guessed number would transmit something subtly wrong while
presenting as correct, which is the failure this project keeps declining to
build.

`protocol = 0` is an absence, not a number, and three places have to keep
saying so, because rc-switch does not: `setProtocol()` clamps anything below
1 up to 1, so an unguarded send transmits Princeton timing under another
protocol's name and looks like it worked. The import names the protocol it
cannot send, the browser shows `none` rather than a digit, and
`transmitProfile()` refuses outright. `tools/check_sub_parse.py` asserts all
three, and that the refusal comes before `setProtocol`.

### Rolling codes are a different answer

KeeLoq, Somfy, Star Line, Security+, Nice Flor-S, CAME Atomo and the rest
carry a counter, so a captured one is good for nothing: the receiver has
moved past it before you can transmit. They are named in `kRollingCodes`
rather than detected, because the names are facts and the detection would
be guesswork.

Every one is longer than 32 bits, so they already failed on `Bit` and the
screen said "bit count out of range". That is true and sends somebody
looking for a shorter capture. The refusal is checked before the bit count
now, so the message is the reason. This changes the sentence, not the
outcome, and nothing in it helps transmit one.

Even Princeton is approximate. rc-switch protocol 1 assumes a 350 µs pulse
and the file carries its own `TE`, usually nearer 400. `TE` is parsed and
kept for whoever wants to act on it; nothing acts on it yet.

## What is checked

A `.sub` arrives from somebody else's SD card, so it is untrusted input in
the same sense a probe request is.

- `Frequency` must be 280–960 MHz. Outside that is `FreqOutOfRange` rather
  than tuned to.
- `Bit` must be 1–32. `SubGhzProfile::value` is a `uint32_t`; anything
  larger is `TooManyBits`, not a silent truncation.
- `Key` must be whole hex byte pairs, at most eight, each followed by a
  separator or the end. Anything else is `BadField`.
- `Frequency` and `Bit` are parsed with an overflow check, so a 40-digit
  number is refused rather than wrapped.
- Every string field is copied into a fixed buffer, trimmed and terminated.

`tools/check_sub_parse.py` transcribes `parse()` and `write()` into Python
and runs 61,448 checks: the real-world cases above, RAW three ways, each
required field dropped in turn, the range edges, every rolling-code name at
both a plausible and an implausible bit count, 60,000 fuzz inputs, and every
single-byte mutation of a valid file against five replacement characters.
Every input must produce one of the eight results and nothing else.

Both function bodies are pinned by hash, so changing the C without bringing
the transcription with it fails rather than drifting.

## Where it is wired

**SUBGHZ > Import .sub** lists `.sub` files in `/pueo/subghz` and imports
the selected one. The order is parse, make room, store, so the steps that
can fail happen before anything is written. The profile name is the
filename without its extension: a `.sub` carries no name of its own, and
prompting for one would put a keyboard between choosing a file and finding
out whether it parses.

Getting there took two changes under the UI. The profile record had been
declared three times, so an import had no single type to write through, and
`saveProfile()` could not store anything without also prompting and drawing.
It is split into `makeRoomForProfile`, `makeProfile` and `storeProfile`,
and `tools/check_profile_record.py` pins both the record layout and that
seam.

There are `MAX_PROFILES = 5` slots. A sixth import exports the five to the
card and starts again rather than asking which one to overwrite, and the
screen says when that happened.

No Stealth gate on either file screen: they read a file and write an EEPROM
record, and there is nothing there to refuse.

## Writing `.sub`

`SubFile::write()` renders a key file, and **SUBGHZ > Export .sub** writes
one per saved profile to `/pueo/subghz/<name>.sub`. This matters more than
it sounds: a capture otherwise leaves this device only as a packed binary
nothing else reads, and the five-slot rotation pushes older ones into that
format as new ones arrive.

It is the easy direction. Every field has exactly one spelling, so there is
nothing to guess:

- `Key` is eight bytes, most significant first, whatever the bit count. A
  24-bit key is five zero bytes and then the three that carry it.
- `Preset` is `FuriHalSubGhzPresetOok650Async`, which is what the one
  protocol this can name is captured with. If that table grows past one
  entry, the preset has to come from the table with it.
- `Protocol` is the reverse of the mapping above, via `protocolNameFor()`.
  A profile with `protocol = 0` cannot be written, because the record never
  stored the name, and inventing one is the same failure from the other
  side. The export screen marks those rows `(no protocol)` rather than
  waiting for the press to say so.
- `TE` is omitted rather than invented. The EEPROM budget is exactly full
  at five 28-byte records, so there is nowhere to store one, and a reader
  that needs `TE` is better off applying its own default than trusting a
  guess from here.

The check round-trips `write()` through `parse()` at every bit count from 1
to 32 across four frequencies and insists the values come back unchanged,
which is the only property of a writer worth asserting.
