"""Render Pueo's screens as the panel would draw them, to PNG.

Not a mockup. This parses the firmware's own bitmaps out of icon.h and
TFT_eSPI's own font data out of glcdfont.c and Font16.c, implements the
handful of TFT primitives the menus use, and replays the drawing calls at
the coordinates the source gives. The text is the panel's actual 5x7 GLCD
font and its 16 px proportional font, not a web substitute.

    python tools/render_screens.py [--out dir] [--scale 3]

What is exact: every bitmap, every glyph, every colour (RGB565 converted
once), and every coordinate, because they are read from the source rather
than retyped.

What is not: the rounded corners. TFT_eSPI draws them with its own circle
helper and this uses PIL's, which can differ by a pixel at r=5. Nothing else
in these screens has curves.

Colours assume the dark theme and accent preset 0 (Orange), which are the
defaults. Battery is drawn at 85%, and the status bar's live counts are
shown in their "something was heard" state, because a screenshot of an idle
device shows less than one of a working one -- stated here rather than
implied.
"""
import argparse
import io
import math
import os
import re
import sys

from PIL import Image, ImageDraw

HERE = os.path.dirname(os.path.abspath(__file__))
REPO = os.path.dirname(HERE)
ICON_H = os.path.join(REPO, "ESP32-DIV", "icon.h")
# The beacon is a separate sketch and keeps its own mark. Reading both here
# rather than copying the array is the same rule Emit.cpp follows for the
# signatures: one definition, two readers.
BEACON_ART = os.path.join(REPO, "PueoBeacon", "BeaconArt.h")
BEACON_EMIT = os.path.join(REPO, "PueoBeacon", "Emit.h")


def beacon_stop_min():
    """The auto-stop, in minutes, out of the firmware's own constant.

    This was typed as "10 min" on both sides while Emit.h said fifteen.
    check_render_sync.py now compares all three."""
    src = open(BEACON_EMIT, encoding="utf-8").read()
    m = re.search(r"kAutoStopMs\s*=\s*(\d+)u?\s*\*\s*(\d+)u?"
                  r"\s*\*\s*(\d+)u?", src)
    if not m:
        raise SystemExit("kAutoStopMs not found in Emit.h")
    a, b, c = (int(g) for g in m.groups())
    return a * b * c // 60000
FONTS = os.path.join(REPO, ".arduino", "user", "libraries", "TFT_eSPI", "Fonts")

# The panel these are drawn for. main() sets it from --panel; the default
# is the 3.5", which is the board this firmware runs on.
#
# Every layout constant below is the firmware's own, read out of
# ESP32-DIV.ino and shared.h rather than chosen to look right. The menu grid
# in particular is a different size per panel, not the same grid scaled:
# 145x92 tiles on a 155 px column pitch against 100x60 on 120.
PANEL = 35
W, H = 320, 480


def set_panel(panel=35):
    """The layout, in one place, matching shared.h.

    Kept as a function rather than inlined because check_render_sync.py reads
    it: it compares every constant here against the C the screens are drawn
    from, and a renderer that has drifted draws a screen nobody will ever
    see. It took a panel argument when this tree built two; there is one now,
    and the argument is retained only so the signature does not have to be
    chased through the callers.
    """
    global PANEL, W, H
    global TILE_W, TILE_H, COLUMN_WIDTH, X_OFFSET_RIGHT, Y_START, Y_SPACING
    global TILE_ICON_DY, TILE_TEXT_DY, STATUS_ICONS_W, STATUS_TALL, TILE_ICON
    global BODY_FONT, BODY_LINE, BODY_LINE3, BODY_ROW, BODY_SIZE
    global GRID_COLS, GRID_ROWS, GRID_SLOTS, GRID_GAP_X, GRID_GAP_Y
    global GRID_TILE_W, GRID_TILE_H, GRID_Y0, GRID_ICON
    global GRID_ICON_DY, GRID_TEXT_DY, GRID_LINE_H, GRID_TEXT_W
    global GRID_LINES
    global GRID_FOOT_H
    PANEL = 35
    W, H = 320, 480
    TILE_W, TILE_H, COLUMN_WIDTH = 145, 92, 155
    Y_START, Y_SPACING = 44, 106
    # icon(32) + 6 gap + label(16) = 54, centred in a 92 px tile
    TILE_ICON_DY, TILE_TEXT_DY = 19, 57
    X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH
    # drawStatusBar()'s right-hand cluster: BLE icon, count, wifi bars, temp,
    # SD, plus gaps and a 4 px margin. Anchored to the right edge.
    STATUS_ICONS_W = 110
    # PUEO_STATUS_TALL in shared.h. The menu grids get the taller bar; their
    # Y_START is 44, so it costs no tile.
    STATUS_TALL = 34

    # ── the submenu grid ──────────────────────────────────────────────────
    #
    # ESP32-DIV.ino's GRID_* block. The submenus were a list of 30 px rows,
    # which is 4.6 mm on a 165 ppi panel and under what a fingertip wants,
    # and they paged at six entries. These tiles are 96x70, or 14.8 x 10.8 mm,
    # and fifteen of them hold the largest submenu, so there are no pages.
    GRID_COLS = 3
    GRID_ROWS = 4
    GRID_SLOTS = GRID_COLS * GRID_ROWS
    GRID_GAP_X = 8
    GRID_GAP_Y = 6
    GRID_TILE_W = (W - GRID_GAP_X * (GRID_COLS + 1)) // GRID_COLS
    GRID_TILE_H = 92
    GRID_Y0 = 54
    GRID_ICON = 32
    GRID_ICON_DY = 6
    GRID_TEXT_DY = 44
    GRID_LINE_H = 16
    GRID_TEXT_W = GRID_TILE_W - 16
    GRID_LINES = 3
    GRID_FOOT_H = 34
    # PUEO_BODY_FONT / PUEO_BODY_H in shared.h. This panel is dense, so the
    # list screens use font 2. The drone detector deliberately does not.
    BODY_FONT = 2
    BODY_LINE = 18
    # kLine3 in Spotter.cpp and FastPairScan.cpp, not 2 * BODY_LINE.
    BODY_LINE3 = 36
    BODY_ROW = 54
    # PUEO_BODY_SIZE: font 1 scaled, for the places that draw centred text
    # rather than a line. The gauge's distance readout is one of them.
    BODY_SIZE = 2
    global HUNT_ROW_H, HUNT_ROW_LINE2
    HUNT_ROW_H = 40
    HUNT_ROW_LINE2 = 20
    # PUEO_TILE_ICON in shared.h.
    TILE_ICON = 32


X_OFFSET_LEFT = 10
set_panel(PANEL)


