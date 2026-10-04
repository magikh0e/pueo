"""Checks the rotating-tail detector in ESP32-DIV/TrackerFollow.cpp.

This is a heuristic, so the checks that decide whether it is worth shipping
are the ones where it must stay QUIET. Anything can raise an alarm. The
question is whether it raises one in a shopping centre, and whether it stays
silent next to your own desk.

The scenarios are scripted minute by minute:

    stationary_desk      you, sitting still, near your own AirTag
    concourse            walking through a crowd of other people's tags
    clone_following      the published clone, keeping station, cycling keys
    clone_passing        the same clone, in the room but not following
    quiet                nothing at all

Only clone_following may fire. The rest are the false-positive budget and
the reason this file exists.

    python tools/check_tracker_follow.py
"""
# This check does not read the firmware: it reimplements the code below and
# tests the reimplementation, so a change to the C cannot fail it. Proved on
# check_spotter_merge.py, where removing the under-reporting guard its own
# docstring exists to protect left it passing.
#
# The function bodies are pinned by hash and the transcribed constants are
# compared by value, which is the better report of the two: it can name the
# constant that moved. Neither can tell you the transcription is correct.
# Re-pin only after reading the C and bringing the Python into line.
from transcript_guard import guard, guard_consts

guard("TrackerFollow.cpp", "sighting", "309d8b9458810aea")
guard("TrackerFollow.cpp", "pickLane", "34654bbc523fd27c")
guard("TrackerFollow.cpp", "lookUp", "a916b2805b07b25d")
guard("TrackerFollow.cpp", "qualifiesByRate",
      "672d4700c25b7890")
guard_consts("TrackerFollow.h", {
    "kLaneWidthDb": 6,
    "kMaxLanes": 8,
    "kGapToleranceMs": 20000,
    "kLaneIdleMs": 120000,
    "kIdentityMs": 300000,
    "kSeenSlots": 64,
    "kChurnWindowMs": 60000,
    "kChurnSlots": 48,
    "kIdentityMinSightings": 3,
    "kMsPerIdentity": 240000,
})

import random

# ── constants, transcribed from TrackerFollow.h ────────────────────────────
LANE_WIDTH_DB = 6
MAX_LANES = 8
GAP_TOLERANCE_MS = 20000
LANE_IDLE_MS = 120000
IDENTITY_MS = 300000
SEEN_SLOTS = 64
CHURN_WINDOW_MS = 60000
CHURN_SLOTS = 48
IDENTITY_MIN_SIGHTINGS = 3
MS_PER_IDENTITY = 240000

NONE, WEAK, LIKELY, STRONG = 0, 1, 2, 3
NAMES = {NONE: "None", WEAK: "Weak", LIKELY: "Likely", STRONG: "Strong"}

GRADES = [            # (hold ms, identities, verdict)
    (600000, 12, STRONG),
    (300000, 6, LIKELY),
    (120000, 3, WEAK),
]


