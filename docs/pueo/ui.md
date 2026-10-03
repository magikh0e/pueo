# The display stack, and why there is no toolkit

Everything on this panel is drawn by calling TFT_eSPI directly. There is no
LVGL, no widget library, no retained object tree, no invalidation system and
no event dispatch. A screen paints itself with `fillRect` and `drawString`,
and a tap is tested against coordinates by hand.

That is a choice, and it is worth writing down, because the first thing
anybody reading `drawMenuGrid()` will think is that a toolkit would be
tidier. It would. The reasons it is not here are physical, and most of the
checks in `tools/` exist to pay for its absence.

```
ESP32-DIV/ESP32-DIV.ino    the menus, the grid, the About page
ESP32-DIV/shared.h         panel geometry, font and status bar constants
tools/tft_fonts.py         glyph width tables, read from the TFT_eSPI zip
```

## What the UI is made of

The whole interface, counted across `ESP32-DIV/`:

| call | sites |
| --- | --- |
| `tft.fillRect` | 260 |
| `tft.drawString` | 159 |
| `tft.drawFastHLine` | 151 |
| `tft.setTextFont` | 127 |
| `tft.drawBitmap` | 118 |
| `tft.setTextDatum` | 57 |
| `tft.fillScreen` | 35 |
| `tft.drawCentreString` | 34 |

Immediate mode. Nothing is retained between frames, so a screen that
replaces another is responsible for erasing what was there, and a screen
that updates one value is responsible for painting over the old one.

## Why not LVGL

Two hard numbers.

**RAM.** A single 320x480 framebuffer at 16 bits per pixel is 307,200 bytes.
The ESP32 on this board has 327,680 bytes of DRAM and this build already
uses 124,428 of it, leaving 203,252. One full framebuffer does not fit, let
alone the two a toolkit would want for tear-free compositing. LVGL can run
with small partial-render buffers instead, but then it is redrawing regions
on demand, which is what the code already does by hand.

**Flash.** The app is 1,796,485 bytes of a 3,145,728 byte partition, 57%.
That partition is a single slot: `huge_app` rather than `min_spiffs`, traded
at 0.4.2 because the sketch was at 89% of the smaller pair. There is room
for LVGL, but the headroom is one slot's worth and it is already spent on
radios.

Neither number forbids a toolkit. Together they mean a toolkit would run in
its least comfortable mode while costing the flash that the features use, to
replace drawing code that already works.

## What the absence costs, and what pays for it

This is the useful part, because each item below is a bug that shipped.

### TFT_eSPI does not clip

A string wider than its container is not truncated and does not wrap. It is
drawn anyway, straight through its neighbours and off the edge of the panel.
A toolkit would clip to the widget. Nothing here does.

`Probe Request Flood` is the example: it needs 89 px in a 96 px tile as two
lines, and on one line it ran across the screen through the tiles beside it.
The gauge readout spent two releases reading `7 dBm    best -45    143 se`
because a font change at 0.4.7 made a 31 character line 372 px on a 320 px
panel.

Three checks cover it, and the split between them is by how the text is
drawn rather than by preference:

- `check_text_fits.py` for centred lines drawn through `showLine()`
- `check_text_margins.py` for left-aligned `drawString(text, x, y)`, which
  loses only its tail, so it reads as a sentence that was cut rather than as
  something obviously broken
- `check_centre_fits.py` for `drawCentreString`, in any font

### The font metrics are not available to the layout

Font 1 is the 6x8 GLCD face and advances 6 px per character whatever the
glyph, so its width is multiplication. Fonts 2 and 4 are proportional, and
TFT_eSPI gets their widths from tables compiled into the library. At design
time there is nothing to ask.

So `tools/tft_fonts.py` parses `widtbl_f16` and `widtbl_f32` out of
`Libraries/TFT_eSPI-master.zip`, which is the copy that ships in the release
archive rather than the one in the Arduino library directory, because a
check that only runs in its own tree cannot verify a release built from
published source.

The tables are not a nicety. A 24 character line is 144 px in font 1 and
anywhere from 24 to 600 px in font 4. Multiplying by an average passes lines
that overrun; multiplying by the widest glyph, 25 px, fails every line on
the device.

Compiled in, from `User_Setup.h`:

| font | height | characters |
| --- | --- | --- |
| 1 | 8 px | 0x20 to 0x7E, fixed 6 px pitch |
| 2 | 16 px | 0x20 to 0x7E, proportional |
| 4 | 26 px | 0x20 to 0x7E, proportional |
| 6, 7, 8 | 48, 48, 75 px | digits, colon, dot and a few more |

