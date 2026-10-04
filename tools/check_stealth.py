#!/usr/bin/env python3
"""Stealth Mode is worth exactly the list of things that honour it.

"Nothing transmits" is a claim about every transmit path in the firmware, and
a claim like that decays silently: somebody adds a feature, or moves a scan,
and the switch keeps reading ON while the antenna does not agree. Nothing in
the build says otherwise, because transmitting compiles exactly as well as
not transmitting.

So this walks the transmit paths rather than trusting them. Three kinds:

  1. Features whose job is to transmit. Each refuses at the top of its own
     setup(), which is the function both dispatch chains call -- the button
     chain and the touch chain are separate copies of the same if/else, and
     gating there would mean gating twice and finding out later that one of
     them was missed.

  2. Scans that transmit while looking like receivers. A Wi-Fi scan is ACTIVE
     unless its third argument says otherwise, and a BLE scan answers every
     advertiser it hears unless you turn that off. Both are made passive
     under stealth rather than blocked, because a tool that listens should
     keep listening.

  3. Raw 802.11 injection. Every esp_wifi_80211_tx() in the tree has to sit
     inside a namespace whose setup() is gated -- which is what stops a new
     transmitter being added to an old screen.

    python tools/check_stealth.py

Reads source; needs no board.
"""
import re
import sys
from pathlib import Path

REPO = Path(__file__).resolve().parent.parent
SKETCH = REPO / "ESP32-DIV"

CHECKS = 0
FAILED = []


def ok(name, cond, detail=""):
    global CHECKS
    CHECKS += 1
    if cond:
        print("  ok    %s" % name)
    else:
        print("  FAIL  %s%s" % (name, ("  -- " + detail) if detail else ""))
        FAILED.append(name)


def read(rel):
    return (SKETCH / rel).read_text(encoding="utf-8", errors="replace")


def func_body(src, signature):
    i = src.find(signature)
    if i < 0:
        return ""
    j = src.index("{", i)
    depth = 0
    for k in range(j, len(src)):
        if src[k] == "{":
            depth += 1
        elif src[k] == "}":
            depth -= 1
            if depth == 0:
                return src[j:k + 1]
    return ""


def enclosing_namespace(src, at):
    """Innermost `namespace X {` still open at offset `at`."""
    stack = []
    depth = 0
    for m in re.finditer(r"namespace\s+(\w+)\s*\{|\{|\}", src[:at]):
        tok = m.group(0)
        if tok.startswith("namespace"):
            stack.append((depth, m.group(1)))
            depth += 1
        elif tok == "{":
            depth += 1
        else:
            depth -= 1
            while stack and stack[-1][0] >= depth:
                stack.pop()
    return stack[-1][1] if stack else None


# The transmitters, by the setup() both dispatch chains call and the label
# the refusal screen shows. The label is checked too: a screen that names
# some other feature is a screen nobody believes.
TRANSMITTERS = [
    ("wifi.cpp",      "void beaconSpamSetup()",        "Beacon Spammer"),
    ("wifi.cpp",      "void deautherSetup()",          "WiFi Deauther"),
    ("wifi.cpp",      "void probeRequestFloodSetup()", "Probe Request Flood"),
    ("wifi.cpp",      "void cportalSetup()",           "Captive Portal"),
    ("wifi.cpp",      "void hiddenSsidSetup()",        "Hidden SSID Revealer"),
    ("wifi.cpp",      "void wpsScannerSetup()",        "WPS Scanner"),
    ("wifi.cpp",      "void arpScannerSetup()",        "ARP Scanner"),
    ("wifi.cpp",      "void karmaSetup()",             "Karma Attack"),
    # Not an attack, and still a transmitter: a SoftAP beacons every 100 ms
    # with this device's AP MAC in it, which is the one thing stealth is for.
    # It is not in TX_NAMESPACES because it injects nothing -- it asks the
    # stack for an access point and serves HTTP over it.
    ("FileServer.cpp", "void setup()",                 "File Transfer"),
    ("bluetooth.cpp", "void blejamSetup()",            "BLE Jammer"),
    ("bluetooth.cpp", "void spooferSetup()",           "BLE Spoofer"),
    ("bluetooth.cpp", "void sourappleSetup()",         "Sour Apple"),
    ("bluetooth.cpp", "void airTagSetup()",            "AirTag Spoofer"),
    ("bluetooth.cpp", "void prokillSetup()",           "Proto Kill"),
    ("bluetooth.cpp", "void esbReplaySetup()",         "ESB Replay"),
    ("bluetooth.cpp", "void mouseJackInjectSetup()",   "MouseJack Inject"),
    ("subghz.cpp",    "void ReplayAttackSetup()",      "Replay Attack"),
    ("subghz.cpp",    "void subjammerSetup()",         "SubGHz Jammer"),
    ("subghz.cpp",    "void subBruteSetup()",          "De Bruijn / Brute"),
    ("subghz.cpp",    "void saveSetup()",              "Saved Profile"),
]

