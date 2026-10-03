#!/usr/bin/env python3
"""Left-aligned text that runs off the right edge.

check_text_fits.py measures the centred lines, the ones drawn through
showLine(). This measures the other kind: tft.drawString(text, x, y), which
starts at a fixed x and runs right until it stops. The failure is quieter
than the centred one. Centred text loses both ends and looks obviously
wrong; left-aligned text loses only the tail, so it reads as a sentence that
has been cut, and if the tail carries the information then what is on screen
is not wrong-looking, it is just missing the answer.

That is how File Transfer shipped a URL reading "http://192.168". The panel
is 320 px wide, the value column was at x=150, and eighteen characters at
twelve pixels each wanted 366.

Writing this found three more that had already shipped: AP Tracker's picker
lost the channel off every row, Hunt's lost the age, and AP Tracker's
empty-list message lost the end of both its lines. All four were in features
somebody had used.

The arithmetic is pixels, per font. Font 1 advances 6 px per character
whatever the glyph, so there a width is 6 * size * length. Fonts 2 and 4 are
proportional, 3 to 10 px and 1 to 25 px respectively, so a character count
is not a width at all and tft_fonts.py reads the tables out of the TFT_eSPI
zip that ships in the release archive. A format is charged its literal runs
measured plus each conversion at the widest character that conversion can
produce, which is neither the font's widest glyph, 25 px in font 4 and
enough to fail every line on the device, nor the width of a digit, since
font 4's uppercase hex reaches 18 px against a digit's 14 and charging a
digit for %X under-measures a MAC by 48 px.

This measured font 1 only until 0.4.29, and the gap was not the obvious half.
setTextFont's argument was resolved with int() behind a digit regex, so
setTextFont(PUEO_BODY_FONT) gave None and the caller then assumed font 1:
nineteen call sites that draw in font 2 were measured at 6 px per character,
about a quarter narrow, in the direction that passes a line which overruns.
Nothing overran, which is luck rather than a result. The font numbers now
come from shared.h so renumbering the constant cannot leave this agreeing
with an old value.

    python tools/check_text_margins.py

Reads source; needs no board.

What it takes to measure one call
---------------------------------
drawString takes the font, size and datum from earlier calls rather than
arguments, and the text is usually neither a literal nor a local. So four
things have to be settled, and each one was a hole that let a real bug
through before it was closed:

  the size      Set in the function that lays a screen out, used in the
                helper that draws a row. Skipping calls whose own function
                set no size measured 14 of 188 and reported green over the
                bug this exists for. A function that sets none inherits from
                its callers when they agree; if they do not, it is skipped.

                Except for a proportional font, where no size set means 1.
                That is TFT_eSPI's default and setTextSize is only called to
                depart from it, and of the call sites where both font and
                size resolve, font 2 is size 1 at all 29 of them. Font 1
                appears at 1, 2 and 3, so the same assumption there would be
                a guess: PUEO_BODY_SIZE is 2 and multiplies font 1, never
                font 2.

  the font      Explicit literal, a #define read from shared.h, or an
                argument on the call itself. Fonts 6, 7 and 8 are skipped:
                they are the gauge's digit-and-colon faces, no table is
                loaded for them, and a guess would be an invented width.

  the x         Written as an offset from the right edge, PUEO_SCREEN_W - N,
                through a constexpr. Requiring a literal skipped exactly the
                four lines that had just been moved there.

  the text      Three shapes. A literal, or an expression over literals and
                char pointers -- a ternary between two messages is both of
                them, so the longer one is the answer. A buffer snprintf'd in
                the same function, taking the last write before the call and
                not the widest write in the file, because DroneScan fills one
                char line[64] with every field of a row in turn. Or a
                parameter, resolved from the call sites one level up, since
                the whole point of drawRow(y, label, value) is that the value
                comes from somewhere else.

  the datum     Only a datum positively set to something not left-aligned is
                skipped, because that is evidence. Absence is not.

Everything unresolved is skipped and counted, and the counts print on every
run, because a check that measures nine things out of two hundred while
printing "ok" is worse than no check.

What it cannot see
------------------
A %s of a runtime string. The File Transfer URL is one:
snprintf(s_url, ..., "http://%s", ip), and the length of an IP address is not
in the source. That line is safe because it was given a full-width line of
its own instead of a column with fourteen characters in it, which is a layout
decision rather than a measurement. What this catches is the other half of
the same bug: the literals laid out beside it, and any column whose position
leaves a bounded value too little room.
"""
import ast
import operator
import re
import sys
from pathlib import Path

