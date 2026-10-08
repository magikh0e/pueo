#!/usr/bin/env python3
"""Every feature shows a way out, on the slot that actually is the way out.

featureExitButtonPressed() reads BTN_SELECT, which is the centre slot of the
touch nav bar. Almost every feature labels that slot "Exit". Three did not:
Spotter, Fast Pair and Hunt passed "" for the centre and put "Back" on the
left, where nothing reads it. On a touch-only board that is a feature you
can open and cannot leave -- the exit was there the whole time, on an
unlabelled button, next to a labelled one that did nothing.

That hid behind check_menu_dispatch.py's bug: those same three could not be
opened by touch at all, so nobody got far enough in to find they could not
get out.

Then the labels were right and still invisible. setTouchNavLabels() only
stores them; redrawTouchButtonBar() paints them. Spotter and Fast Pair did
call it -- and then called redraw(true), whose first act is fillScreen. The
bar was drawn and wiped inside one setup, which from the outside looks
exactly like never drawing it.

Four rules, one per way this has actually broken:

  1. the centre slot is never an empty string. nullptr is fine: it draws the
     default icon, which is still something to press. "" is a blank button.

  2. no slot promises a way out the feature does not route there -- "Back"
     on the left while the centre is blank is the same bug as rule 1, named
     separately so the report says why it matters.

  3. a file that sets labels also repaints the bar. File-level rather than
     per-call: several features set labels in a helper and repaint in the
     caller, and a line-window rule would fail those for no reason.

  5. a full-repaint helper puts the bar back. Rule 4 catches a repaint that
     is wiped afterwards; this catches the opposite, a wipe with no repaint
     at all. Two features had a redraw(bool full) that fillScreens and did
     not restore the bar, and both relied on every caller remembering.
     Spotter's dwell alert did not: the first time a device dwelled long
     enough to alert, the buttons vanished for the rest of the session, and
     it was reported from the board rather than caught here. Fast Pair had
     it too, reachable by backing out of a view. Asserting it on the helper
     rather than at each call site is what makes it stay true as callers are
     added.

  4. nothing clears the screen between the repaint and the end of its
     function. A clear is tft.fillScreen(), or a call to redraw() -- this
     tree's idiom for a full repaint, and the one that hid the bug, because
     the call site says redraw(true) and never says fillScreen.

  6. a labelled side slot has its pin wired to something. The slots are
     positional, (left, down, center, up, right), while the handler names
     its button, so a label can sit on one slot with the code reading
     another. Two screens were written that way, Reset SD and the Freq
     Analyser, both labelling "Reset"/"Rescan" on down with the handler on
     BTN_UP: one labelled dead button, one live blank one, and the other 342
     assertions here passed. Scoped to the enclosing named namespace,
     because utils.cpp reads BTN_UP in an unrelated function 1,000 lines
     away and a file-level test passes the bug it was written for.

Reads source. Does not need a board.
"""
import re
import sys
from pathlib import Path

SKETCH = Path(__file__).resolve().parent.parent / "ESP32-DIV"

SET_LABELS = re.compile(r"\bsetTouchNavLabels\s*\(")
REPAINT = re.compile(r"\bredrawTouchButtonBar\s*\(\s*\)")
CLEAR = re.compile(r"\btft\.fillScreen\s*\(|\bredraw\s*\(")

# utils.cpp and wifi.cpp each wrap the setter and forward their own
# parameters. Those are not label sites.
FORWARDER = re.compile(r"\bsetTouchNavLabels\(left, down, center, up, right\)")

LEAVE_WORDS = {"back", "exit", "quit", "leave"}


def split_args(text, open_paren):
    """The arguments of the call whose '(' is at open_paren, or None."""
    depth = 0
    for i in range(open_paren, len(text)):
        if text[i] in "([":
            depth += 1
        elif text[i] in ")]":
            depth -= 1
            if depth == 0:
                inner = text[open_paren + 1:i]
                out, buf, d = [], "", 0
                for ch in inner:
                    if ch in "([":
                        d += 1
                    elif ch in ")]":
                        d -= 1
                    if ch == "," and d == 0:
                        out.append(buf.strip())
                        buf = ""
                    else:
                        buf += ch
                out.append(buf.strip())
                return out
    return None


def literal(arg):
    """The string a slot shows, or None when it is not a plain literal."""
    m = re.fullmatch(r'"((?:[^"\\]|\\.)*)"', arg)
    return m.group(1) if m else None


def line_of(src, pos):
    return src[:pos].count("\n") + 1


LABEL_CALL = re.compile(r"setTouchNavLabels\s*\([^;]*?\)\s*;", re.S)


def wired(scope, pin):
    """Is this slot's pin used for anything besides being labelled?

    Deliberately agnostic about how. There are at least four spellings in
    this tree: isTouchNavButtonPressedEdge(BTN_RIGHT) directly,
    isButtonPressed(BTN_DOWN), which tries the PCF8574 and falls through to
    the nav bar, ducky's `static EdgeBtn ebDown{BTN_DOWN}` wrapper objects,
    and waits like resetWaitNavRelease(BTN_LEFT). Enumerating the call shapes
    got ducky's wrappers wrong and reported eleven working buttons as dead,
    which is how a check teaches people to ignore it.

    What the rule actually needs is narrower than "is it read": a labelled
    slot whose pin is never named anywhere nearby is drifted, whatever the
    mechanism. So the label calls are removed first, and then the question is
    just whether the constant appears at all.
    """
    return re.search(r"\b%s\b" % pin, LABEL_CALL.sub(" ", scope)) is not None

NAMESPACE = re.compile(r"^namespace\s+(\w+)\s*\{", re.M)