# The transmit paths that are not features and have no setup() to gate, so
# they carry the check inside the function that does the transmitting and
# report it to their caller instead of drawing the refusal screen. A caller
# that forgets is then a caller that gets a refusal it has to display, rather
# than a radio that keys.
#
#   (file, signature, the enum value it must return)
GATED_CALLS = [
    ("DeviceInfo.cpp",   "void read(", "Status::Refused"),
    ("FastPairProbe.cpp", "void run(",  "Outcome::Refused"),
]

# Namespaces allowed to call esp_wifi_80211_tx, because each has a gated
# setup() above. A new one here is a new transmitter that nothing refuses.
TX_NAMESPACES = {
    "BeaconSpammer", "Deauther", "ProbeRequestFlood", "CaptivePortal",
    "HiddenSsidReveal", "KarmaAttack",
}


def main():
    print("every feature whose job is to transmit refuses first:")
    for fname, sig, label in TRANSMITTERS:
        body = func_body(read(fname), sig)
        ok("%-22s (%s)" % (label, fname),
           bool(body) and "Stealth::refuse" in body,
           "no body found for %r" % sig if not body
           else "starts transmitting with Stealth Mode on")
        ok("  and names itself",
           bool(body) and ('"%s"' % label) in body,
           "the refusal screen names some other feature")

    print("\nthe transmit paths with no menu entry gate themselves:")
    for fname, sig, refused in GATED_CALLS:
        body = func_body(read(fname), sig)
        ok("%-20s (%s)" % (sig.split("(")[0].split()[-1] + "()", fname),
           bool(body) and "Stealth::on()" in body and refused in body,
           "no body found for %r" % sig if not body
           else "transmits with Stealth Mode on, or does not say it refused")
        # A gate that runs after the connection is open is not a gate. The
        # check is crude on purpose: the refusal has to come before anything
        # that touches the radio.
        if body:
            gate = body.find("Stealth::on()")
            radio = min([i for i in (body.find("createClient"),
                                     body.find("NimBLEDevice::"),
                                     len(body)) if i >= 0])
            ok("  and refuses before it touches the radio", gate < radio,
               "the gate is after the first radio call")

    print("\nRFID is gated once, where all of it is launched:")
    ino = (SKETCH / "ESP32-DIV.ino").read_text(encoding="utf-8", errors="replace")
    rfid = func_body(ino, "static void otherRfidPlaceholderAction(int idx)")
    # Reading a card energises the field and waits for an answer, so "read"
    # transmits as much as "clone" -- the whole menu is one gate.
    ok("otherRfidPlaceholderAction refuses",
       bool(rfid) and "Stealth::refuse" in rfid)

    print("\nthe scans that transmit while looking like receivers go quiet:")
    for fname in ("wifi.cpp", "gps.cpp", "bluetooth.cpp"):
        src = read(fname)
        # scanNetworks(async, hidden, passive, dwell): a literal false in the
        # third slot is an active scan, and so is the two-argument form,
        # which defaults to one.
        active = re.findall(r"WiFi\.scanNetworks\([^,)]+,[^,)]+,\s*false", src)
        short = re.findall(r"WiFi\.scanNetworks\([^,)]*,[^,)]*\)", src)
        ok("%s: no active WiFi scan" % fname, not active and not short,
           "%d explicit, %d relying on the default"
           % (len(active), len(short)))
        ok("%s: no active BLE scan" % fname,
           "setActiveScan(true)" not in src,
           "an active scan answers every advertiser it hears")

    print("\nthe wardriver still listens, and does not upload:")
    gps = read("gps.cpp")
    ok("the WiGLE upload is refused",
       re.search(r"Stealth::on\(\)\s*\)\s*\{\s*\n\s*wardNotify\(\"WiGLE\"", gps)
       is not None,
       "uploading joins an access point, which transmits")

    print("\nevery raw 802.11 transmit sits behind a gate:")
    wifi = read("wifi.cpp")
    sites = [m.start() for m in re.finditer(r"esp_wifi_80211_tx\s*\(", wifi)]
    ok("found the injection sites", len(sites) >= 5, "%d" % len(sites))
    seen = set()
    for at in sites:
        ns = enclosing_namespace(wifi, at)
        seen.add(ns)
    for ns in sorted(x for x in seen if x):
        ok("  %s is a gated namespace" % ns, ns in TX_NAMESPACES,
           "injects frames and nothing refuses it under stealth")
    ok("  no injection outside a namespace", None not in seen,
       "a file-scope transmit has no feature to gate")

    print("\nand the switch exists:")
    store_h = read("SettingsStore.h")
    ok("AppSettings carries stealthMode", "bool     stealthMode" in store_h)
    ui = read("utils.cpp")
    ok("Settings offers the row",
       '{"Stealth Mode", &AppSettings::stealthMode' in ui)
    st = read("Stealth.cpp")
    ok("refuse() reads the setting and exits the feature",
       "settings().stealthMode" in read("Stealth.cpp") + st
       and "feature_exit_requested = true" in st)

    print("\nand the docs list the same tools the code gates:")
    check_docs()

    print()
    if FAILED:
        print("FAILED: %d of %d" % (len(FAILED), CHECKS))
        return 1
    print("%d checks passed" % CHECKS)
    return 0