All of the text fonts stop at 0x7E, which is why an em dash in a drawn
string arrives as three UTF-8 bytes and three wrong glyphs.
`check_ascii_strings.py` enforces that; 36 strings had one, 20 of them in
`rfid.cpp` status lines where the status line is the entire interface.

### The panel is dense, so pixels are not legibility

577 px across a 3.5 inch diagonal is 165 ppi, so one pixel is 0.154 mm.
Font 1 at size 1 is a 1.23 mm cap height, about three and a half point. A
toolkit would not help with this, but it is the reason the font constants in
`shared.h` exist and carry their reasoning with them.

`PUEO_BODY_FONT` is 2 and `PUEO_BODY_SIZE` is also 2, and they are not the
same 2: the first selects TFT_eSPI font 2, already 16 px, and the second is
a multiplier for font 1. Mixing them gives 32 px text. That is exactly what
happened in Hunt's picker, which set size 1 at the top and restored
`PUEO_BODY_SIZE` after each row's right-hand column, so every row after the
first came out 32 px tall in a 40 px row whose lines sit 20 apart, and each
label wore the address below it. `check_font_size.py` tracks which font was
last selected before each use of the constant.

### One shared mutable object

Font, size, datum and colour live on the single `tft` instance and persist
across screens. A function that sets them and does not restore them corrupts
whatever draws next, and the corruption appears in unrelated code. A toolkit
would scope style to a widget.

`check_text_margins.py` models this explicitly: it reconstructs
`(size, font, datum)` as the code has left them at each call, last call
wins, and treats a setting a function never touches as unknown rather than
assumed, because whatever ran before that screen left it however it liked.

### There is no layout engine, so positions are constants

Every coordinate is a literal or a named constant. The submenu grid is
twelve of them:

```
GRID_COLS 3   GRID_ROWS 4   GRID_SLOTS 12
GRID_GAP_X 8     GRID_GAP_Y 6
GRID_TILE_W (PUEO_SCREEN_W - GRID_GAP_X * (GRID_COLS + 1)) / GRID_COLS
                                       96 px, so the gaps are the input
GRID_TILE_H 92                         96 x 92 px is 14.8 x 14.2 mm
GRID_Y0 54                             clears the 34 px bar and the title
GRID_ICON 32     GRID_ICON_DY 6
GRID_TEXT_DY 44  GRID_LINE_H 16   GRID_LINES 3
GRID_TEXT_W GRID_TILE_W - 16
```

`check_grid_capacity.py` fails if a menu outgrows the grid, because
`drawMenuGrid()` stops at `GRID_SLOTS` and a thirteenth feature would simply
not be drawn with nothing to say so. WiFi uses twelve of twelve.

### Input is coordinates, not events

There are no widgets to deliver a tap to, so each menu hit-tests rectangles
itself. Two consequences.

The rows and tiles **are** the tap targets, so their size is an ergonomic
decision rather than a cosmetic one. A fingertip wants about 7 mm, which is
45 px here, and 9 mm is comfortable. The submenus were lists of 30 px rows,
4.6 mm, before they became the grid.

And a hit box reads like a coordinate while behaving like a dimension. Five
menus tested `x <= 220` on a panel 320 across, which is a 2.8 inch number:
that panel is 240 wide, where `10..220` covered it. The right 100 px of
every row in those menus was dead to a tap. `check_touch_targets.py` reads
the panel from `shared.h` and fails on a hit box that runs past it or stops
more than 40 px short. It also prints, without failing, every target under
7 mm, because a row pitch is a design decision and a check that failed on
one would be demanding a redesign rather than reporting a fault.

The physical path is worth knowing too: `isButtonPressed()` is satisfied by
either the PCF8574 button expander or the touch navigation strip, and the
expander is not fitted on this board. Touch is all there is.

### The renderer is a model, not the firmware

`tools/render_screens.py` draws the website's screenshots from the firmware
source. It reads the menu tables out of the sketch and reimplements the
layout, which means it can disagree with the panel while looking right.

It drew Hunt's picker correctly throughout the release the picker was broken
in, because it models the screen rather than executing it. That one needed a
board and a photograph. `check_render_sync.py` holds the renderer's
constants to the firmware's, which catches drift in the geometry but cannot
catch a layout the renderer never implemented, and the About page is drawn
by neither.

## The short version

The drawing code is simple and the arithmetic around it is not. A toolkit
would move that arithmetic into a library, at a flash and RAM cost this
board cannot comfortably pay, and the checks that currently enforce it would
become unnecessary rather than wrong. Until the hardware changes, the
arithmetic stays in `tools/`.