import tft_fonts
from check_text_fits import (FONT1_ADVANCE, fmt_pixels, fmt_width, literal,
                             snprintf_fmt)
from check_text_pitch import calls, functions

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

# One panel, because there is one panel. check_text_fits.py still carries a
# 2.8" entry from when this tree built two, and it is harmless there because
# nothing it measures fails on it. Here three of the first eleven failures
# were 2.8"-only: three pieces of work on a screen nobody owns, standing
# between this check and being green. The 2.8" was dropped on 2026-09-26.
PANELS = [('3.5"', 320, 2)]

# A datum whose x is the left edge. Anything else means x is a centre or a
# right edge, which is check_text_fits.py's question, not this one.
LEFT_DATUM = ("TL_DATUM", "ML_DATUM", "BL_DATUM")

NUM = re.compile(r"^-?\d+$")


def font_macros():
    """Font numbers reachable through a #define, from shared.h.

    setTextFont(PUEO_BODY_FONT) used to resolve to None, and the caller then
    assumed font 1 and measured at 6 px per character. PUEO_BODY_FONT is 2:
    proportional, and measured that way nineteen call sites came out about a
    quarter narrow, which is the direction that passes an overrun.

    Read rather than written down, so renumbering the constant cannot leave
    this agreeing with an old value.
    """
    src = (SKETCH / "shared.h").read_text(encoding="utf-8", errors="replace")
    out = {}
    for m in re.finditer(r"^#define\s+(\w*FONT\w*)\s+(\d+)\s*$",
                         src, re.M):
        out[m.group(1)] = int(m.group(2))
    return out


FONT_MACROS = font_macros()

# The right-hand side is an expression, not a literal: a column placed as an
# offset from the right edge is written PUEO_SCREEN_W - 104.
CONST = re.compile(r"^(?:static\s+)?constexpr\s+(?:const\s+)?int\s+(\w+)\s*="
                   r"\s*([^;]+);", re.M)
DEFINE = re.compile(r"^#define\s+(\w+)\s+([-+*/() \d]+)\s*$", re.M)

# A char* given a string literal, at its definition or anywhere it is
# assigned. Several assignments to one pointer are alternative messages for
# one slot on screen, so the longest of them is the width to check.
STR_ASSIGN = re.compile(r"\b(\w+)\s*=\s*((?:\s*\"(?:[^\"\\]|\\.)*\")+)\s*;")

STR_LITERAL = re.compile(r'"((?:[^"\\]|\\.)*)"')


# Integer arithmetic over a parsed tree, not eval(). The input is this repo's
# own source so there is nothing hostile in it, but a character class loose
# enough to allow "*" also allows "**", and 9**9**9 would hang the check
# rather than fail it. Four operators and a unary minus is the whole of what
# an x coordinate here is ever written as.
ARITH = {ast.Add: operator.add, ast.Sub: operator.sub,
         ast.Mult: operator.mul, ast.FloorDiv: operator.floordiv,
         ast.Div: operator.floordiv}


def arith(node):
    if isinstance(node, ast.Expression):
        return arith(node.body)
    if isinstance(node, ast.Constant) and isinstance(node.value, int):
        return node.value
    if isinstance(node, ast.UnaryOp) and isinstance(node.op, ast.USub):
        return -arith(node.operand)
    if isinstance(node, ast.BinOp) and type(node.op) in ARITH:
        return ARITH[type(node.op)](arith(node.left), arith(node.right))
    raise ValueError(ast.dump(node))


