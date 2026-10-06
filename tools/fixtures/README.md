# Capture fixtures

Real frames, off real radios, for the checks that until now only ever saw
frames this repository built itself.

A synthetic frame proves the arithmetic. It cannot prove the arithmetic was
applied to the shapes that actually turn up, because the same hand chose
both the question and the answer: a header layout nobody thought of is
absent from the parser and absent from the test for the same reason. A
capture is the part neither hand wrote.

## What belongs here

Captures made by Pueo, from a network belonging to whoever made them.
`Packet Monitor` writes `/pueo/pcap/` as it runs, and a WPA association in
earshot puts a four-way handshake in that file.

Small. Trim to the frames the check needs with `editcap`, or capture for a
few seconds rather than a few minutes. These are read on every release.

Unencrypted frame bodies only. `findPayload` refuses a frame with the
Protected bit set and so does every check here, so a fixture never needs to
carry anything decipherable. The handshake itself is in the clear by
construction, which is the whole reason it is worth capturing.

## What does not belong here

Somebody else's network, and somebody else's capture.

The obvious fixtures are the WPA captures in Wireshark's test corpus, which
every tool in this space uses. They are not here. Wireshark is
GPL-2.0-or-later and that would be compatible, but the licence covers the
project rather than each contributed capture, and the SampleCaptures page
states no terms for the files at all. Putting a binary of unknown provenance
in a public repository on the strength of everybody else doing it is not a
licence.

They remain useful for checking this checker by hand, which does not require
redistributing them:

    python tools/check_eapol.py --capture path/to/some.pcap

That reads any classic pcap with link type 105 or 127 and prints what it
found, without asserting anything. Run it against a capture you trust and
compare the result with what you know is in it.

## Format

Classic pcap, link type 105 (bare 802.11) or 127 (radiotap, which Pueo
writes with a 19-byte header). pcapng is not read: the reader here is forty
lines and stays that way, and `editcap -F pcap` converts.

## The manifest

`manifest.json` says what each file is expected to contain, so the check
fails when a fixture is replaced by one that does not exercise the same
thing. Counts are of frames the parser accepts, not of frames in the file.

Every fixture needs a `source` line naming who captured it and on what, and
a `licence` line. A fixture whose manifest entry cannot say both does not go
in.
