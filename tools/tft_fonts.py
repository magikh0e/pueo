#!/usr/bin/env python3
"""Glyph widths for the fonts the panel actually has, from the font files.

Font 1 advances 6 px per character whatever the glyph, which is why
check_text_fits.py and check_text_margins.py could measure it with
multiplication. Fonts 2 and 4 are proportional: TFT_eSPI's textWidth() sums
widtbl_fNN[c - 32] over the string and multiplies by the text size. A
character count tells you nothing about how wide one of those lines is, and
the two obvious shortcuts are both wrong in a way that matters. Multiplying
by an average passes lines that overrun. Multiplying by the widest glyph,
25 px in font 4, fails every line on the device.

So the widths come from the tables themselves.

Where they come from
--------------------
Libraries/TFT_eSPI-master.zip, which ships in the release archive, rather
than the Arduino library directory, which does not. A check that only runs
in the tree it was written in cannot be part of verifying a release built
from the published source.

Two things in the files need handling. Each row carries a trailing
"// char 32 - 39" comment whose numbers are not widths, and Font16.c carries
row 96..103 twice under #ifdef TFT_ESPI_GRAVE_IS_DEGREE. The build does not
define that, so the #else row is the one the panel uses; taking both would
shift the table by eight entries and mismeasure every character above 0x60
while still looking like a 104 entry table that nothing checks.
"""
import io
import re
import zipfile
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
ZIP = REPO / "Libraries" / "TFT_eSPI-master.zip"

FIRST_CHR = 32

# font number -> (file in the zip, table symbol, glyph height in px)
FONTS = {
    2: ("Fonts/Font16.c", "widtbl_f16", 16),
    4: ("Fonts/Font32rle.c", "widtbl_f32", 26),
}

# Font 1 is the 6x8 GLCD face: fixed pitch, so it has no table.
FONT1_ADVANCE = 6
FONT1_HEIGHT = 8

_cache = {}


def _parse(text, symbol):
    at = text.index(symbol)
    body = text[text.index("{", at) + 1:text.index("}", at)]
    body = re.sub(r"//[^\r\n]*", "", body)
    body = re.sub(r"#ifdef\s+TFT_ESPI_GRAVE_IS_DEGREE.*?#else", "", body,
                  flags=re.S)
    body = body.replace("#endif", "")
    vals = [int(v, 0) for v in re.findall(r"0[xX][0-9a-fA-F]+|\d+", body)]
    if len(vals) != 96:
        raise ValueError("%s has %d entries, expected 96"
                         % (symbol, len(vals)))
    return vals


def table(font):
    """Width table for a proportional font, or None for font 1."""
    if font == 1:
        return None
    if font not in FONTS:
        raise KeyError("no width table for font %r" % (font,))
    if font not in _cache:
        member, symbol, _ = FONTS[font]
        with zipfile.ZipFile(ZIP) as z:
            name = next(n for n in z.namelist() if n.endswith(member))
            text = z.read(name).decode("utf-8", "replace")
        _cache[font] = _parse(text, symbol)
    return _cache[font]


def height(font):
    if font == 1:
        return FONT1_HEIGHT
    return FONTS[font][2]


def known(font):
    return font == 1 or font in FONTS


def width(text, font, size=1):
    """Pixels TFT_eSPI will advance drawing `text`, or None if it cannot say.

    None where a character is outside the font. Fonts 1, 2 and 4 all cover
    0x20 to 0x7E and nothing else, which is check_ascii_strings.py's whole
    subject: anything above that is three UTF-8 bytes and three wrong
    glyphs, and guessing a width for it would be inventing one.
    """
    if font == 1:
        return len(text) * FONT1_ADVANCE * size
    tbl = table(font)
    total = 0
    for ch in text:
        i = ord(ch) - FIRST_CHR
        if i < 0 or i >= len(tbl):
            return None
        total += tbl[i]
    return total * size


def widest(chars, font, size=1):
    """Width of the widest of `chars` in `font`, or None if one is outside it.

    For charging a printf conversion. A numeric conversion produces digits,
    and in font 2 every digit is 8 px and no hex letter or sign is wider, so
    a character count would have done. In font 4 it would not: digits are
    14 px while uppercase hex runs to 18, so charging a digit for %X
    under-measures a MAC address by 48 px, which is the direction that
    passes a line running off the panel.
    """
    best = 0
    for ch in chars:
        w = width(ch, font, size)
        if w is None:
            return None
        if w > best:
            best = w
    return best


def main():
    print("glyph widths, from Libraries/TFT_eSPI-master.zip")
    print()
    print("  font 1   %2d px tall   fixed %d px per character"
          % (FONT1_HEIGHT, FONT1_ADVANCE))
    for font in sorted(FONTS):
        tbl = table(font)
        print("  font %d   %2d px tall   %d glyphs, %d..%d px wide"
              % (font, height(font), len(tbl), min(tbl), max(tbl)))
    print()
    print("  a 24 character line is %d px in font 1 at size 1, and anywhere"
          % (24 * FONT1_ADVANCE))
    print("  from %d to %d px in font 4, which is why these tables exist."
          % (24 * min(table(4)), 24 * max(table(4))))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