def resolve_x(expr, consts, width):
    """x in pixels, or None when it is not a number this can work out."""
    e = expr.strip()
    e = re.sub(r"\bPUEO_SCREEN_W\b", str(width), e)
    e = re.sub(r"\btft\.width\(\)", str(width), e)
    for name, val in consts.items():
        e = re.sub(r"\b%s\b" % re.escape(name), str(val), e)
    try:
        return arith(ast.parse(e, mode="eval"))
    except (SyntaxError, ValueError, ZeroDivisionError, TypeError):
        return None


def consts_in(src, width):
    """{name: value} for the int constants this file defines, resolved.

    Definitions refer to each other and to PUEO_SCREEN_W, so this runs to a
    fixed point: a name that cannot be worked out yet may become resolvable
    once another one is.
    """
    raw = {}
    for m in CONST.finditer(src):
        raw[m.group(1)] = m.group(2).strip()
    for m in DEFINE.finditer(src):
        raw.setdefault(m.group(1), m.group(2).strip())

    out = {}
    for _ in range(len(raw) + 1):
        progress = False
        for name, expr in raw.items():
            if name in out:
                continue
            val = resolve_x(expr, out, width)
            if val is not None:
                out[name] = val
                progress = True
        if not progress:
            break
    return out


def str_consts(src):
    """{name: longest literal} for char pointers assigned string literals."""
    out = {}
    for m in STR_ASSIGN.finditer(src):
        lit = literal(m.group(2))
        if lit is None:
            continue
        if len(lit) > len(out.get(m.group(1), "")):
            out[m.group(1)] = lit
    return out


def expr_width(expr, strs):
    """Characters a text expression can be, from literals and known pointers.

    `s_fail != nullptr ? s_fail : "Nothing to serve"` is a ternary over two
    things that land in the same place on screen, so the answer is whichever
    is longer. Returns (n, how) or None.
    """
    best = None
    for m in STR_LITERAL.finditer(expr):
        cand = m.group(1)
        if best is None or len(cand) > len(best):
            best = cand
    for name in set(re.findall(r"\b([A-Za-z_]\w*)\b", expr)):
        cand = strs.get(name)
        if cand is not None and (best is None or len(cand) > len(best)):
            best = cand
    if best is None:
        return None
    return ("literal", best, '"%s"' % best)


def width_px(cand, font, size):
    """Pixels a candidate can draw at its widest, or None.

    The unit is pixels rather than characters because a count means nothing
    across fonts: font 1 advances 6 px per glyph, font 2 runs 3 to 10 and
    font 4 runs 1 to 25. tft_fonts reads the tables out of the TFT_eSPI zip
    that ships in the release archive.
    """
    kind, payload, _how = cand
    if kind == "literal":
        return tft_fonts.width(payload, font, size)
    if kind == "format":
        return fmt_pixels(payload, font, size)
    return None


def params_of(src, fname):
    """Parameter names of fname's definition, in order. None if not found."""
    m = re.search(r"\b%s\s*\(([^)]*)\)\s*\{" % re.escape(fname), src)
    if not m:
        return None
    out = []
    for part in m.group(1).split(","):
        part = part.strip()
        if not part or part == "void":
            continue
        ident = re.findall(r"(\w+)\s*$", part)
        out.append(ident[0] if ident else None)
    return out


def own_size(body, body_size):
    """The last size this function sets, or None if it sets none."""
    size = None
    for _, args in calls(body, "setTextSize"):
        a = args[0].strip() if args else ""
        if a == "PUEO_BODY_SIZE":
            size = body_size
        elif NUM.match(a):
            size = int(a)
        else:
            size = None
    return size