# ── the list, written out three more times ──────────────────────────────────
#
# TRANSMITTERS above is the list. The user guide prints it as a box and
# counts it in words, and the README counts it in words again. Three copies,
# and the docs are the two that nobody compiles.
#
# This is not hypothetical here: the box said nineteen through the whole of
# the AP Tracker work, and AP Tracker does not transmit, so the number
# happened to stay right by luck rather than by anything noticing. A feature
# that does transmit would have left the guide telling somebody that their
# device goes quiet when it does not.
#
# The box abbreviates to fit four columns, so the names are matched as
# subsequences rather than literally: "Probe Req Flood" against "Probe
# Request Flood", "Hidden SSID Rev." against "Hidden SSID Revealer". Loose
# enough to survive the layout, strict enough that a name not in the code at
# all matches nothing, and the match has to be one to one.

WORDS = {19: "Nineteen", 20: "Twenty", 21: "Twenty-one", 22: "Twenty-two",
         23: "Twenty-three", 24: "Twenty-four", 25: "Twenty-five"}


def squash(s):
    return re.sub(r"[^a-z0-9]", "", s.lower())


def is_subseq(short, long):
    it = iter(long)
    return all(c in it for c in short)


def site_pages():
    """The published pages, via .publish.local. Empty when the site is not
    on this machine, which is the case inside the source archive."""
    pub = REPO / ".publish.local"
    if not pub.is_file():
        return []
    m = re.search(r"PUEO_PUBLISH_DIR\s*=\s*['\"]?([^'\"\n]+)",
                  pub.read_text(encoding="utf-8", errors="replace"))
    if not m:
        return []
    raw = m.group(1).strip()
    if re.match(r"^/[a-zA-Z]/", raw):
        raw = raw[1] + ":" + raw[2:]
    d = Path(raw)
    return sorted(d.glob("*.html")) if d.is_dir() else []


def check_docs():
    labels = [t[2] for t in TRANSMITTERS]
    n = len(labels)
    word = WORDS.get(n)

    guide = (REPO / "docs" / "pueo" / "user-guide.md").read_text(
        encoding="utf-8", errors="replace")
    readme = (REPO / "README.md").read_text(encoding="utf-8", errors="replace")

    ok("the count has a word for it", word is not None,
       "%d gated features and no spelling in WORDS" % n)
    if word is None:
        return

    # The box is the fenced block right after "What it refuses:".
    m = re.search(r"What it refuses:\s*\n+```\n(.*?)```", guide, re.S)
    ok("found the box in the user guide", m is not None)
    if m:
        # Columns are runs of two or more spaces.
        entries = [e for line in m.group(1).strip().splitlines()
                   for e in re.split(r"\s{2,}", line.strip()) if e]
        ok("  it lists %d tools" % n, len(entries) == n,
           "the box has %d: %s" % (len(entries), entries))

        unmatched_box, pool = [], list(labels)
        for e in entries:
            hit = next((l for l in pool if is_subseq(squash(e), squash(l))),
                       None)
            if hit is None:
                unmatched_box.append(e)
            else:
                pool.remove(hit)
        ok("  every name in it is a tool the code gates", not unmatched_box,
           "no gated feature matches %s" % unmatched_box)
        ok("  and every gated tool is in it", not pool,
           "gated and not listed: %s" % [l for l in pool])

    ok("the user guide's count is right", ("%s tools" % word) in guide,
       "it does not say %r" % ("%s tools" % word))
    # And the website, which said Nineteen for two releases after it became
    # twenty. The firmware's two copies were swept and the site's third was
    # not, because nothing here knew the site existed.
    site = site_pages()
    if not site:
        print("  --    the site is not reachable, skipping it")
    else:
        html = "".join(p.read_text(encoding="utf-8", errors="replace")
                       for p in site)
        ok("the website's count is right",
           re.search(r"%s features" % word, html) is not None,
           "no page says %r" % ("%s features" % word))
        stale = [w for k, w in WORDS.items() if k != n
                 and re.search(r"%s features\b" % w, html)]
        ok("  and no page says an older one", not stale,
           "a page also says %s" % stale)

    ok("the README's count is right",
       ("%s\nfeatures that transmit" % word) in readme
       or ("%s features that transmit" % word) in readme,
       "it does not say %r" % ("%s features that transmit" % word))


if __name__ == "__main__":
    sys.exit(main())