def enclosing_namespace(src, pos):
    """(text to search, True) for the named namespace around pos.

    Falls back to (whole file, False) when there is none, which is the case
    for the features that live at file scope. Braces are matched from the
    namespace's opening one rather than trusting indentation.
    """
    best = None
    for m in NAMESPACE.finditer(src):
        if m.start() > pos:
            break
        i, depth = m.end() - 1, 0
        while i < len(src):
            if src[i] == "{":
                depth += 1
            elif src[i] == "}":
                depth -= 1
                if depth == 0:
                    break
            i += 1
        if m.start() <= pos <= i:
            best = src[m.start():i + 1]      # innermost wins
    return (best, True) if best else (src, False)


def main():
    sources = sorted(SKETCH.glob("*.cpp"))
    if not sources:
        print("no sources under %s" % SKETCH, file=sys.stderr)
        return 1

    checks = 0
    failures = []
    # how many of rule 6's assertions were namespace-scoped vs file-wide
    tight, loose = [0], [0]

    for path in sources:
        src = path.read_text(encoding="utf-8", errors="replace")

        # Rules 1 and 2: what each slot says.
        for m in SET_LABELS.finditer(src):
            if FORWARDER.match(src, m.start()):
                continue
            args = split_args(src, m.end() - 1)
            if args is None or len(args) != 5:
                continue
            checks += 1
            where = "%s:%d" % (path.name, line_of(src, m.start()))
            centre = literal(args[2])
            left = literal(args[0])
            if centre == "":
                failures.append(
                    '%s: centre slot is "" -- that is the exit button, with '
                    "no label on it" % where)
            if left is not None and left.lower() in LEAVE_WORDS and centre == "":
                failures.append(
                    "%s: left says %r while the exit is the unlabelled centre"
                    % (where, left))

            # Rule 6: a labelled side slot is a slot something reads.
            #
            # The slots are positional -- (left, down, center, up, right) --
            # while the handler names its button, so a label can sit on one
            # slot with the code reading another and nothing says they have
            # drifted. Reset SD was written with "Rescan" on down and the
            # handler on BTN_UP: Down was labelled and dead, Up was live and
            # blank, and all 342 other assertions here passed. Same failure
            # as rule 2 -- a button promising what it is not wired to -- on
            # the side slots, and without needing the word "back" in it.
            #
            # Scoped to the enclosing named namespace, not the file. That is
            # not fastidiousness: utils.cpp is 4,000 lines and reads BTN_UP in
            # an unrelated function, so a file-level rule passes the exact bug
            # it was written for. Features that sit at file scope, which is
            # most of wifi.cpp, only get the file-level test, and the count
            # below says how many of each ran.
            scope, scoped = enclosing_namespace(src, m.start())
            for slot, pin in ((0, "BTN_LEFT"), (1, "BTN_DOWN"),
                              (3, "BTN_UP"), (4, "BTN_RIGHT")):
                label = literal(args[slot])
                if not label:
                    continue          # nullptr, "" or computed: nothing shown
                checks += 1
                if scoped:
                    tight[0] += 1
                else:
                    loose[0] += 1
                if not wired(scope, pin):
                    failures.append(
                        "%s: %s slot says %r and nothing in %s reads %s"
                        % (where, pin[4:].lower(), label,
                           "that namespace" if scoped else "the file", pin))

        # Rule 3: whoever sets labels paints them somewhere.
        sets = [m for m in SET_LABELS.finditer(src)
                if not FORWARDER.match(src, m.start())]
        if sets:
            checks += 1
            if not REPAINT.search(src):
                failures.append(
                    "%s: sets nav labels %dx and never calls "
                    "redrawTouchButtonBar() -- the bar keeps the menu's icons"
                    % (path.name, len(sets)))

        # Rule 4: the repaint is not undone before the function returns.
        # Scope runs from the repaint to the next line closing at column 0,
        # which in this tree is the end of the enclosing function.
        for m in REPAINT.finditer(src):
            checks += 1
            close = src.find("\n}", m.end())
            span = src[m.end():close if close != -1 else len(src)]
            cm = CLEAR.search(span)
            if cm:
                failures.append(
                    "%s:%d: the nav bar is repainted and then %s() clears it "
                    "again before the function returns"
                    % (path.name, line_of(src, m.start()),
                       cm.group(0).rstrip("( ").strip()))

        # Rule 5: a full-repaint helper puts the bar back.
        #
        # The opposite of rule 4, and the one that was reported from the
        # board instead of caught here. A redraw(bool full) that fillScreens
        # and does not repaint leaves every caller responsible for
        # remembering, and the callers that forget are the ones that run
        # rarely: a dwell alert, backing out of a result view. Those are
        # exactly the paths nobody exercises while developing.
        if "setTouchNavLabels" not in src:
            continue
        rm = re.search(r"void redraw\(bool \w+\) \{", src)
        if rm is None:
            continue
        checks += 1
        close = src.find("\n}", rm.end())
        body = src[rm.end():close if close != -1 else len(src)]
        if CLEAR.search(body) and not REPAINT.search(body):
            failures.append(
                "%s:%d: redraw() clears the screen and never repaints the "
                "nav bar, so every caller has to remember and the rare ones "
                "will not" % (path.name, line_of(src, rm.start())))

    for f in failures:
        print("  FAIL  " + f)

    if failures:
        print("\nFAILED: %d of %d" % (len(failures), checks))
        return 1

    print("  ok    every centre slot is pressable, painted, and not cleared "
          "afterwards")
    print("  ok    every labelled side slot has its pin wired to something "
          "(%d within a namespace, %d file-wide)" % (tight[0], loose[0]))
    print("\n%d checks passed" % checks)
    return 0


if __name__ == "__main__":
    sys.exit(main())