def state_before(body, off, body_size):
    """(size, font, datum_is_left) as the code has left them at `off`.

    Last call wins, which is what the hardware does: these are setters on one
    shared TFT_eSPI object. A setting the function never touches is unknown
    rather than assumed, because whatever ran before this screen left it
    however it liked.
    """
    head = body[:off]
    size = own_size(head, body_size)
    font = None
    datum = None
    for _, args in calls(head, "setTextFont"):
        a = args[0].strip() if args else ""
        if NUM.match(a):
            font = int(a)
        elif a in FONT_MACROS:
            font = FONT_MACROS[a]
        else:
            font = None
    for _, args in calls(head, "setTextDatum"):
        a = args[0].strip() if args else ""
        datum = a in LEFT_DATUM
    return size, font, datum


def inherited_sizes(funcs, body_size):
    """{name: size} for functions that set no size and whose callers agree.

    One level, no transitive walk. Both shapes this exists for are one call
    deep, and a deeper search would start inferring through the UI helpers in
    utils.cpp, which every screen calls and which set whatever they need.
    """
    mine = {name: own_size(body, body_size) for name, _, body in funcs}
    out = {}
    for name, _, body in funcs:
        if mine.get(name) is not None:
            continue
        seen = set()
        for caller, _, cbody in funcs:
            if caller == name:
                continue
            for off, _ in calls(cbody, name):
                s, _f, _d = state_before(cbody, off, body_size)
                if s is None:
                    s = mine.get(caller)
                seen.add(s)
        if len(seen) == 1:
            val = seen.pop()
            if val is not None:
                out[name] = val
    return out


def param_width(src, funcs, strs, fname, var, font, size):
    """Width of a parameter, from every call site one level up.

    drawRow(y, "Joined", buf, colour) is the whole point of a row helper: the
    text comes from the caller. Without this, every value drawn through one
    is invisible, which here is most of a screen.

    Returns (n, how, unknown_callers), the widest of the call sites that do
    resolve, or None when none of them do.

    Requiring every call site was the first version and it measured nothing:
    File Transfer's three rows go through one drawRow, and one of them passes
    a %s of humanSize(), which is unmeasurable. All-or-nothing let that one
    row hide the other two, including the "%d of %d" that is the widest thing
    in the column. So the widest resolvable wins and the unresolved ones are
    counted and printed.

    The cost is a real false negative: an unresolved caller could pass
    something wider than every resolved one and this would not see it. That
    is a miss rather than a wrong answer, and a miss is what the all-or-
    nothing version gave for the whole column.
    """
    params = params_of(src, fname)
    if not params or var not in params:
        return None
    idx = params.index(var)

    best = None
    unknown = 0
    for caller, _, cbody in funcs:
        if caller == fname:
            continue
        for off, args in calls(cbody, fname):
            if len(args) <= idx:
                unknown += 1
                continue
            arg = args[idx].strip()

            got = expr_width(arg, strs)
            if got is None:
                fmt = snprintf_fmt(cbody[:off], arg)
                if fmt is None:
                    unknown += 1
                    continue
                got = ("format", fmt, '"%s" at its widest' % fmt)
            # Widest in pixels, not longest in characters. In a proportional
            # font those are different questions and this is asking the one
            # that decides whether the line fits.
            px = width_px(got, font, size)
            if px is None:
                unknown += 1
                continue
            if best is None or px > best[0]:
                best = (px, got)
    if best is None:
        return None
    return best[0], best[1][2], unknown