# ── colours, straight from shared.h ────────────────────────────────────────
def rgb(c565):
    r = (c565 >> 11) & 0x1F
    g = (c565 >> 5) & 0x3F
    b = c565 & 0x1F
    return (r * 255 // 31, g * 255 // 63, b * 255 // 31)


UI_BG = rgb(0x20E4)
UI_FG = rgb(0x3166)
UI_LINE = rgb(0x8410)
UI_TEXT = rgb(0xFFFF)
UI_ICON = rgb(0xFBE4)          # accent preset 0, Orange
UI_LABLE = rgb(0x4208)
GREEN = rgb(0xB721)
WHITE = (255, 255, 255)
BLACK = (0, 0, 0)
CYAN = rgb(0x07FF)
RED = rgb(0xF800)              # TFT_RED, Conf::Strong
LIGHTGREY = rgb(0xD69A)        # TFT_LIGHTGREY, the address line
DARKGREY = rgb(0x7BEF)         # TFT_DARKGREY, Conf::Weak and the third line
BLUE = rgb(0x001F)             # TFT_BLUE, Hunt's COLDER


def _strip_comments(src):
    """A // comment sits between `=` and `{` in these files, so a regex that
    expects only whitespace there matches nothing. Worse, the width table's
    comments are full of digits ("char 32 - 39") which would be read as
    widths. Take them out before parsing anything."""
    src = re.sub(r"/\*.*?\*/", "", src, flags=re.S)
    return re.sub(r"//[^\n]*", "", src)


def _preprocess(src):
    """Resolve the #ifdef/#else pairs in Font16.c.

    Not optional. The width table carries two versions of chars 96-103, one
    behind TFT_ESPI_GRAVE_IS_DEGREE and one behind #else, and reading both
    gives a 104-entry table in which everything past index 64 is shifted by
    eight. The visible result is that narrow letters advance too far --
    "WiFi" comes out as "Wi Fi" -- which looks like a rendering bug rather
    than a parsing one, so it is worth resolving properly instead of special
    casing the one table.
    """
    defined = set(re.findall(r"^\s*#define\s+(\w+)\s*$", src, re.M))
    out, stack = [], []
    for line in src.split("\n"):
        m = re.match(r"\s*#(ifdef|ifndef|else|endif)\s*(\w*)", line)
        if m:
            kind, name = m.group(1), m.group(2)
            if kind == "ifdef":
                stack.append(name in defined)
            elif kind == "ifndef":
                stack.append(name not in defined)
            elif kind == "else":
                stack[-1] = not stack[-1]
            else:
                stack.pop()
            continue
        if all(stack):
            out.append(line)
    return "\n".join(out)


def load_branding():
    """The boot strings, from Branding.h rather than retyped here.

    The version is on the boot screen, so a hardcoded copy means the
    screenshots quietly disagree with the firmware one release after anyone
    stops checking."""
    src = _strip_comments(
        open(os.path.join(REPO, "ESP32-DIV", "Branding.h"),
             encoding="utf-8", newline="").read())
    out = {}
    for name in ("PUEO_NAME", "PUEO_VERSION", "PUEO_AUTHOR", "PUEO_TAGLINE",
                 "PUEO_UPSTREAM"):
        m = re.search(r"#define\s+" + name + r'\s+"([^"]*)"', src)
        if not m:
            raise SystemExit("Branding.h has no %s" % name)
        out[name] = m.group(1)
    return out


# ── the firmware's bitmaps ─────────────────────────────────────────────────
def load_bitmaps():
    out = {}
    for path in (ICON_H, BEACON_ART):
        if not os.path.isfile(path):
            continue
        src = _strip_comments(open(path, encoding="utf-8", newline="").read())
        for m in re.finditer(
                r"\b(bitmap_\w+)\s*\[\]\s*PROGMEM\s*=\s*\{(.*?)\};", src, re.S):
            vals = [int(v, 16)
                    for v in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2))]
            out[m.group(1)] = vals
    return out


# ── TFT_eSPI font 1: 5x7 GLCD, 5 columns per char, LSB is the top pixel ────
def load_glcd():
    src = _strip_comments(
        open(os.path.join(FONTS, "glcdfont.c"), encoding="utf-8").read())
    body = src[src.index("font[] PROGMEM = {"):]
    vals = [int(v, 16) for v in re.findall(r"0x([0-9a-fA-F]{2})", body)]
    return [vals[i * 5:(i + 1) * 5] for i in range(256)]


# ── TFT_eSPI font 2: 96 glyphs from ASCII 32, 16 rows, MSB left ────────────
def load_font16():
    src = _preprocess(_strip_comments(
        open(os.path.join(FONTS, "Font16.c"), encoding="utf-8").read()))
    wm = re.search(r"widtbl_f16\[96\]\s*=\s*\{(.*?)\};", src, re.S)
    widths = [int(x) for x in re.findall(r"\b(\d+)\b", wm.group(1))]
    # 96 exactly, or an #ifdef was mishandled and every later glyph shifts
    assert len(widths) == 96, "width table has %d entries, want 96" % len(widths)

    glyphs = {}
    for m in re.finditer(
            r"chr_f16_([0-9a-fA-F]{2})\[\d+\]\s*=\s*\{(.*?)\};", src, re.S):
        code = int(m.group(1), 16)
        glyphs[code] = [int(v, 16)
                        for v in re.findall(r"0x([0-9a-fA-F]{2})", m.group(2))]
    return widths, glyphs