class State:
    """TrackerFollow::State, transcribed."""

    def __init__(self):
        self.lanes = [None] * MAX_LANES        # dict or None
        self.seen = []                          # [hash, ms] pairs, capped
        self.seen_next = 0
        self.churn_ring = [0] * CHURN_SLOTS
        self.churn_next = 0
        self.new_identities = 0
        self.sightings = 0
        self.seen_slots = [None] * SEEN_SLOTS

    # -- lookUp(): (isNew, matured) --
    def look_up(self, ms, h):
        for i in range(SEEN_SLOTS):
            e = self.seen_slots[i]
            if e is not None and e["hash"] == h:
                if ms - e["ms"] > IDENTITY_MS:
                    self.seen_slots[i] = dict(hash=h, ms=ms, count=1,
                                              credited=False)
                    return True, False
                e["ms"] = ms
                e["count"] += 1
                if not e["credited"] and e["count"] >= IDENTITY_MIN_SIGHTINGS:
                    e["credited"] = True
                    return False, True
                return False, False
        self.seen_slots[self.seen_next] = dict(hash=h, ms=ms, count=1,
                                               credited=False)
        self.seen_next = (self.seen_next + 1) % SEEN_SLOTS
        return True, False

    def note_churn(self, ms):
        self.churn_ring[self.churn_next] = ms
        self.churn_next = (self.churn_next + 1) % CHURN_SLOTS
        self.new_identities += 1

    # -- pickLane() --
    def pick_lane(self, ms, rssi):
        best, best_delta = -1, LANE_WIDTH_DB + 1
        for i, L in enumerate(self.lanes):
            if L is None:
                continue
            d = abs(rssi - L["centre"])
            if d <= LANE_WIDTH_DB and d < best_delta:
                best, best_delta = i, d
        if best >= 0:
            return best
        for i, L in enumerate(self.lanes):
            if L is None:
                self.lanes[i] = dict(centre=rssi, firstMs=ms, lastMs=ms,
                                     identities=0, sightings=0)
                return i
        stale = min(range(MAX_LANES), key=lambda i: self.lanes[i]["lastMs"])
        self.lanes[stale] = dict(centre=rssi, firstMs=ms, lastMs=ms,
                                 identities=0, sightings=0)
        return stale

    # -- sighting() --
    def sighting(self, ms, rssi, h):
        self.sightings += 1
        is_new, matured = self.look_up(ms, h)
        if is_new:
            self.note_churn(ms)

        i = self.pick_lane(ms, rssi)
        L = self.lanes[i]
        if ms - L["lastMs"] > GAP_TOLERANCE_MS:
            L["firstMs"] = ms
            L["identities"] = 0
            L["sightings"] = 0
        L["centre"] = (L["centre"] * 3 + rssi) // 4
        L["lastMs"] = ms
        L["sightings"] += 1
        if matured:
            L["identities"] += 1

    # -- tick() --
    def tick(self, ms):
        for i, L in enumerate(self.lanes):
            if L is not None and ms - L["lastMs"] > LANE_IDLE_MS:
                self.lanes[i] = None

    # -- verdict() --
    def verdict(self, ms):
        best = NONE
        for L in self.lanes:
            if L is None or ms - L["lastMs"] > GAP_TOLERANCE_MS:
                continue
            held = L["lastMs"] - L["firstMs"]
            for hold, ids, v in GRADES:
                if (held >= hold and L["identities"] >= ids
                        and L["identities"] * MS_PER_IDENTITY >= held):
                    best = max(best, v)
                    break
        return best

    def churn(self, ms):
        return sum(1 for t in self.churn_ring
                   if t != 0 and ms - t <= CHURN_WINDOW_MS)


def h(n):
    """Stand-in for a hashed BLE address."""
    return (n * 2654435761) & 0xFFFFFFFF


checks = 0


def case(name, cond, detail=""):
    global checks
    assert cond, "FAILED: %s %s" % (name, detail)
    checks += 1


# ── scenario: your own desk ────────────────────────────────────────────────
# You are still. Your AirTag is still. It rotates on Apple's slow schedule,
# so half an hour beside it yields two identities. Nothing should fire.
st = State()
ident = 0
for sec in range(0, 30 * 60, 2):
    ms = sec * 1000
    if sec % (15 * 60) == 0:
        ident += 1
    st.sighting(ms, -55 + random.Random(sec).randint(-2, 2), h(ident))
    st.tick(ms)
end = 30 * 60 * 1000
case("desk: half an hour beside your own tag is quiet",
     st.verdict(end) == NONE,
     "got %s, lane identities %s" % (NAMES[st.verdict(end)],
                                     [L["identities"] for L in st.lanes if L]))