def main():
    problems = []
    measured = 0
    skipped = {}
    assumed = set()

    def skip(why):
        skipped[why] = skipped.get(why, 0) + 1

    for path in sorted(SKETCH.glob("*.cpp")):
        src = path.read_text(encoding="utf-8", errors="replace")
        if "drawString(" not in src:
            continue
        funcs = functions(src)
        strs = str_consts(src)
        inherit = {p[0]: inherited_sizes(funcs, p[2]) for p in PANELS}
        consts = {p[0]: consts_in(src, p[1]) for p in PANELS}

        for fname, line0, body in funcs:
            for off, args in calls(body, "drawString"):
                if len(args) < 3:
                    continue
                text, xarg = args[0], args[1]
                line = line0 + body[:off].count("\n")

                for panel, width, body_size in PANELS:
                    size, font, datum = state_before(body, off, body_size)

                    # An explicit 4th argument is the font and overrides
                    # whatever setTextFont left behind.
                    if len(args) >= 4:
                        a = args[3].strip()
                        font = int(a) if NUM.match(a) else None

                    if font is None:
                        font = 1
                        assumed.add("font 1 where the function set none")
                    if not tft_fonts.known(font):
                        # 6, 7 and 8 are the gauge's digit-and-colon faces.
                        # No table is loaded for them and a guess would be
                        # an invented width.
                        skip("font %d, which has no width table" % font)
                        continue
                    if size is None:
                        size = inherit[panel].get(fname)
                        if size is not None:
                            assumed.add("the size this function's callers "
                                        "set, where it sets none")
                    if size is None and font != 1:
                        # A proportional font with no size set is size 1.
                        # Not a guess: it is TFT_eSPI's default, and of the
                        # call sites where both resolve, font 2 is size 1 at
                        # all 29 of them. Font 1 appears at 1, 2 and 3, which
                        # is why this does not apply to it: PUEO_BODY_SIZE is
                        # 2 and multiplies font 1, never font 2.
                        size = 1
                        assumed.add("size 1 for a proportional font whose "
                                    "size nothing sets, which is the "
                                    "library default")
                    if size is None:
                        skip("size set neither here nor by any caller")
                        continue
                    if datum is False:
                        skip("datum set to something not left-aligned")
                        continue

                    x = resolve_x(xarg, consts[panel], width)
                    if x is None:
                        skip("x does not resolve to a number")
                        continue

                    px = None
                    got = expr_width(text, strs)
                    if got is None:
                        var = text.strip()
                        fmt = snprintf_fmt(body[:off], var)
                        if fmt is not None:
                            got = ("format", fmt,
                                   '"%s" at its widest' % fmt)
                        else:
                            traced = param_width(src, funcs, strs, fname,
                                                 var, font, size)
                            if traced is None:
                                skip("text is a variable this cannot trace")
                                continue
                            px, how = traced[0], traced[1]
                            assumed.add("the widest thing any caller passes, "
                                        "for text that is a parameter")
                            if traced[2]:
                                assumed.add(
                                    "that no unresolved caller passes "
                                    "something wider (%d such call site(s))"
                                    % traced[2])
                    if px is None:
                        how = got[2]
                        px = width_px(got, font, size)
                        if px is None:
                            skip("a width this font cannot be asked for")
                            continue

                    measured += 1
                    end = x + px
                    if end > width:
                        problems.append(
                            ("%s:%s()" % (path.name, fname), line, panel,
                             x, end, width, how, font))

    print("left-aligned strings measured: %d" % measured)
    for why, n in sorted(skipped.items(), key=lambda kv: -kv[1]):
        print("  skipped %4d  %s" % (n, why))
    for a in sorted(assumed):
        print("  assuming      %s" % a)

    if measured == 0:
        print()
        print("FAIL: none measured. drawString, the function scanner or the")
        print("      state tracking stopped matching, and this is green over")
        print("      nothing.")
        return 1

    print()
    if not problems:
        print("every one of them ends before the right edge.")
        return 0

    for where, line, panel, x, end, width, how, font in problems:
        print("  %-34s line %-5d %s  x=%d ends at %d, panel is %d"
              % (where, line, panel, x, end, width))
        print("  %-34s %s" % ("", how))
    print()
    print("FAILED: %d string(s) run past the right edge." % len(problems))
    print()
    print("TFT_eSPI does not clip or wrap. The tail is simply not drawn, so")
    print("what is on screen is a sentence that stops. Shorten it, move it")
    print("left, or put it on its own line.")
    return 1


if __name__ == "__main__":
    sys.exit(main())