class Tft:
    """Only the primitives the menus actually call."""

    def __init__(self, glcd, f16_widths, f16_glyphs, bitmaps):
        self.im = Image.new("RGB", (W, H), BLACK)
        self.d = ImageDraw.Draw(self.im)
        self.glcd = glcd
        self.fw, self.fg = f16_widths, f16_glyphs
        self.bm = bitmaps

    # -- shapes --
    def fill_screen(self, c):
        self.d.rectangle([0, 0, W - 1, H - 1], fill=c)

    def fill_rect(self, x, y, w, h, c):
        if w <= 0 or h <= 0:
            return
        self.d.rectangle([x, y, x + w - 1, y + h - 1], fill=c)

    def fill_round_rect(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r, fill=c)

    def draw_round_rect(self, x, y, w, h, r, c):
        self.d.rounded_rectangle([x, y, x + w - 1, y + h - 1], radius=r,
                                 outline=c)

    def draw_fast_hline(self, x, y, w, c):
        self.d.rectangle([x, y, x + w - 1, y], fill=c)

    def draw_fast_vline(self, x, y, h, c):
        self.d.rectangle([x, y, x, y + h - 1], fill=c)

    def draw_rect(self, x, y, w, h, c):
        self.d.rectangle([x, y, x + w - 1, y + h - 1], outline=c)

    def draw_pixel(self, x, y, c):
        if 0 <= x < W and 0 <= y < H:
            self.im.putpixel((x, y), c)

    def draw_line(self, x0, y0, x1, y1, c):
        self.d.line([x0, y0, x1, y1], fill=c)

    def fill_triangle(self, x0, y0, x1, y1, x2, y2, c):
        self.d.polygon([(x0, y0), (x1, y1), (x2, y2)], fill=c)

    def fill_circle(self, x, y, r, c):
        self.d.ellipse([x - r, y - r, x + r, y + r], fill=c)

    # -- 1bpp bitmap, rows of ceil(w/8) bytes, MSB first --
    def draw_bitmap(self, x, y, name, w, h, c):
        data = self.bm[name]
        stride = (w + 7) // 8
        px = self.im.load()
        for row in range(h):
            for col in range(w):
                i = row * stride + (col >> 3)
                if i < len(data) and data[i] & (0x80 >> (col & 7)):
                    xx, yy = x + col, y + row
                    if 0 <= xx < W and 0 <= yy < H:
                        px[xx, yy] = c

    def draw_bitmap_scaled(self, x, y, name, w, h, c, scale):
        """drawBitmapScaled() in utils.cpp -- the bits doubled, not new art."""
        if scale <= 1:
            self.draw_bitmap(x, y, name, w, h, c)
            return
        data = self.bm[name]
        stride = (w + 7) // 8
        px = self.im.load()
        for row in range(h):
            for col in range(w):
                i = row * stride + (col >> 3)
                if i >= len(data) or not (data[i] & (0x80 >> (col & 7))):
                    continue
                for sy in range(scale):
                    for sx in range(scale):
                        xx, yy = x + col * scale + sx, y + row * scale + sy
                        if 0 <= xx < W and 0 <= yy < H:
                            px[xx, yy] = c

    # -- font 1, size 1: 5 columns then a 1px gap --
    def _glcd_char(self, x, y, ch, c, bg):
        cols = self.glcd[ord(ch) & 0xFF]
        px = self.im.load()
        for col in range(6):
            bits = cols[col] if col < 5 else 0
            for row in range(8):
                on = bits & (1 << row)
                xx, yy = x + col, y + row
                if 0 <= xx < W and 0 <= yy < H:
                    if on:
                        px[xx, yy] = c
                    elif bg is not None:
                        px[xx, yy] = bg

    def draw_string_tc(self, s, cx, y, c, bg=None):
        """TC_DATUM: x is the centre of the string, y its top."""
        x = cx - (len(s) * 6) // 2
        for ch in s:
            self._glcd_char(x, y, ch, c, bg)
            x += 6

    def print_f1_size(self, x, y, s, c, bg, size):
        """setTextSize(n) with font 1: the same 5x7 cells, n x n pixels each.

        Written out rather than drawn at 1x and upscaled. TFT_eSPI scales
        the cell, not the string, so the one-pixel gap between characters
        scales with it -- upscaling a finished bitmap puts that gap in the
        wrong place and rounds the glyph edges."""
        cw = 6 * size
        for i, ch in enumerate(s):
            cx = x + i * cw
            for col in range(5):
                bits = self.glcd[ord(ch) & 0xFF][col]
                for row in range(8):
                    if bits & (1 << row):
                        fill = c
                    elif bg is not None:
                        fill = bg
                    else:
                        continue
                    self.d.rectangle(
                        [cx + col * size, y + row * size,
                         cx + col * size + size - 1,
                         y + row * size + size - 1], fill=fill)
            if bg is not None:
                self.d.rectangle([cx + 5 * size, y,
                                  cx + 6 * size - 1,
                                  y + 8 * size - 1], fill=bg)

    def centre_f1(self, s, cx, y, c, bg=None, size=1):
        """drawCentreString(s, x, y, 1): x is the centre, not the left."""
        self.print_f1_size(cx - (6 * size * len(s)) // 2, y, s, c, bg, size)

    def print_f1(self, x, y, s, c, bg=None):
        """Cursor-relative print in font 1, which is what drawStatusBar uses
        for the battery percentage -- it calls setTextFont(1) immediately
        before. Rendering that line in font 2 puts a 16 px glyph cell in a
        20 px bar starting at y=6, which paints two rows of background below
        the bar and looks exactly like a firmware bug. It is not one."""
        for ch in s:
            self._glcd_char(x, y, ch, c, bg)
            x += 6
        return x

    # -- font 2 --
    def text_width(self, s):
        return sum(self.fw[ord(ch) - 32] for ch in s if 32 <= ord(ch) < 128)

    def print_f2(self, x, y, s, c, bg=None):
        px = self.im.load()
        for ch in s:
            code = ord(ch)
            if not (32 <= code < 128):
                continue
            w = self.fw[code - 32]
            glyph = self.fg.get(code)
            stride = (w + 7) // 8
            for row in range(16):
                for col in range(w):
                    i = row * stride + (col >> 3)
                    on = glyph and i < len(glyph) and \
                        glyph[i] & (0x80 >> (col & 7))
                    xx, yy = x + col, y + row
                    if 0 <= xx < W and 0 <= yy < H:
                        if on:
                            px[xx, yy] = c
                        elif bg is not None:
                            px[xx, yy] = bg
            x += w
        return x


# ── drawStatusBar(), 85% battery, something heard on both radios ───────────
def status_bar(t, h=20):
    """drawStatusBar(). h is PUEO_STATUS_SHORT or PUEO_STATUS_TALL.

    Everything is placed off y, exactly as the firmware does it, so the tall
    bar is the one line below and not a second layout."""
    t.fill_rect(0, 0, W, h, UI_LABLE)
    x, y = 7, 4 + (h - 20) // 2
    t.draw_round_rect(x, y, 22, 10, 2, WHITE)
    t.fill_rect(x + 22, y + 3, 2, 4, WHITE)
    t.fill_round_rect(x + 2, y + 2, 85 * 20 // 100, 6, 1, GREEN)
    t.print_f1(x + 30, y + 2, "85%", GREEN, UI_LABLE)

    # Name and version, centred in what is left between the battery block and
    # the icon cluster, and dropped rather than overlapped if it will not fit.
    # Font 2 once the bar is tall enough for 16 px, matching the icons.
    build = "Pueo " + BRAND["PUEO_VERSION"]
    big = h >= 28
    text_w = t.text_width(build) if big else len(build) * 6
    gap_l = x + 30 + 24
    gap_r = W - STATUS_ICONS_W - 4
    if gap_r - gap_l >= text_w:
        bx = gap_l + (gap_r - gap_l - text_w) // 2
        by = (h - (16 if big else 8)) // 2
        (t.print_f2 if big else t.print_f1)(bx, by, build, UI_LINE, UI_LABLE)

    ble_icon_x, gap, icon_w = W - STATUS_ICONS_W, 3, 16
    ble_text_x = ble_icon_x + icon_w + gap
    wifi_bars_x = ble_text_x + 12 + gap
    temp_icon_x = wifi_bars_x + 24 + gap
    sd_icon_x = temp_icon_x + icon_w + gap
    icon_y = y - 2

    wifi_x, wifi_y = wifi_bars_x + 10, y + 11
    for i in range(4):
        bar_h = (i + 1) * 3
        t.draw_round_rect(wifi_x + i * 6, wifi_y - bar_h, 4, bar_h, 1, WHITE)

    t.draw_bitmap(ble_icon_x + 25, icon_y, "bitmap_icon_ble", 16, 16, CYAN)
    t.draw_bitmap(temp_icon_x + 10, y - 2, "bitmap_icon_temp", 16, 16, GREEN)
    t.draw_bitmap(sd_icon_x + 10, y - 2, "bitmap_icon_sdcard", 16, 16, GREEN)


def _c565(r, g, b):
    """TFT_eSPI's color565. Kept as an int so the mix below is the firmware's."""
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def _mix565(a, b, k):
    """splashMix() in utils.cpp: a toward b by k/255, in RGB565 components."""
    ar, ag, ab = (a >> 11) & 0x1F, (a >> 5) & 0x3F, a & 0x1F
    br, bg, bb = (b >> 11) & 0x1F, (b >> 5) & 0x3F, b & 0x1F
    return (((ar + (br - ar) * k // 255) << 11)
            | ((ag + (bg - ag) * k // 255) << 5)
            | (ab + (bb - ab) * k // 255))


# bootSplash() in utils.cpp. Same names, so a diff against the C++ reads.
SPLASH_HORIZON = 384
SPLASH_LOG_TOP = 252
SPLASH_LOG_END = SPLASH_HORIZON - 44
SPLASH_LINE_H = 16

SPLASH_LOG = [("[boot] start", True),
              ("[boot] classic BT RAM released", False),
              ("[boot] settings: loaded from SD", False),
              ("[boot] BLE/WiFi-bg deferred (v1)", False),
              ("[boot] ready", True)]


def render_boot(t, brand):
    """bootSplash() in utils.cpp, at its last step."""
    import math
    bg = _c565(8, 6, 16)
    grid_b, grid_d = _c565(170, 45, 165), _c565(70, 22, 82)
    horiz, trace = _c565(190, 60, 185), _c565(124, 229, 119)
    log_c = _c565(96, 158, 116)
    dim, dimmer = _c565(96, 108, 120), _c565(86, 96, 106)
    cx = W // 2

    t.fill_screen(rgb(bg))

    t.draw_string_tc("%s %s" % (brand["PUEO_NAME"], brand["PUEO_VERSION"]),
                     cx, 5, rgb(dim), rgb(bg))
    t.draw_string_tc(brand["PUEO_TAGLINE"], cx, 15, rgb(dim), rgb(bg))
    t.draw_string_tc("by %s . %s" % (brand["PUEO_AUTHOR"], brand["PUEO_UPSTREAM"]),
                     cx, 25, rgb(dimmer), rgb(bg))

    for gx in range(-240, 561, 40):
        t.draw_line(cx, SPLASH_HORIZON, gx, H, rgb(grid_d))
    for i in range(1, 13):
        y = SPLASH_HORIZON + (i * i * 21) // 20
        if y >= H:
            break
        t.draw_fast_hline(0, y, W, rgb(grid_b if i < 4 else grid_d))

    prev = None
    for x in range(W):
        n = math.sin(x * 0.11) * 7.0 + math.sin(x * 0.037 + 1.3) * 7.0
        peak = 34 if 96 < x < 104 else (26 if 210 < x < 216 else 0)
        y = SPLASH_HORIZON - int(abs(n)) - peak
        if prev is not None:
            t.draw_line(x - 1, prev, x, y, rgb(trace))
        prev = y
    t.draw_fast_hline(0, SPLASH_HORIZON, W, rgb(horiz))

    lw = lh = 200
    lx, ly = (W - lw) // 2, 40
    t.draw_bitmap(lx, ly, "bitmap_pueo_logo", lw, lh, WHITE)
    for y in range(0, ly + lh, 4):
        t.draw_fast_hline(0, y, W, rgb(_c565(4, 4, 10)))

    step = len(SPLASH_LOG) - 1
    for i in range(step + 1):
        y = SPLASH_LOG_TOP + (step - i) * SPLASH_LINE_H
        if y > SPLASH_LOG_END:
            continue
        k = (y - SPLASH_LOG_TOP) * 255 // (SPLASH_LOG_END - SPLASH_LOG_TOP)
        k = k * k // 255
        text, hot = SPLASH_LOG[i]
        t.print_f1(16, y, text, rgb(_mix565(trace if hot else log_c, bg, k)),
                   rgb(bg))


# PUEO_MARK_W / PUEO_MARK_H in Branding.h. Panel-independent: 200 fits both.
MARK = 200


def render_mark(t, bitmap, caption):
    """showFeatureMark() in utils.cpp.

    No status bar -- the mark is drawn over a cleared screen and the feature
    paints its own chrome once the hold is over."""
    t.fill_screen(UI_BG)
    x = (W - MARK) // 2
    y = (H - MARK) // 2 - 14
    t.draw_bitmap(x, y, bitmap, MARK, MARK, UI_ICON)
    tw = t.text_width(caption)
    t.print_f2((W - tw) // 2, y + MARK + 12, caption, UI_TEXT, UI_BG)


# dwellAlert() in Spotter.cpp draws on TFT_BLACK rather than on UI_BG, and
# uses UI_WARN, TFT_WHITE and UI_DIM_TEXT. On the dark theme those resolve to
# the accent, white and GRAY.
BLACK = (0, 0, 0)
UI_WARN = UI_ICON                  # UI.warn is the accent preset
UI_DIM_TEXT = rgb(0x8410)          # GRAY, per uiDimTextColor() on dark


def render_dwell(t, huntable):
    """dwellAlert() in Spotter.cpp, drawn at step 0 -- before the fade.

    The alert fades out over six steps by redrawing the same mark in a colour
    blended towards the background, so every later frame is this one dimmer.
    Rendering the first is rendering the alert.

    `huntable` is the gate on the suggestion: Hunt lists BLE trackers and
    nothing else, so a dwelling plate reader or body camera gets the duration
    and no advice."""
    mw = mh = 200
    x = (W - mw) // 2
    # The firmware positions mark-plus-text as one block, two thirds of the
    # slack above and one third below. Same arithmetic as dwellAlert().
    y = (H - (mh + 80)) * 2 // 3
    cx = W // 2

    t.fill_screen(BLACK)
    t.draw_bitmap(x, y, "bitmap_pueo_dwell", mw, mh, UI_WARN)

    if huntable:
        sub_line = "AirTag  12 min"
        hint1, hint2 = "may be travelling with you", "Hunt can walk you to it"
    else:
        sub_line = "Flock Safety camera  12 min"
        hint1, hint2 = "in range this whole time", None

    def centred(text, yy, colour):
        t.print_f2(cx - t.text_width(text) // 2, yy, text, colour, BLACK)

    centred("DWELL", y + mh + 6, UI_WARN)
    centred(sub_line, y + mh + 26, WHITE)
    centred(hint1, y + mh + 46, UI_DIM_TEXT)
    if hint2 is not None:
        centred(hint2, y + mh + 64, UI_WARN)


# PueoBeacon.ino's own palette, which is not the detector's: it never loads
# a theme, so these are literals in that file rather than UI.* lookups.
BCN_BG   = (0, 0, 0)
BCN_TEXT = (255, 255, 255)
BCN_DIM  = rgb(0x8410)
BCN_LIVE = rgb(0x07E0)
BCN_WARN = rgb(0xFBE0)
BCN_STOP = rgb(0xF800)

def beacon_signals():
    """Emit::name() and Emit::detectedBy(), read in enum order.

    This used to be a list written out here, under a comment saying it was
    those two functions. It was eight rows when the enum had fourteen, and it
    still said Spotter and Drones after the labels became Surveillance and
    Drone Detector, so the rendered screenshot showed a beacon that had not
    existed for three releases.

    A screenshot is documentation, and a fact copied out of the source into a
    place that does not compile goes stale the same way wherever it lands.

    Both functions are switches whose cases fall through, so a label belongs
    to every case stacked above its return.
    """
    src = open(BEACON_EMIT.replace("Emit.h", "Emit.cpp"),
               encoding="utf-8", errors="replace").read()
    hdr = open(BEACON_EMIT, encoding="utf-8", errors="replace").read()

    m = re.search(r"enum Signal[^{]*\{(.*?)\}", hdr, re.S)
    order = [x for x in re.findall(r"^\s*([A-Z]\w*)\s*(?:=[^,]*)?,", m.group(1),
                                   re.M) if x != "kSignalCount"]

    def table(fn):
        body = re.search(r"const char\* %s\(Signal s\).*?\n\}" % fn,
                         src, re.S).group(0)
        out, pending = {}, []
        for case, label in re.findall(
                r"case\s+(\w+):|return\s+\"([^\"]*)\"", body):
            if case:
                pending.append(case)
            else:
                for c in pending:
                    out[c] = label
                pending = []
        return out

    names, by = table("name"), table("detectedBy")
    return [(names[s], by[s]) for s in order if s in names and s in by]


BEACON_SIGNALS = beacon_signals()


def render_drone_alert(t, have_operator):
    """droneAlert() in DroneScan.cpp, at step 0 -- before the fade.

    Same machinery as the dwell alert: the mark is redrawn in a colour
    blended toward the background, so every later frame is this one dimmer.

    `have_operator` is the gate on the last line. Remote ID's System message
    carries the operator's own position, which is the part that makes this
    different from watching an aircraft, so it is claimed only when it has
    actually been received."""
    mw = mh = 200
    x = (W - mw) // 2
    # Same block placement as droneAlert().
    y = (H - (mh + 80)) * 2 // 3
    cx = W // 2

    t.fill_screen(BLACK)
    t.draw_bitmap(x, y, "bitmap_pueo_drone", mw, mh, UI_WARN)

    def centred(text, yy, colour):
        t.print_f2(cx - t.text_width(text) // 2, yy, text, colour, BLACK)

    if have_operator:
        who, where = "1596F3F2A1B4C5D6E7F8", "94 m up  12 m/s"
        hint, hint_colour = "operator location broadcast", UI_WARN
    else:
        who, where = "ID not broadcast yet", "position not decoded yet"
        hint, hint_colour = "no operator location yet", UI_DIM_TEXT

    centred("DRONE  BLE" if have_operator else "DRONE  WiFi", y + mh + 6, UI_WARN)
    centred(who, y + mh + 26, WHITE)
    centred(where, y + mh + 46, UI_DIM_TEXT)
    centred(hint, y + mh + 64, hint_colour)


def render_beacon_splash(t, brand):
    """drawSplash() in PueoBeacon.ino, at the last second of the countdown.

    The splash is the only moment before the board starts transmitting, which
    is why it counts down rather than sitting there: a detector was once
    overwritten with this firmware because nothing on screen said which was
    which."""
    mw = mh = 200
    x = (W - mw) // 2
    y0 = (H - mh) // 2 - 40
    cx = W // 2

    t.fill_screen(BCN_BG)
    t.draw_bitmap(x, y0, "bitmap_pueo_beacon", mw, mh, BCN_WARN)

    def centred_f2(text, yy, colour):
        t.print_f2(cx - t.text_width(text) // 2, yy, text, colour, BCN_BG)

    def centred_f1(text, yy, colour):
        t.print_f1(cx - 6 * len(text) // 2, yy, text, colour, BCN_BG)

    y = y0 + mh + 12
    centred_f2("BEACON", y, BCN_WARN)
    y += 22
    centred_f1("bench transmitter", y, BCN_TEXT)
    y += 14
    centred_f1(brand["PUEO_VERSION"], y, BCN_DIM)

    centred_f1("THIS BOARD TRANSMITS", H - 54, BCN_STOP)
    centred_f1("WiFi + BLE, lowest power, %d min" % beacon_stop_min(),
               H - 40, BCN_DIM)
    centred_f1("broadcasting in 1...", H - 22, BCN_WARN)


def render_beacon_running(t, brand):
    """drawFrame() + drawBody(), mid-session with BLE on the tracker decoy.

    Counts are what the screen is for. "Nothing detected" on the other board
    is either a dead receiver or a dead transmitter, and these numbers are
    the only thing that tells the two apart."""
    t.fill_screen(BCN_BG)
    t.print_f2(8, 6, "PUEO BEACON", BCN_WARN, BCN_BG)
    t.print_f1(8, 28, "bench transmitter - " + brand["PUEO_VERSION"],
               BCN_DIM, BCN_BG)
    t.draw_fast_hline(0, 42, W, BCN_DIM)

    y = 50
    t.print_f1(8, y, "TRANSMITTING - stops in 8:54", BCN_LIVE, BCN_BG)
    y += 16

    # BLE advertising is a state rather than an event, so exactly one BLE
    # signal is live at a time and the scheduler rotates them. Find My is up.
    #
    # Keyed by name rather than by position. As a list of eight it threw an
    # IndexError the moment the enum reached fourteen, and before that it had
    # been silently rendering the first eight of however many there were.
    # A signal with no number here draws a plausible one rather than stopping
    # the render, because the alternative is that adding a decoy breaks the
    # screenshots.
    # Read off a board mid-session rather than invented, because the shape
    # was the part that mattered and guessing got it wrong. The four WiFi
    # paths send on their own timer and run to the same figure; the ten BLE
    # signals share ten rotation slots of three seconds, so each gets a turn
    # roughly every thirty seconds and they sit two orders of magnitude lower.
    # A set of numbers spread evenly between the two, which is what was here
    # before, shows a device that cannot exist.
    live_name = "Smart glasses"
    COUNTS = {
        "Remote ID / WiFi": 363, "ALPR probe": 363,
        "Bodycam beacon": 362, "Pineapple SSID": 362,
        "Remote ID / BLE": 13, "Smart glasses": 13,
        "Vehicle module": 12, "Find My tracker": 12, "Find Hub tag": 12,
        "DULT tracker": 12, "Fleet tracker": 12, "TPMS sensor": 12,
        "Car (weak row)": 12, "Fast Pair": 12,
    }

    for i, (nm, by) in enumerate(BEACON_SIGNALS):
        is_live = (nm == live_name)
        n = COUNTS.get(nm, 20 + (i * 7) % 40)
        colour = BCN_LIVE if is_live else (BCN_TEXT if n else BCN_DIM)
        t.print_f1(8, y, ("> " if is_live else "  ") + nm, colour, BCN_BG)
        cnt = str(n)
        t.print_f1(W - 44, y, cnt, BCN_DIM, BCN_BG)
        t.print_f1(20, y + 10, by, BCN_DIM, BCN_BG)
        y += 24

    t.print_f1(8, H - 14, "all payloads say PUEO-TEST", BCN_DIM, BCN_BG)


def body(t, x, y, text, colour, bg):
    """Draw a line in whichever font this panel uses for list bodies."""
    if BODY_FONT == 2:
        t.print_f2(x, y, text, colour, bg)
    else:
        t.print_f1(x, y, text, colour, bg)


MENU = [
    ("WiFi", "bitmap_icon_wifi"), ("NRF24", "bitmap_icon_jammer"),
    ("Detect", "bitmap_icon_eye"), ("GPS", "bitmap_icon_satellite"),
    ("Bluetooth", "bitmap_icon_spoofer"), ("SubGHz", "bitmap_icon_analyzer"),
    ("RFID/NFC", "bitmap_icon_rfid_chip"), ("System", "bitmap_icon_setting"),
]


def render_menu(t, selected=0):
    """displayMenu() in ESP32-DIV.ino -- a tile grid, so the tall bar."""
    t.fill_screen(UI_BG)
    for i, (label, icon) in enumerate(MENU):
        col, row = i // 4, i % 4
        x = X_OFFSET_LEFT if col == 0 else X_OFFSET_RIGHT
        y = Y_START + row * Y_SPACING
        sel = (i == selected)
        fill = UI_ICON if sel else UI_FG
        edge = UI_ICON if sel else UI_LINE
        ink = UI_BG if sel else UI_TEXT
        t.fill_round_rect(x, y, TILE_W, TILE_H, 5, fill)
        t.draw_round_rect(x, y, TILE_W, TILE_H, 5, edge)
        t.draw_bitmap_scaled(x + (TILE_W - TILE_ICON) // 2, y + TILE_ICON_DY,
                             icon, 16, 16, ink, TILE_ICON // 16)
        tw = t.text_width(label)
        t.print_f2(x + (TILE_W - tw) // 2, y + TILE_TEXT_DY, label, ink, fill)
    status_bar(t, STATUS_TALL)




def ino_table(name):
    """A menu's strings or icon names, read out of the sketch.

    BT_PAGE0 used to be a list in this file with eight entries while the
    sketch's page had six. A screenshot is documentation and this is the file
    that exists so documentation cannot drift from the source, so it does not
    get to keep its own copy of the source.
    """
    src = io.open(os.path.join(REPO, "ESP32-DIV", "ESP32-DIV.ino"),
                  encoding="utf-8", errors="replace").read()
    m = re.search(r"\*\s*%s\s*\[[^\]]*\]\s*=\s*\{(.*?)\};"
                  % re.escape(name), src, re.S)
    if not m:
        raise SystemExit("render_screens: no table %s in the sketch" % name)
    body = re.sub(r"//[^\n]*", "", m.group(1))
    if '"' in body:
        return re.findall(r'"([^"]*)"', body)
    return [x.strip() for x in body.split(",") if x.strip()]


def grid_wrap(t, label):
    """Greedy word wrap into at most GRID_LINES lines, measured.

    gridWrap() in the sketch, same rule and the same font. Font 2 is
    proportional, so the sketch measures with textWidth() and so does this; a
    character count would break somewhere else and the screenshot would stop
    being a picture of the panel.

    Greedy rather than the balanced two-way split this replaced, because
    balanced does not generalise to three lines and 'Probe Request Flood'
    needs three."""
    out, cur = [], ""
    for w in label.split():
        trial = w if not cur else cur + " " + w
        if t.text_width(trial) <= GRID_TEXT_W or not cur:
            cur = trial
        else:
            out.append(cur)
            cur = w
    if cur:
        out.append(cur)
    return out[:GRID_LINES]


def render_grid(t, title, items, icons, selected):
    """drawMenuGrid() plus drawSubmenuFooter()."""
    t.fill_screen(UI_BG)
    status_bar(t, STATUS_TALL)
    t.print_f1(8, 38, title, UI_ICON, UI_BG)
    n = "%d features" % len(items)
    t.print_f1(W - 8 - len(n) * 6, 38, n, rgb(0x8410), UI_BG)
    t.draw_fast_hline(0, 50, W, UI_LINE)

    for i, (label, icon) in enumerate(zip(items, icons)):
        if i >= GRID_SLOTS:
            break
        col, row = i % GRID_COLS, i // GRID_COLS
        x = GRID_GAP_X + col * (GRID_TILE_W + GRID_GAP_X)
        y = GRID_Y0 + row * (GRID_TILE_H + GRID_GAP_Y)
        sel = (i == selected)
        fill = UI_ICON if sel else UI_FG
        edge = UI_ICON if sel else UI_LINE
        ink = UI_BG if sel else UI_TEXT
        icol = UI_BG if sel else UI_ICON
        t.fill_round_rect(x, y, GRID_TILE_W, GRID_TILE_H, 5, fill)
        t.draw_round_rect(x, y, GRID_TILE_W, GRID_TILE_H, 5, edge)
        try:
            t.draw_bitmap_scaled(x + (GRID_TILE_W - GRID_ICON) // 2,
                                 y + GRID_ICON_DY, icon, 16, 16, icol,
                                 GRID_ICON // 16)
        except Exception:
            pass
        ls = grid_wrap(t, label)
        ty = y + GRID_TEXT_DY + ((GRID_LINES - len(ls)) * GRID_LINE_H) // 2
        for k, line in enumerate(ls):
            t.print_f2(x + (GRID_TILE_W - t.text_width(line)) // 2,
                       ty + k * GRID_LINE_H, line, ink, fill)

    fy = H - GRID_FOOT_H
    t.fill_rect(0, fy, W, GRID_FOOT_H, UI_BG)
    t.draw_fast_hline(0, fy, W, UI_LINE)
    iy = fy + (GRID_FOOT_H - 16) // 2
    try:
        t.draw_bitmap(10, iy, "bitmap_icon_go_back", 16, 16, UI_TEXT)
    except Exception:
        pass
    t.print_f2(30, iy, "Main Menu", UI_TEXT, UI_BG)




def render_wifi(t, selected=11):
    """The WiFi submenu: twelve features, one screen, no page button.

    The one worth rendering. It holds the two longest labels in the firmware,
    "Hidden SSID Revealer" at 134 px and "Probe Request Flood" at 131, in a
    96 px tile, so if the wrapping is wrong anywhere it is wrong here.
    """
    items = ino_table("wifi_page0_items") + ino_table("wifi_page1_items")
    icons = ino_table("wifi_page0_icons") + ino_table("wifi_page1_icons")
    render_grid(t, "WiFi", items, icons, selected)


def render_bluetooth(t, selected=3):
    """The Bluetooth submenu, which is one grid now rather than two pages.

    Eleven features, fifteen slots, so Next Page is gone. The rows it had
    were 30 px, which is 4.6 mm on this panel against the 7 mm a fingertip
    wants, and they were the tap targets.
    """
    items = ino_table("bluetooth_page0_items") + ino_table("bluetooth_page1_items")
    icons = ino_table("bluetooth_page0_icons") + ino_table("bluetooth_page1_icons")
    render_grid(t, "Bluetooth", items, icons, selected)


# Spotter rows, in the shape drawList() prints them. Each is what the
# detector would hold after hearing the device described in the comment.
#
# conf drives the colour of the first line and nothing else: Strong red,
# Likely orange, Weak dark grey. "**" in the right margin is corroborated --
# two different signatures matched the same address, which is the only way a
# Likely is promoted to Strong.
SPOTTER_HITS = [
    # Flock's own IEEE block, heard on WiFi. Bolted to a pole: 41 minutes in
    # range and 312 probe requests, which is what separates it from a phone.
    dict(kind="ALPR", label="Flock Safety", conf="Strong",
         mac="B4:1E:52:0C:7A:31", via="WiFi", rssi=-58, hits=312,
         fp=0x9E41C7A2, rnd=False, rot=0, age="41m", corrob=False),

    # A Liteon OUI, which alone is worth nothing -- but the same address also
    # beaconed "Flock-2291", and two independent fields agreeing is the whole
    # point of the scoring. Promoted, and marked.
    dict(kind="ALPR", label="Flock SSID", conf="Strong",
         mac="00:F4:8D:11:B2:60", via="WiFi", rssi=-71, hits=96,
         fp=0x2D7F0B54, rnd=False, rot=0, age="39m", corrob=True),

    # 0.3.1: Axon's own block on a public BLE address. No fingerprint,
    # because that is built out of WiFi information elements.
    dict(kind="BODYCAM", label="Axon Enterprise", conf="Strong",
         mac="00:25:DF:4A:19:E2", via="BLE", rssi=-49, hits=18,
         fp=0, rnd=False, rot=0, age="2m", corrob=False),

    # 0.3.3: a KARR module, matched on its advertised name -- "QT " plus
    # exactly eight characters. Likely, not Strong: the name says a module is
    # there, and nothing in the advertisement says whether it still carries
    # the shared key. Also the only orange row here, which is the point of
    # including it -- the grading has three levels and the render showed two.
    dict(kind="VEHICLE", label="KARR BT module", conf="Likely",
         mac="D8:3A:DD:41:0C:96", via="BLE", rssi=-63, hits=27,
         fp=0, rnd=False, rot=0, age="6m", corrob=False),

    # What the same table looks like when it is guessing. A contract
    # manufacturer's block, seen three times in eight seconds, walking past.
    dict(kind="ALPR", label="Liteon (ALPR?)", conf="Weak",
         mac="14:5A:FC:83:D1:07", via="WiFi", rssi=-77, hits=3,
         fp=0x33B1006E, rnd=False, rot=0, age="8s", corrob=False),

    # A randomised address. "rnd" says the OUI on the line above is made up,
    # so the Weak grading is being generous.
    dict(kind="ALPR", label="LAA, not a vendor", conf="Weak",
         mac="82:6B:F2:5E:40:98", via="WiFi", rssi=-69, hits=11,
         fp=0x7C22A1D4, rnd=True, rot=2, age="4m", corrob=False),
]

CONF_COLOUR = {"Strong": RED, "Likely": UI_ICON, "Weak": DARKGREY}


def render_spotter(t):
    """drawHeader() and drawList() in Spotter.cpp.

    Rendered without the touch nav bar, so contentBottom() is the panel
    height and the list gets (H-42)/30 rows -- 14 on the 3.5", 9 on the 2.8".
    With touch buttons enabled the feature reserves the bottom strip for
    Exit/Down/Up/Log and the count drops.
    """
    t.fill_screen(BLACK)
    status_bar(t)

    # drawHeader()
    t.fill_rect(0, 20, W, 18, BLACK)
    body(t, 8, 24, "ch  6  frames 18244  hits %d" % len(SPOTTER_HITS),
         WHITE, BLACK)
    # right-aligned, as drawHeader() does it
    tag = "REC 41"
    tw = t.text_width(tag) if BODY_FONT == 2 else 6 * len(tag)
    body(t, W - 4 - tw, 24, tag, RED, BLACK)         # s_logging, rows written

    # drawList()
    top, row_h = 42, BODY_ROW
    t.fill_rect(0, top, W, H - top, BLACK)
    for i, h in enumerate(SPOTTER_HITS):
        y = top + i * row_h

        body(t, 8, y, "%-9s %s" % (h["kind"], h["label"]),
             CONF_COLOUR[h["conf"]], BLACK)

        body(t, 8, y + BODY_LINE, "%s %s %ddBm x%u"
             % (h["mac"], h["via"], h["rssi"], h["hits"]),
             LIGHTGREY, BLACK)

        third = ""
        if h["fp"]:
            third += "fp %08X " % h["fp"]
        if h["rnd"]:
            third += "rnd "
        if h["rot"]:
            third += "+%d " % h["rot"]
        third += h["age"]
        body(t, 8, y + BODY_LINE3, third, DARKGREY, BLACK)

        if h["corrob"]:
            body(t, W - 32, y, "**", RED, BLACK)


# ── Hunt ───────────────────────────────────────────────────────────────────
#
# Replays TrackerHunt.cpp's drawPicker() and drawGauge(). Rendered without
# the touch nav bar, so contentBottom() is the panel height -- on the board
# the bar takes the bottom strip and the picker shows two fewer rows.
#
# The addresses are locally-administered (the 0x02 bit set) and invented.
# Real ones would be somebody's tracker, and a Find My address is rotating
# anyway, so a screenshot of one says nothing true for longer than an hour.

HUNT_TARGETS = [
    {"label": "Find My",  "mac": "4E:11:A0:3C:97:22", "rssi": -52, "age": 0},
    {"label": "Tile",     "mac": "E2:0C:7B:44:19:83", "rssi": -67, "age": 2},
    {"label": "Find My",  "mac": "56:9D:2F:08:B1:6E", "rssi": -74, "age": 1},
    {"label": "Samsung (SmartTag?)", "mac": "7A:31:C4:5D:02:AF",
     "rssi": -81, "age": 6},
    {"label": "Eddystone beacon", "mac": "62:88:EE:13:40:D7",
     "rssi": -89, "age": 14},
]

HUNT_ROW_H = 40   # set_panel() is the authority; see it for the rest
HUNT_SEL_BG = rgb(0x2124)


def render_hunt_pick(t):
    """drawPicker() in TrackerHunt.cpp."""
    t.fill_screen(BLACK)
    status_bar(t)

    top = 22
    bottom = H
    rows = (bottom - top - 18) // HUNT_ROW_H
    sel_index = 0

    t.fill_rect(0, top, W, bottom - top, BLACK)
    body(t, 8, top + 2, "trackers in range: %d" % len(HUNT_TARGETS),
               WHITE, BLACK)

    y = top + 18
    for i, d in enumerate(HUNT_TARGETS[:rows]):
        sel = i == sel_index
        bg = HUNT_SEL_BG if sel else BLACK
        if sel:
            t.fill_rect(0, y, W, HUNT_ROW_H, HUNT_SEL_BG)
        body(t, 8, y + 2, d["label"], UI_ICON if sel else WHITE, bg)
        body(t, 8, y + HUNT_ROW_LINE2, d["mac"], UI_ICON if sel else DARKGREY, bg)
        body(t, W - 96, y + 6, "%4d dBm  %2ds" % (d["rssi"], d["age"]),
                   UI_ICON if sel else WHITE, bg)
        y += HUNT_ROW_H


# The gauge's own constants, from TrackerHunt.cpp.
HUNT_RSSI_FAR = -100
HUNT_RSSI_NEAR = -35


def _dial():
    top, bottom = 30, H
    by_w = W // 2 - 10
    by_h = bottom - top - 90
    r = by_w if by_w < by_h else by_h
    return W // 2, top + r, r


def _angle_for(rssi):
    rssi = max(HUNT_RSSI_FAR, min(HUNT_RSSI_NEAR, rssi))
    frac = (rssi - HUNT_RSSI_FAR) / float(HUNT_RSSI_NEAR - HUNT_RSSI_FAR)
    return int(180.0 - frac * 180.0 + 0.5)


def _polar(cx, cy, deg, radius):
    a = math.radians(deg)
    return (cx + int(math.cos(a) * radius + 0.5),
            cy - int(math.sin(a) * radius + 0.5))


def render_hunt_gauge(t):
    """drawGauge() in TrackerHunt.cpp, locked on the strongest row."""
    lock = HUNT_TARGETS[0]
    smooth = -47          # a few seconds of walking toward it
    peak = -45
    last = -49
    seen = 143
    trend = 1             # WARMER

    cx, cy, r = _dial()
    t.fill_screen(BLACK)
    status_bar(t)

    # drawGaugeChrome()
    t.fill_rect(0, 22, W, H - 22, BLACK)
    t.print_f1(8, 24, lock["label"], UI_ICON, BLACK)
    t.print_f1(W - 104, 24, lock["mac"], DARKGREY, BLACK)

    for deg in range(0, 181, 2):
        x, y = _polar(cx, cy, deg, r)
        col = RED if deg <= 40 else (UI_ICON if deg <= 80 else DARKGREY)
        t.draw_pixel(x, y, col)
    for deg in range(0, 181, 30):
        x0, y0 = _polar(cx, cy, deg, r)
        x1, y1 = _polar(cx, cy, deg, r - 8)
        t.draw_line(x0, y0, x1, y1, DARKGREY)
    t.print_f1(cx - r, cy + 2, "far", DARKGREY, BLACK)
    t.print_f1(cx + r - 22, cy + 2, "near", DARKGREY, BLACK)
    t.fill_circle(cx, cy, 3, DARKGREY)

    # peak marker, then the needle
    pdeg = _angle_for(peak)
    px0, py0 = _polar(cx, cy, pdeg, r)
    px1, py1 = _polar(cx, cy, pdeg, r - 12)
    t.draw_line(px0, py0, px1, py1, GREEN)

    deg = _angle_for(smooth)
    tx, ty_ = _polar(cx, cy, deg, r - 10)
    bx0, by0 = _polar(cx, cy, (deg + 90) % 360, 5)
    bx1, by1 = _polar(cx, cy, (deg + 270) % 360, 5)
    t.fill_triangle(tx, ty_, bx0, by0, bx1, by1, RED)

    # the readout
    ty = cy + 14
    t.fill_rect(0, ty, W, H - ty, BLACK)
    word, wcol = ("WARMER", GREEN) if trend > 0 else (
        ("COLDER", BLUE) if trend < 0 else ("HOLD", DARKGREY))
    t.centre_f1(word, cx, ty, wcol, BLACK, 3)

    sm = smooth
    if sm < -85:
        band, bcol = "FAR", DARKGREY
    elif sm < -70:
        band, bcol = "CLOSER", WHITE
    elif sm < -55:
        band, bcol = "NEAR", UI_ICON
    elif sm < -45:
        band, bcol = "VERY CLOSE", UI_ICON
    else:
        band, bcol = "ARM'S LENGTH", RED
    t.centre_f1(band, cx, ty + 30, bcol, BLACK, 2)

    bw, bx, by = W - 40, 20, ty + 54
    fill = int((sm - HUNT_RSSI_FAR) / float(HUNT_RSSI_NEAR - HUNT_RSSI_FAR) * bw)
    fill = max(0, min(bw, fill))
    t.draw_rect(bx, by, bw, 10, DARKGREY)
    t.fill_rect(bx + 1, by + 1, fill, 8, bcol)
    ppx = bx + int((peak - HUNT_RSSI_FAR) /
                   float(HUNT_RSSI_NEAR - HUNT_RSSI_FAR) * bw)
    t.draw_fast_vline(max(bx, min(bx + bw, ppx)), by - 3, 16, GREEN)

    t.centre_f1("%d dBm    best %d" % (sm, peak),
                cx, by + 18, DARKGREY, BLACK, BODY_SIZE)
    t.centre_f1("%d seen" % seen,
                cx, by + 18 + 12 * BODY_SIZE, DARKGREY, BLACK, BODY_SIZE)


# ── Fast Pair ──────────────────────────────────────────────────────────────
#
# Replays FastPairScan.cpp's drawHeader() and drawList(). Three lines per
# device at kRowH = 30, the same shape as Spotter's list.
#
# Colour is the frame type and nothing else: green is a device advertising a
# Model ID, which is what pairing mode looks like; cyan is an account-key
# frame, meaning it already belongs to somebody; grey is an empty filter.
#
# Addresses and model IDs are invented. Every Model ID row shows the raw
# hex, because that is what the device does: kModels in FastPair.cpp is a
# sentinel and nothing else, on the grounds that a name needs a source
# rather than a spam list, so modelName() returns nullptr for everything.
# A render showing "Pixel Buds Pro" would be inventing a capability.

FASTPAIR_DEVS = [
    {"line1": "PAIRING  model 0E30A0", "col": GREEN,
     "mac": "F0:9E:4A:22:8B:01", "addr": "pub", "rssi": -44,
     "line3": "batt 90/85/60+ x37 22s", "sel": True},
    {"line1": "paired   filter 6 bytes", "col": CYAN,
     "mac": "5C:3A:11:9D:74:E2", "addr": "rnd", "rssi": -61,
     "line3": "x214 4m", "sel": False},
    {"line1": "PAIRING  model 92BBBD", "col": GREEN,
     "mac": "A4:C1:38:0B:66:1F", "addr": "pub", "rssi": -72,
     "line3": "x12 9s", "sel": False},
    {"line1": "paired   no account keys", "col": LIGHTGREY,
     "mac": "6E:82:D5:40:AA:3C", "addr": "rnd", "rssi": -79,
     "line3": "x88 11m", "sel": False},
    {"line1": "paired   filter 10 bytes", "col": CYAN,
     "mac": "72:0D:9C:57:31:B8", "addr": "rnd", "rssi": -86,
     "line3": "x5 1h", "sel": False},
]

FASTPAIR_SEL_BG = rgb(0x18E3)


def render_fastpair(t):
    """drawHeader() and drawList() in FastPairScan.cpp."""
    t.fill_screen(BLACK)
    status_bar(t)

    t.fill_rect(0, 20, W, 18, BLACK)
    body(t, 8, 24, "devices %d   adverts 1962" % len(FASTPAIR_DEVS),
               WHITE, BLACK)

    top, row_h = 42, BODY_ROW
    rows = (H - top) // row_h
    t.fill_rect(0, top, W, H - top, BLACK)

    for i, d in enumerate(FASTPAIR_DEVS[:rows]):
        y = top + i * row_h
        bg = FASTPAIR_SEL_BG if d["sel"] else BLACK
        if d["sel"]:
            t.fill_rect(0, y - 2, W, row_h - 2, FASTPAIR_SEL_BG)
        body(t, 8, y, d["line1"], d["col"], bg)
        body(t, 8, y + BODY_LINE, "%s %s %ddBm"
                   % (d["mac"], d["addr"], d["rssi"]), LIGHTGREY, bg)
        body(t, 8, y + BODY_LINE3, d["line3"], DARKGREY, bg)



# ── Freq Analyser ────────────────────────────────────────────────────────
#
# FreqScan in subghz.cpp. The twelve entries of subghz_frequency_list, one
# row each, a bar scaled between kRssiFloor and kRssiCeil, and the held peak
# marked with a bright tick at its own width.
#
# The readings are a plausible sample, not a capture: a fob keyed at 433.92
# with the band otherwise near the floor, because that is the case the
# screen exists for and the one worth being able to recognise.

# subghz_frequency_list, all eighteen of it and in its order. An earlier
# version of this listed twelve, five of which are not in the array, which
# is what comes of transcribing a wrapped slice of a C file.
FREQSCAN_HZ = [
    300000000, 303875000, 304250000, 310000000, 314000000, 315000000,
    318000000, 390000000, 418000000, 433075000, 433420000, 433920000,
    434420000, 434775000, 438900000, 868350000, 915000000, 925000000,
]
# A fob keyed at 433.92 with the band otherwise near the floor, which is the
# case the screen exists for and the one worth recognising at a glance.
FREQSCAN_PEAK = [
    -108, -110, -106, -109, -110, -104,
    -110, -107, -110, -96, -88, -41,
    -73, -109, -110, -102, -110, -108,
]
FREQSCAN_FLOOR = -110
FREQSCAN_CEIL = -20
# rowHeight() in subghz.cpp: derived from the space and the number of rows,
# clamped. Written down here it would drift from the screen it depicts.
FREQSCAN_ROW_MIN = 13
FREQSCAN_ROW_MAX = 24


def render_freqscan(t):
    """FreqScan::draw() in subghz.cpp."""
    t.fill_screen(BLACK)
    status_bar(t)

    top = 30
    # Drawn without the touch nav bar, as every screen here is, so the
    # content runs to the panel edge and contentBottom() is the height.
    bottom = H
    label_x = 6
    bar_x = 62
    dbm_x = W - 40
    full_w = dbm_x - bar_x - 8

    body(t, 6, top - 12, "Freq Analyser", UI_TEXT, BLACK)
    body(t, 150, top - 12, "peak hold, dBm", UI_LABLE, BLACK)

    avail = bottom - top - 14
    row_h = max(FREQSCAN_ROW_MIN,
                min(FREQSCAN_ROW_MAX, avail // len(FREQSCAN_HZ)))

    strongest = max(range(len(FREQSCAN_PEAK)), key=lambda i: FREQSCAN_PEAK[i])

    last_y = top
    for i, hz in enumerate(FREQSCAN_HZ):
        y = top + i * row_h
        if y + row_h > bottom:
            break
        last_y = y + row_h
        peak = FREQSCAN_PEAK[i]
        best = (i == strongest) and peak > FREQSCAN_FLOOR

        bar_y = y + (row_h - 8) // 2

        body(t, label_x, bar_y, "%7.2f" % (hz / 1e6),
             UI_ICON if best else UI_LABLE, BLACK)

        if peak <= FREQSCAN_FLOOR:
            w = 0
        elif peak >= FREQSCAN_CEIL:
            w = full_w
        else:
            w = ((peak - FREQSCAN_FLOOR) * full_w) // (FREQSCAN_CEIL - FREQSCAN_FLOOR)

        if w > 0:
            t.fill_rect(bar_x, bar_y, w, 8, UI_ICON if best else UI_LABLE)
            # UI_WARN resolves to UI_ICON here, which would make the tick
            # the same colour as the bar it marks.
            t.fill_rect(bar_x + w - 1, bar_y - 1, 1, 10, RED)

        body(t, dbm_x, bar_y, "%4d" % peak, UI_LABLE, BLACK)

    body(t, 6, last_y + 3,
         "strongest %.2f MHz" % (FREQSCAN_HZ[strongest] / 1e6),
         UI_LABLE, BLACK)




# ── NRF24 scanner waterfall ──────────────────────────────────────────────
#
# Scanner::scannerUpdateFall in bluetooth.cpp. Time across, channel down,
# one column per sweep, no history kept on the device.

FALL_CHANS = 128


def _fall_colour(v, max_v):
    """scannerFallColour: black, blue, green, red."""
    if v <= 0:
        return (0, 0, 0)
    top = max_v if max_v else 1
    q = int(v) * 255 // top
    if q < 64:
        return (0, 0, 64 + q * 2)
    if q < 128:
        return (0, (q - 64) * 4, 255 - (q - 64) * 2)
    if q < 192:
        return ((q - 128) * 4, 255, 0)
    return (255, max(0, 255 - (q - 192) * 4), 0)


def _fall_traffic(col, ch):
    """Synthetic but shaped: a WiFi block, BLE advertising, and a hopper."""
    v = 0
    # WiFi channel 6: 2437 MHz, about 20 MHz wide, always there.
    if 27 <= ch <= 47:
        edge = min(ch - 27, 47 - ch)
        v = max(v, 6 + edge)
    # BLE advertising: three fixed channels, bursty.
    if ch in (2, 26, 80) and (col * 7 + ch) % 5 < 2:
        v = max(v, 14)
    # A hopper walking one channel per sweep.
    if ch == (col * 3) % FALL_CHANS:
        v = max(v, 12)
    # Floor noise, so black is rare and the palette is doing work.
    if (col * 31 + ch * 17) % 23 == 0:
        v = max(v, 1)
    return v


def render_nrf_fall(t):
    """The scanner's waterfall view."""
    t.fill_screen(BLACK)
    status_bar(t)

    # Geometry mirrors scannerEnsurePlotLayout closely enough to judge it:
    # a log area at the top, then the plot with a one-pixel frame.
    graph_top = 150
    axis_x = 10
    plot_top = graph_top + 14
    plot_w = W - axis_x - 10
    plot_h = H - plot_top - 40

    body(t, 8, 26, "[+] Scanner ready", UI_ICON, BLACK)
    body(t, 8, 40, "Peak: Ch37  2.437GHz", UI_LABLE, BLACK)
    body(t, 8, 54, "Active: 31 channel(s)", UI_LABLE, BLACK)

    t.draw_rect(axis_x, plot_top, plot_w, plot_h, UI_LINE)
    body(t, (W - 100) // 2, graph_top + 2, "2.4 GHz Waterfall", UI_LABLE, BLACK)

    inner_h = plot_h - 2
    cols = plot_w - 2
    for c in range(cols):
        vals = [_fall_traffic(c, ch) for ch in range(FALL_CHANS)]
        mx = max(vals)
        for row in range(inner_h):
            ch = (row * FALL_CHANS) // inner_h
            t.im.putpixel((axis_x + 1 + c, plot_top + 1 + row),
                          _fall_colour(vals[ch], mx))

    label_y = plot_top + plot_h + 2
    body(t, axis_x + 2, label_y, "2.40", UI_LABLE, BLACK)
    body(t, axis_x + plot_w // 2 - 10, label_y, "time", UI_LABLE, BLACK)
    body(t, axis_x + plot_w - 24, label_y, "2.52", UI_LABLE, BLACK)


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--out", default=os.path.join(REPO, "render"))
    ap.add_argument("--scale", type=int, default=3)
    ap.add_argument("--panel", type=int, choices=(35,), default=35,
                    help="kept so old invocations still parse; 35 is the "
                         "only panel")
    args = ap.parse_args()
    set_panel(args.panel)

    for p in (ICON_H, os.path.join(FONTS, "glcdfont.c")):
        if not os.path.isfile(p):
            print("missing %s - run tools/build.sh setup first" % p,
                  file=sys.stderr)
            return 1

    bitmaps = load_bitmaps()
    glcd = load_glcd()
    fw, fg = load_font16()
    brand = load_branding()
    global BRAND
    BRAND = brand
    print("loaded %d bitmaps, %d glcd chars, %d font-2 glyphs"
          % (len(bitmaps), len(glcd), len(fg)))
    print("branding: %s %s, by %s"
          % ("Pueo", brand["PUEO_VERSION"], brand["PUEO_AUTHOR"]))
    print("panel: %.1f\" -- %dx%d, %dx%d tiles"
          % (args.panel / 10, W, H, TILE_W, TILE_H))

    os.makedirs(args.out, exist_ok=True)
    for name, fn in (("boot", lambda t: render_boot(t, brand)),
                     ("menu", render_menu),
                     ("wifi", render_wifi),
                     ("bluetooth", render_bluetooth),
                     ("spotter", render_spotter),
                     ("hunt-pick", render_hunt_pick),
                     ("hunt-gauge", render_hunt_gauge),
                     ("fastpair", render_fastpair),
                     ("freqscan", render_freqscan),
                     ("nrf-fall", render_nrf_fall),
                     ("hunt-mark",
                      lambda t: render_mark(t, "bitmap_pueo_hunt", "Hunt")),
                     ("spotter-mark",
                      lambda t: render_mark(t, "bitmap_pueo_spotter", "Surveillance")),
                     ("dwell-tracker", lambda t: render_dwell(t, True)),
                     ("dwell-other", lambda t: render_dwell(t, False)),
                     ("beacon-splash",
                      lambda t: render_beacon_splash(t, brand)),
                     ("beacon-running",
                      lambda t: render_beacon_running(t, brand)),
                     ("drone-alert", lambda t: render_drone_alert(t, True)),
                     ("drone-alert-partial",
                      lambda t: render_drone_alert(t, False)),
                     ("drone-mark",
                      lambda t: render_mark(t, "bitmap_pueo_drone",
                                            "Drone Detector"))):
        t = Tft(glcd, fw, fg, bitmaps)
        fn(t)
        p1 = os.path.join(args.out, "pueo-screen-%s.png" % name)
        t.im.save(p1)
        big = t.im.resize((W * args.scale, H * args.scale), Image.NEAREST)
        pN = os.path.join(args.out, "pueo-screen-%s@%dx.png" % (name, args.scale))
        big.save(pN)
        print("  %-10s %s  and  %s" % (name, os.path.basename(p1),
                                       os.path.basename(pN)))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