# The property the rate gate buys: sitting still does not eventually fire.
# A count alone creeps -- 30 min gives 2 identities, 45 gives 3, and the old
# Weak threshold was 3. Rate does not accumulate, so three hours is as quiet
# as thirty minutes.
for hours_min in (45, 90, 180):
    st = State()
    for sec in range(0, hours_min * 60, 2):
        ms = sec * 1000
        st.sighting(ms, -55, h(sec // (15 * 60)))
        st.tick(ms)
    case("desk: still quiet after %d minutes" % hours_min,
         st.verdict(hours_min * 60 * 1000) == NONE,
         "got %s" % NAMES[st.verdict(hours_min * 60 * 1000)])

# ── scenario: a concourse ──────────────────────────────────────────────────
# Walking. Many strangers' devices, each in range briefly, RSSI all over the
# place. Identities churn hard; nothing holds a lane.
st = State()
rng = random.Random(7)
for sec in range(0, 20 * 60, 2):
    ms = sec * 1000
    for _ in range(rng.randint(0, 3)):
        st.sighting(ms, rng.randint(-95, -45), h(rng.randint(0, 5000)))
    st.tick(ms)
end = 20 * 60 * 1000
case("concourse: a crowd does not read as a tail",
     st.verdict(end) == NONE,
     "got %s, churn %d/min" % (NAMES[st.verdict(end)], st.churn(end)))
case("concourse: churn is high even though the verdict is None",
     st.churn(end) > 10, "churn %d" % st.churn(end))

# ── scenario: the clone, following ─────────────────────────────────────────
# Keeps station at roughly one distance, mints a new key every 30 s.
st = State()
rng = random.Random(11)
for sec in range(0, 20 * 60, 2):
    ms = sec * 1000
    st.sighting(ms, -62 + rng.randint(-3, 3), h(10000 + sec // 30))
    st.tick(ms)
end = 20 * 60 * 1000
case("clone following: fires", st.verdict(end) == STRONG,
     "got %s" % NAMES[st.verdict(end)])

# how soon
st = State()
first = None
for sec in range(0, 20 * 60, 2):
    ms = sec * 1000
    st.sighting(ms, -62, h(10000 + sec // 30))
    st.tick(ms)
    if first is None and st.verdict(ms) != NONE:
        first = sec
case("clone following: noticed within four minutes", first is not None and
     first <= 240, "first alert at %ss" % first)

# ── scenario: the clone, present but not following ─────────────────────────
# Same key rotation, but the distance jumps around: it is in the building,
# not behind you. Lanes fragment, so no run accumulates.
st = State()
rng = random.Random(13)
for sec in range(0, 20 * 60, 2):
    ms = sec * 1000
    st.sighting(ms, rng.choice([-45, -60, -75, -90]) + rng.randint(-3, 3),
                h(20000 + sec // 30))
    st.tick(ms)
end = 20 * 60 * 1000
case("clone not following: stays below Strong",
     st.verdict(end) < STRONG, "got %s" % NAMES[st.verdict(end)])

# ── scenario: nothing ──────────────────────────────────────────────────────
st = State()
for sec in range(0, 10 * 60, 2):
    st.tick(sec * 1000)
case("quiet: no sightings, no verdict", st.verdict(600000) == NONE)
case("quiet: churn is zero", st.churn(600000) == 0)

# ── a gap breaks the run ───────────────────────────────────────────────────
st = State()
for sec in range(0, 8 * 60, 2):              # 8 min of a following clone
    st.sighting(sec * 1000, -62, h(30000 + sec // 30))
v_before = st.verdict(8 * 60 * 1000)
st.sighting(8 * 60 * 1000 + GAP_TOLERANCE_MS + 5000, -62, h(99999))
after = 8 * 60 * 1000 + GAP_TOLERANCE_MS + 5000
case("a run that goes quiet then returns starts over",
     v_before != NONE and st.verdict(after) == NONE,
     "before %s, after %s" % (NAMES[v_before], NAMES[st.verdict(after)]))

# ── a stale verdict does not linger ────────────────────────────────────────
st = State()
for sec in range(0, 20 * 60, 2):
    st.sighting(sec * 1000, -62, h(40000 + sec // 30))
live = 20 * 60 * 1000
case("fires while present", st.verdict(live) == STRONG)
case("and stops once it leaves",
     st.verdict(live + GAP_TOLERANCE_MS + 1000) == NONE)

# ── one identity, however long, is never a rotating tail ───────────────────
st = State()
for sec in range(0, 60 * 60, 2):
    st.sighting(sec * 1000, -62, h(55555))
case("an hour of a single identity is not a rotating tail",
     st.verdict(60 * 60 * 1000) == NONE)

# ── fuzz: never crash, never exceed bounds ─────────────────────────────────
rng = random.Random(20260919)
for _ in range(300):
    st = State()
    ms = 0
    for _ in range(400):
        ms += rng.randint(0, 40000)
        st.sighting(ms, rng.randint(-128, 127), h(rng.randint(0, 10**6)))
        if rng.random() < 0.3:
            st.tick(ms)
        assert st.verdict(ms) in (NONE, WEAK, LIKELY, STRONG)
        assert 0 <= st.churn(ms) <= CHURN_SLOTS
        assert sum(1 for L in st.lanes if L is not None) <= MAX_LANES
    checks += 1

print("ok -- %d checks" % checks)
print("the four quiet scenarios are the point; only a clone keeping station "
      "and cycling keys fires")
