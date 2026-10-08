#include "Spotter.h"

#include "SpiBus.h"
#include "config.h"
#include "icon.h"
#include "shared.h"
#include "utils.h"

#include <SD.h>
#include <esp_wifi.h>
#include <string.h>
#include <strings.h>

namespace Spotter {

namespace {

constexpr int      kMaxHits      = 48;
constexpr uint32_t kHopMs        = 260;   // per-channel dwell
constexpr uint8_t  kChanFirst    = 1;
constexpr uint8_t  kChanLast     = 13;
constexpr uint32_t kBleWindowMs  = 4000;  // BLE scan slice between WiFi hops
constexpr uint32_t kRedrawMs     = 400;
/* Three lines to a row. The 2.8" numbers are the originals, kept exactly:
 * that panel has never been booted and this is not the change to start
 * moving it with. */
constexpr int      kRowH         = 54;
constexpr int      kLine2        = 18;   /* second line, from the row top */
constexpr int      kLine3        = 36;

/* ── dwell ────────────────────────────────────────────────────────────────
 *
 * The question this whole feature is pointed at is not "what is nearby",
 * which the list already answers, but "has something been nearby for too
 * long". A plate reader you walk past is present for ten seconds. A tracker
 * in your bag is present for the whole afternoon.
 *
 * What the device can honestly say is "this has been in range for 22
 * minutes". What it cannot say is "you are being followed": with no fix of
 * its own it cannot tell a tracker that moves with you from a camera you
 * are standing under. The label says DWELL rather than FOLLOWING for that
 * reason, and the distinction is the whole of the honesty here.
 *
 * Ten minutes because it is longer than any doorway, queue or set of
 * traffic lights, and shorter than a meal.
 *
 * The staleness window has to be longer than the gap between two sightings
 * of the same device, or something that advertises intermittently drops out
 * of the alarm between its own packets. The bench beacon is the worst case
 * to hand: it gives each BLE decoy a three-second slice in every fifteen,
 * so twelve seconds pass between one sighting and the next, and a twelve
 * second window would have sat exactly on that boundary. Thirty is clear of
 * it and still short enough that a device which has genuinely gone stops
 * alarming while you are still looking at the screen. */
constexpr uint32_t kDwellAlarmMs = 10u * 60u * 1000u;
constexpr uint32_t kDwellStaleMs = 30u * 1000u;

/* Tagged elements are walked with a hard cap as well as a length bound. A
 * probe request carries nowhere near this many; the cap is there so that a
 * frame built to lie about its lengths ends the loop rather than running it. */
constexpr int      kMaxIes       = 32;

/* ── capture ──────────────────────────────────────────────────────────────
 *
 * There is no fingerprint table and there cannot be one until somebody
 * stands next to a camera with this in their hand, so the useful thing to
 * build first is the means of writing one down. Capture appends a row per
 * device to a CSV on the card.
 *
 * It records every device it hears, not only the ones a signature matched.
 * That is the entire point -- a fingerprint that already matched something
 * is one you already have -- and it is also the reason this is off until it
 * is switched on. A capture of the air around you is a list of the people
 * near you: their phones, their watches, their cars. It stays on the card,
 * nothing uploads it, and it is the operator's to delete.
 *
 * The frames arrive on the WiFi task and the card is written from the main
 * one, so the two are joined by a ring. Deduplication happens on the
 * producing side, because writing a row per frame would be hundreds a
 * second and the interesting content is one row per device.
 * ──────────────────────────────────────────────────────────────────────── */
constexpr int kCapRing  = 24;    // records waiting to reach the card
constexpr int kCapSeen  = 128;   // (address, fingerprint) pairs already written
constexpr int kCapBatch = 8;     // rows moved per flush; bounds the stack copy

struct CapRec {
  uint32_t ms;
  uint32_t fp;
  uint8_t  mac[6];
  int8_t   rssi;
  uint8_t  chan;
  uint8_t  ssidLen;
  char     ssid[33];
};

CapRec   s_cap[kCapRing];
uint8_t  s_capHead = 0;
uint8_t  s_capTail = 0;
uint32_t s_capDropped = 0;
bool     s_logJson    = false;   // fixed when the file opens

/* Direct-mapped rather than searched: this runs in the promiscuous callback
 * inside the critical section, so it is one compare rather than 128. Two
 * devices landing on the same slot take turns and get written twice, which
 * costs a duplicate row and no correctness. */
uint32_t s_capSeen[kCapSeen];

bool     s_logging   = false;
bool     s_logFailed = false;
bool     s_logBlocked = false;   // refused by Settings, not by the card
uint32_t s_logRows   = 0;
File     s_logFile;

Hit      s_hits[kMaxHits];
int      s_hitCount = 0;
uint32_t s_frames   = 0;
uint8_t  s_chan     = kChanFirst;
bool     s_running  = false;
int      s_scroll   = 0;
uint32_t s_lastHop  = 0;
uint32_t s_lastDraw = 0;
bool     s_dirty    = true;

/* The promiscuous callback runs on the WiFi task, so anything it touches is
 * shared. The table is only appended to and the UI only reads, but a hit
 * arriving mid-redraw could still tear a row, hence the flag rather than
 * drawing from the callback. */
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* FNV-1a. Cheap, well distributed, and deliberately not cryptographic:
 * nothing here depends on it being hard to collide on purpose, only on it
 * separating the element sets that occur in practice. */
constexpr uint32_t kFnvBasis = 2166136261u;
constexpr uint32_t kFnvPrime = 16777619u;

inline uint32_t fnv1a(uint32_t h, uint8_t b) {
  return (h ^ b) * kFnvPrime;
}

/* A signature matches on its prefix, and on its length when it declares one.
 * The length is what makes a short prefix usable: KARR advertises "QT " or
 * "DR " followed by exactly eight characters, and a bare case-insensitive
 * "dr " would otherwise claim every device whose name starts that way. */
bool nameMatch(const char* s, const NameSig& sig) {
  if (!s || !sig.prefix) {
    return false;
  }
  if (strncasecmp(s, sig.prefix, strlen(sig.prefix)) != 0) {
    return false;
  }
  return sig.exactLen == 0 || strlen(s) == sig.exactLen;
}

/* A needle found anywhere in the name, case-insensitively.
 *
 * Written out rather than strcasestr, which is not in this toolchain's
 * libc. O(n*m) over names of at most 31 characters and needles of at most
 * fifteen, so the loop is the clear way to write it.
 *
 * minNameLen is checked first because it is the cheap half, and because a
 * needle that is the whole name is the case it exists for: "Marauder" alone
 * is a deauther and "Marauder" inside a long name is somebody's speaker. */
bool nameContains(const char* s, const NameInSig& sig) {
  if (!s || !sig.needle || !*sig.needle) {
    return false;
  }
  const size_t n = strlen(s);
  if (sig.minNameLen != 0 && n < sig.minNameLen) {
    return false;
  }
  const size_t m = strlen(sig.needle);
  if (m > n) {
    return false;
  }
  for (size_t i = 0; i + m <= n; i++) {
    if (strncasecmp(s + i, sig.needle, m) == 0) {
      return true;
    }
  }
  return false;
}

/* The bytes after the company ID match this signature's prefix.
 *
 * md is the whole manufacturer data as NimBLE hands it over, company ID
 * included, so the payload starts at md[2]. A signature's prefix[0] is
 * md[2]; reading from md[0] instead would compare a company byte against a
 * payload byte and produce a hit that looks entirely plausible. */
bool mfgMatch(const uint8_t* md, size_t len, const MfgSig& sig) {
  if (!md || sig.prefixLen == 0 || sig.prefixLen > sizeof(sig.prefix)) {
    return false;
  }
  if (len < (size_t)2 + sig.prefixLen) {
    return false;
  }
  return memcmp(md + 2, sig.prefix, sig.prefixLen) == 0;
}

/* The first bytes of a service's data match. prefixLen 0 means the service
 * carrying any data at all is the signal, which is what DULT is: nothing
 * advertises 0xFCB2 except something saying it is a location tracker. */
bool svcDataMatch(const uint8_t* sd, size_t len, const SvcDataSig& sig) {
  if (sig.prefixLen == 0) {
    return true;
  }
  if (!sd || sig.prefixLen > sizeof(sig.prefix) || len < sig.prefixLen) {
    return false;
  }
  return memcmp(sd, sig.prefix, sig.prefixLen) == 0;
}

/* Find or create the row for this MAC. Returns null when the table is full,
 * which is deliberate: dropping new devices is better than evicting one the
 * operator is currently looking at. */
Hit* findOrAdd(const uint8_t* mac, bool viaBle, uint32_t fp) {
  for (int i = 0; i < s_hitCount; i++) {
    if (memcmp(s_hits[i].mac, mac, 6) == 0) {
      return &s_hits[i];
    }
  }

  /* No row for this address. Before making one: it may be a row already
   * here, wearing a new address.
   *
   * Only when both addresses are locally administered. A globally unique MAC
   * is a real, stable identifier and keeps its own row even if something in
   * range shares its element set -- two cameras of one model have the same
   * fingerprint, and folding those together would report one where there are
   * two. Two randomised addresses with the same element set are the
   * defensible case: that is what one radio rotating looks like.
   *
   * It is not proof. Two handsets of the same model, both randomising, are
   * indistinguishable from here, and this will merge them. The merge is
   * deliberately limited to the case where the alternative -- a row per
   * address -- is certainly wrong. */
  if (fp != 0 && !viaBle && (mac[0] & 0x02)) {
    for (int i = 0; i < s_hitCount; i++) {
      Hit& h = s_hits[i];
      if (!h.viaBle && h.fingerprint == fp && (h.mac[0] & 0x02)) {
        memcpy(h.mac, mac, 6);
        if (h.addrChanges < 255) {
          h.addrChanges++;
        }
        return &h;
      }
    }
  }

  if (s_hitCount >= kMaxHits) {
    return nullptr;
  }
  Hit& h = s_hits[s_hitCount++];
  memset(&h, 0, sizeof(h));
  memcpy(h.mac, mac, 6);
  h.firstMs = millis();
  h.viaBle = viaBle;
  h.kind = Kind::Unknown;
  h.conf = Conf::Weak;
  h.label = "?";
  h.rssiBest = -127;
  h.fingerprint = fp;
  return &h;
}

/* Called for every management frame, matched or not. Cheap on purpose: one
 * hash, one compare, one memcpy, all inside the lock the table already
 * uses. */
void captureNote(const uint8_t* mac, int8_t rssi, uint32_t fp,
                 const uint8_t* ssid, uint8_t ssidLen) {
  if (!s_logging) {
    return;
  }

  uint32_t key = fp;
  for (int i = 0; i < 6; i++) {
    key = fnv1a(key, mac[i]);
  }
  if (key == 0) {
    key = 1;                            // 0 marks an empty slot
  }

  portENTER_CRITICAL(&s_mux);
  const uint16_t slot = (uint16_t)(key % (uint32_t)kCapSeen);
  if (s_capSeen[slot] != key) {
    s_capSeen[slot] = key;

    const uint8_t next = (uint8_t)((s_capHead + 1) % kCapRing);
    if (next == s_capTail) {
      s_capDropped++;                   // card is not keeping up; say so later
    } else {
      CapRec& r = s_cap[s_capHead];
      r.ms   = millis();
      r.fp   = fp;
      memcpy(r.mac, mac, 6);
      r.rssi = rssi;
      r.chan = s_chan;
      r.ssidLen = (ssidLen > 32) ? 32 : ssidLen;
      if (r.ssidLen > 0 && ssid != nullptr) {
        memcpy(r.ssid, ssid, r.ssidLen);
      }
      r.ssid[r.ssidLen] = '\0';
      s_capHead = next;
    }
  }
  portEXIT_CRITICAL(&s_mux);
}

/* Moves whatever is waiting onto the card. Runs on the main task, never in
 * the callback: SD is on the shared SPI bus and a write is slow enough that
 * doing it from the promiscuous handler would drop frames.
 *
 * The bus is claimed each time rather than held. Touch is on the same bus
 * and re-claims it whenever it is polled, so holding would only mean fighting
 * over it; claim() is idempotent and cheap when nothing else intervened. */
void captureFlush() {
  if (!s_logFile) {
    return;                             // gated on the file, not on s_logging,
  }                                     // so captureStop can drain after it
                                        // has already stopped the producer

  CapRec batch[kCapBatch];
  int n = 0;
  portENTER_CRITICAL(&s_mux);
  while (s_capTail != s_capHead && n < kCapBatch) {
    batch[n++] = s_cap[s_capTail];
    s_capTail = (uint8_t)((s_capTail + 1) % kCapRing);
  }
  portEXIT_CRITICAL(&s_mux);

  if (n == 0) {
    return;
  }

  SpiBus::claim(SpiBus::Dev::Sd);
  for (int i = 0; i < n; i++) {
    const CapRec& r = batch[i];
    char head[160];
    if (s_logJson) {
      /* Same field names as the CSV header, so the two formats describe the
       * same thing. rnd is a boolean here rather than 0 or 1, because JSON
       * has booleans and CSV does not. */
      snprintf(head, sizeof(head),
               "{\"ms\":%lu,\"mac\":\"%02X:%02X:%02X:%02X:%02X:%02X\","
               "\"rnd\":%s,\"fp\":\"%08lX\",\"rssi_dbm\":%d,"
               "\"ch\":%u,\"ssid\":\"",
               (unsigned long)r.ms,
               r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
               (r.mac[0] & 0x02) ? "true" : "false",
               (unsigned long)r.fp, (int)r.rssi, (unsigned)r.chan);
    } else {
      snprintf(head, sizeof(head),
               "%lu,%02X:%02X:%02X:%02X:%02X:%02X,%d,%08lX,%d,%u,",
               (unsigned long)r.ms,
               r.mac[0], r.mac[1], r.mac[2], r.mac[3], r.mac[4], r.mac[5],
               (r.mac[0] & 0x02) ? 1 : 0,
               (unsigned long)r.fp, (int)r.rssi, (unsigned)r.chan);
    }
    s_logFile.print(head);

    /* The SSID is arbitrary bytes off the air going into a text file, and a
     * network named with a quote and a newline must not be able to forge rows
     * in somebody's capture.
     *
     * CSV has no escape, so it quotes the field and drops anything that could
     * end it early. JSON does have one, so the bytes survive: a quote or a
     * backslash is escaped and anything outside printable ASCII becomes
     * \uXXXX. That is the one place this format is not merely a different
     * spelling of the same row. */
    if (s_logJson) {
      for (uint8_t k = 0; k < r.ssidLen; k++) {
        const uint8_t c = (uint8_t)r.ssid[k];
        if (c == '"' || c == '\\') {
          s_logFile.print('\\');
          s_logFile.print((char)c);
        } else if (c >= 0x20 && c < 0x7F) {
          s_logFile.print((char)c);
        } else {
          char u[8];
          snprintf(u, sizeof(u), "\\u%04X", (unsigned)c);
          s_logFile.print(u);
        }
      }
      s_logFile.println("\"}");
    } else {
      s_logFile.print('"');
      for (uint8_t k = 0; k < r.ssidLen; k++) {
        const char c = r.ssid[k];
        if (c >= 32 && c < 127 && c != '"') {
          s_logFile.print(c);
        } else {
          s_logFile.print('.');
        }
      }
      s_logFile.println('"');
    }
    s_logRows++;
  }
  s_logFile.flush();
}

}  // namespace

/* A KARR module is a parked car with a known-weak immobiliser, and the paper
 * that documented it describes the attack starting with a name-pattern query
 * against a public wardriving database. Uploading these sightings would be
 * contributing to exactly that index, so the WiGLE conversion drops them.
 *
 * The local log keeps them. Withholding from a public database and lying to
 * the operator about what their own radio heard are different things, and
 * only the first is wanted. */
bool isVehicleName(const char* name) {
  if (!name || !name[0]) {
    return false;
  }
  for (size_t i = 0; i < kBleNameSigCount; i++) {
    if (kBleNameSigs[i].kind == Kind::Vehicle
        && nameMatch(name, kBleNameSigs[i])) {
      return true;
    }
  }
  return false;
}

namespace {

bool captureStart() {
  s_logFailed = false;
  s_logBlocked = false;

  /* Asked before the card is even looked at, so pressing REC with logging
   * switched off says so rather than saying "no SD" at a card that is
   * sitting right there. */
  if (!sdLoggingAllowed(LogApp::Surveillance)) {
    s_logBlocked = true;
    return false;
  }

  if (!isSDCardAvailable()) {
    s_logFailed = true;
    return false;
  }

  SpiBus::claim(SpiBus::Dev::Sd);
  /* Read once, here, and not again until the next file. Toggling the setting
   * mid-capture must not produce a file that is half one format. */
  s_logJson = settings().logJson;
  /* LOG_DIR, not the root. These went straight into the card's top level
   * from the beginning, which is how a Surveillance capture ended up beside
   * somebody's photographs. */
  if (!sdEnsureDir(LOG_DIR)) {
    s_logFailed = true;
    return false;
  }
  char path[64];
  snprintf(path, sizeof(path), s_logJson ? LOG_DIR "/spotter_%lu.jsonl"
                                         : LOG_DIR "/spotter_%lu.csv",
           (unsigned long)millis());
  s_logFile = SD.open(path, FILE_WRITE);
  if (!s_logFile) {
    s_logFailed = true;
    return false;
  }
  if (!s_logJson) {
    s_logFile.println("ms,mac,rnd,fp,rssi_dbm,ch,ssid");   // JSON names each row
  }
  s_logFile.flush();

  portENTER_CRITICAL(&s_mux);
  memset(s_capSeen, 0, sizeof(s_capSeen));
  s_capHead = 0;
  s_capTail = 0;
  s_capDropped = 0;
  portEXIT_CRITICAL(&s_mux);

  s_logRows = 0;
  s_logging = true;
  return true;
}

void captureStop() {
  s_logging = false;                    // the producer stops here
  if (s_logFile) {
    /* Drain what is still queued: the ring holds more than one batch. */
    for (int i = 0; i < (kCapRing / kCapBatch) + 1; i++) {
      captureFlush();
    }
    s_logFile.flush();
    s_logFile.close();
  }
}

/* Record a match. A second, different signature on the same device promotes
 * it: two weak hints agreeing is worth more than either alone, which is the
 * whole reason confidence is tracked rather than just a boolean. */
void record(const uint8_t* mac, int8_t rssi, Kind kind, Conf conf,
            const char* label, bool viaBle, uint32_t fp = 0) {
  portENTER_CRITICAL(&s_mux);
  Hit* h = findOrAdd(mac, viaBle, fp);
  if (h) {
    h->hits++;
    h->lastMs = millis();
    h->rssiLast = rssi;
    if (rssi > h->rssiBest) {
      h->rssiBest = rssi;
    }
    if (fp != 0) {
      h->fingerprint = fp;
    }
    if (h->kind != Kind::Unknown && h->label != label) {
      h->corroborated = true;
    }
    if ((uint8_t)conf >= (uint8_t)h->conf) {
      h->conf = conf;
      h->kind = kind;
      h->label = label;
    }
    if (h->corroborated && h->conf == Conf::Likely) {
      h->conf = Conf::Strong;
    }
    s_dirty = true;
  }
  portEXIT_CRITICAL(&s_mux);
}

/* ── WiFi ────────────────────────────────────────────────────────────────── */

void IRAM_ATTR onPacket(void* buf, wifi_promiscuous_pkt_type_t type) {
  if (!s_running || type != WIFI_PKT_MGMT) {
    return;
  }
  const wifi_promiscuous_pkt_t* pkt = (const wifi_promiscuous_pkt_t*)buf;
  const uint16_t len = pkt->rx_ctrl.sig_len;
  if (len < 24) {
    return;
  }
  const uint8_t* p = pkt->payload;
  const uint8_t subtype = p[0] & 0xF0;

  // 0x40 probe request, 0x80 beacon, 0x50 probe response.
  if (subtype != 0x40 && subtype != 0x80 && subtype != 0x50) {
    return;
  }
  s_frames++;

  const uint8_t* src = p + 10;          // addr2, the transmitter
  const int8_t rssi = pkt->rx_ctrl.rssi;

  /* The walk runs before either signature table, so that a row created by
   * one of them carries its fingerprint from the moment it exists -- which
   * is what lets findOrAdd recognise the same radio under a new address.
   *
   * Tagged parameters start after the fixed header: probe requests have none,
   * beacons and probe responses carry 12 bytes of them first.
   *
   * Everything past here is attacker-shaped. `ilen` is one byte off the air
   * and an element is free to claim more than the frame actually holds, so
   * each step is bounded against sig_len before anything is read, and the
   * arithmetic is done in uint32_t so a large offset plus a large length
   * cannot wrap back into range. A frame that lies ends the walk; it does
   * not read past the buffer.
   *
   * The walk replaces reading the SSID straight off the front. It costs one
   * pass and buys two things: SSID is found wherever it sits rather than
   * only as the first element, and a zero-length SSID no longer ends
   * processing of the frame. That second one matters for what comes next --
   * a zero-length SSID is a wildcard probe, and those carry the full element
   * set that fingerprinting will want. See docs/pueo/ie-fingerprinting.md.
   *
   * The same pass builds the fingerprint. Which bytes go into it is the
   * whole question, and the answer is in docs/pueo/ie-fingerprinting.md:
   * every element's id, because presence and order are decided by the
   * chipset and driver; contents only where they are a property of the
   * device rather than of this particular probe. */
  const uint16_t ieStart = (subtype == 0x40) ? 24 : 36;

  const uint8_t* ssidVal = nullptr;
  uint8_t        ssidLen = 0;

  uint32_t fp     = kFnvBasis;
  int      hashed = 0;

  uint32_t off = ieStart;
  for (int seen = 0; seen < kMaxIes; seen++) {
    if (off + 2u > len) {
      break;                            // no room for an id and a length
    }
    const uint8_t  id   = p[off];
    const uint8_t  ilen = p[off + 1];
    if (off + 2u + ilen > len) {
      break;                            // claims more than the frame holds
    }
    const uint8_t* val = p + off + 2;

    if (id == 0x00 && ssidVal == nullptr) {
      ssidVal = val;
      ssidLen = ilen;
    }

    /* The id always goes in: that an element is present, and where in the
     * order it sits, is signal on its own. */
    fp = fnv1a(fp, id);
    hashed++;

    switch (id) {
      case 0x00:   // SSID -- the one field that varies between probes from
      case 0x03:   // one device, and DS Param, which is the current channel
        break;     // and Spotter hops channels every 260 ms

      case 0xDD: { // vendor specific: whose element it is, not what it says.
        const uint8_t n = (ilen < 4) ? ilen : 4;   // OUI plus vendor type
        for (uint8_t k = 0; k < n; k++) {
          fp = fnv1a(fp, val[k]);
        }
        break;
      }

      case 0xFF:   // element extension: the extension id identifies it
        if (ilen >= 1) {
          fp = fnv1a(fp, val[0]);
        }
        break;

      case 0x01:   // supported rates
      case 0x2D:   // HT capabilities
      case 0x32:   // extended supported rates
      case 0x7F:   // extended capabilities
      case 0xBF:   // VHT capabilities
        for (uint8_t k = 0; k < ilen; k++) {
          fp = fnv1a(fp, val[k]);
        }
        break;

      default:
        break;     // id alone
    }

    off += 2u + ilen;
  }

  /* 0 means "no fingerprint", so a real hash that lands on it is nudged.
   * One value in four billion, and cheaper than carrying a valid flag. */
  if (hashed == 0) {
    fp = 0;
  } else if (fp == 0) {
    fp = 1;
  }

  /* Before the signature tables, and regardless of them: a fingerprint is
   * only worth capturing while it is still unknown. */
  captureNote(src, rssi, fp, ssidVal, ssidLen);

/* The whole address, checked before the OUI table. Most specific first, as
 * everywhere else here, and record() would keep the stronger label either
 * way -- but a reader of this function should see them in the order that
 * explains them. */
  for (size_t i = 0; i < kMacSigCount; i++) {
    if (memcmp(src, kMacSigs[i].mac, 6) == 0) {
      record(src, rssi, kMacSigs[i].kind, kMacSigs[i].conf, kMacSigs[i].label,
             false, fp);
      break;
    }
  }

  for (size_t i = 0; i < kOuiSigCount; i++) {
    if (memcmp(src, kOuiSigs[i].oui, 3) == 0) {
      record(src, rssi, kOuiSigs[i].kind, kOuiSigs[i].conf, kOuiSigs[i].label,
             false, fp);
      break;
    }
  }

  if (ssidVal != nullptr && ssidLen > 0 && ssidLen <= 32) {
    char ssid[33];
    memcpy(ssid, ssidVal, ssidLen);
    ssid[ssidLen] = '\0';

    for (size_t i = 0; i < kSsidSigCount; i++) {
      if (nameMatch(ssid, kSsidSigs[i])) {
        record(src, rssi, kSsidSigs[i].kind, kSsidSigs[i].conf, kSsidSigs[i].label,
               false, fp);
        break;
      }
    }

    /* And the substring table, which the prefix table above cannot express.
     * Second on purpose: an anchored match is the better answer when both
     * would fire, and it has already had its turn. */
    for (size_t i = 0; i < kNameInSigCount; i++) {
      if (nameContains(ssid, kNameInSigs[i])) {
        record(src, rssi, kNameInSigs[i].kind, kNameInSigs[i].conf,
               kNameInSigs[i].label, false, fp);
        break;
      }
    }
  }
}

/* ── BLE ─────────────────────────────────────────────────────────────────── */

class SpotterAdvCallbacks : public BLEAdvertisedDeviceCallbacks {
  void onResult(BLEAdvertisedDevice* dev) override {
    if (!s_running || !dev) {
      return;
    }

    uint8_t mac[6] = {0};
    const std::string addr = dev->getAddress().toString();
    unsigned b[6] = {0};
    if (sscanf(addr.c_str(), "%02x:%02x:%02x:%02x:%02x:%02x",
               &b[0], &b[1], &b[2], &b[3], &b[4], &b[5]) != 6) {
      return;
    }
    for (int i = 0; i < 6; i++) {
      mac[i] = (uint8_t)b[i];
    }
    const int8_t rssi = (int8_t)dev->getRSSI();

    /* An OUI is only meaningful on a public address. A random address's top
     * two bits encode the address type, not a vendor block, so matching one
     * against the OUI table reads noise as a name. That is not a theoretical
     * worry: 00:25:DF has top two bits 00, which is exactly the shape of a
     * non-resolvable private address, so the table's strongest entry is also
     * the one a randomised address could wear. The odds are 1 in 16.7M per
     * rotation and the gate costs one comparison.
     *
     * Strong only. The Strong OUIs are the blocks IEEE assigned to the
     * vendors themselves; the rest of the table is contract manufacturers
     * and module vendors, and Espressif's block against a public BLE address
     * would flag every dev board in range, including -- given a scan that
     * heard its own radio -- this one. Filtering on Conf rather than keeping
     * a second table means a vendor-own block added to SpotterSignatures.h
     * applies here too, which is what that file's header promises.
     *
     * The WiFi path passes an element fingerprint to record(); there is no
     * BLE equivalent, and findOrAdd's viaBle flag already keeps a hit found
     * this way apart from a WiFi hit for the same vendor. */
    if (dev->getAddress().getType() == BLE_ADDR_PUBLIC) {
      for (size_t i = 0; i < kOuiSigCount; i++) {
        if (kOuiSigs[i].conf != Conf::Strong) {
          continue;
        }
        if (memcmp(mac, kOuiSigs[i].oui, 3) == 0) {
          record(mac, rssi, kOuiSigs[i].kind, kOuiSigs[i].conf,
                 kOuiSigs[i].label, true);
          break;
        }
      }
    }

    uint16_t company = 0;
    if (dev->haveManufacturerData()) {
      const std::string md = dev->getManufacturerData();
      if (md.size() >= 2) {
        // Company ID is little-endian in the first two bytes.
        company = (uint16_t)((uint8_t)md[0] | ((uint8_t)md[1] << 8));

        /* The bytes past the ID, which is where an AirTag lives. kBleSigs
         * can say "company 0x004C" and that is every Apple device in range;
         * the Find My type byte is what makes it a tracker. */
        const uint8_t* raw = (const uint8_t*)md.data();
        for (size_t i = 0; i < kMfgSigCount; i++) {
          if (kMfgSigs[i].company != company) {
            continue;
          }
          if (mfgMatch(raw, md.size(), kMfgSigs[i])) {
            record(mac, rssi, kMfgSigs[i].kind, kMfgSigs[i].conf,
                   kMfgSigs[i].label, true);
            break;
          }
        }
      }
    }

    /* Service data, before the bare-service table below. More specific
     * first, as everywhere else here: 0xFEAA with a 0x40 frame type is
     * Google's tracker network, and 0xFEAA on its own is a shop beacon.
     *
     * Both can fire for one advertisement. That is harmless -- record()
     * keeps the higher confidence, so the Strong tracker label wins over the
     * Weak beacon guess -- and it is why the Eddystone row did not need
     * removing. It still says what it always said about everything else on
     * that service. */
    if (dev->haveServiceData()) {
      const uint8_t nsd = dev->getServiceDataCount();
      for (uint8_t d = 0; d < nsd; d++) {
        const NimBLEUUID su = dev->getServiceDataUUID(d);
        if (su.bitSize() != 16) {
          continue;
        }
        const uint16_t svc = (uint16_t)su.getNative()->u16.value;
        const std::string sd = dev->getServiceData(d);
        for (size_t i = 0; i < kSvcDataSigCount; i++) {
          if (kSvcDataSigs[i].service != svc) {
            continue;
          }
          if (svcDataMatch((const uint8_t*)sd.data(), sd.size(),
                           kSvcDataSigs[i])) {
            record(mac, rssi, kSvcDataSigs[i].kind, kSvcDataSigs[i].conf,
                   kSvcDataSigs[i].label, true);
            break;
          }
        }
      }
    }

    // Collect advertised 16-bit service UUIDs once, and check the 128-bit
    // ones as we go. A 128-bit service is somebody's own protocol rather
    // than an allocation out of the shared SIG space, so a hit is worth more
    // and there is no reason to collect them for a second pass.
    uint16_t services[8];
    uint8_t nServices = 0;
    const uint8_t count = dev->getServiceUUIDCount();
    for (uint8_t i = 0; i < count; i++) {
      const NimBLEUUID u = dev->getServiceUUID(i);
      if (u.bitSize() == 16) {
        if (nServices < 8) {
          services[nServices++] = (uint16_t)u.getNative()->u16.value;
        }
        continue;
      }
      if (u.bitSize() != 128) {
        continue;
      }
      for (size_t k = 0; k < kBle128SigCount; k++) {
        if (memcmp(u.getNative()->u128.value, kBle128Sigs[k].uuid, 16) == 0) {
          record(mac, rssi, kBle128Sigs[k].kind, kBle128Sigs[k].conf,
                 kBle128Sigs[k].label, true);
          break;
        }
      }
    }

    for (size_t i = 0; i < kBleSigCount; i++) {
      const BleSig& sig = kBleSigs[i];
      const bool companyOk = (sig.company == 0) || (company == sig.company);
      bool serviceOk = (sig.service == 0);
      for (uint8_t j = 0; j < nServices && !serviceOk; j++) {
        serviceOk = (services[j] == sig.service);
      }
      // A signature naming neither field would match everything.
      if (sig.company == 0 && sig.service == 0) {
        continue;
      }
      if (companyOk && serviceOk) {
        record(mac, rssi, sig.kind, sig.conf, sig.label, true);
        break;
      }
    }

    const std::string name = dev->getName();
    if (!name.empty()) {
      for (size_t i = 0; i < kBleNameSigCount; i++) {
        if (nameMatch(name.c_str(), kBleNameSigs[i])) {
          record(mac, rssi, kBleNameSigs[i].kind, kBleNameSigs[i].conf,
                 kBleNameSigs[i].label, true);
          break;
        }
      }

      for (size_t i = 0; i < kNameInSigCount; i++) {
        if (nameContains(name.c_str(), kNameInSigs[i])) {
          record(mac, rssi, kNameInSigs[i].kind, kNameInSigs[i].conf,
                 kNameInSigs[i].label, true);
          break;
        }
      }
    }
  }
};

SpotterAdvCallbacks s_advCb;
BLEScan* s_scan = nullptr;

/* ── UI ──────────────────────────────────────────────────────────────────── */

int contentBottom() {
  return featureHasTouchNavBar() ? (int)touchNavContentBottomY() : PUEO_SCREEN_H;
}

const char* kindText(Kind k) {
  switch (k) {
    case Kind::Alpr:      return "ALPR";
    case Kind::Glasses:   return "GLASSES";
    case Kind::Bodycam:   return "BODYCAM";
    case Kind::Accessory: return "ACCESSORY";
    case Kind::Vehicle:   return "VEHICLE";
    case Kind::Camera:    return "CAMERA";
    case Kind::Pentest:   return "PENTEST";
    case Kind::Tracker:   return "TRACKER";
    case Kind::Mesh:      return "MESH";
    case Kind::Acoustic:  return "ACOUSTIC";
    default:              return "?";
  }
}

/* ── The filter ───────────────────────────────────────────────────────────
 *
 * Display only. Nothing here changes what is detected, counted or written to
 * the card: a filter that edits the capture makes the capture depend on what
 * somebody had selected while it ran, and that is not readable later.
 *
 * Per session. s_kindMask and s_minConf are reset by setup() every time the
 * screen opens, because the way a remembered filter fails is silent -- you
 * conclude a street is clean when what happened is that Strong-only was left
 * on weeks ago. The header says so whenever the filter is not showing
 * everything, which is the other half of the same guard.
 *
 * One bit per Kind. Kind::Unknown is bit 0 and is never set by a signature,
 * so it costs a bit and saves an offset. */
constexpr uint16_t kAllKinds = 0x07FF;     // eleven Kind values, bits 0..10
uint16_t s_kindMask = kAllKinds;
uint8_t  s_minConf  = 0;                   // 0 Weak and up, 1 Likely and up, 2 Strong
bool     s_filtOpen = false;
int      s_filtSel  = 0;                   // 0..9 kinds, 10 the confidence row

/* The kinds the filter offers, in the order they are drawn. Kind::Unknown is
 * not here: nothing in SpotterSignatures.h produces it, so a row for it
 * would be a control that does nothing. */
const Kind kFiltKinds[] = {
  Kind::Alpr, Kind::Bodycam, Kind::Camera, Kind::Acoustic, Kind::Tracker,
  Kind::Glasses, Kind::Vehicle, Kind::Accessory, Kind::Pentest, Kind::Mesh,
};
constexpr int kFiltKindCount = (int)(sizeof(kFiltKinds) / sizeof(kFiltKinds[0]));
constexpr int kFiltRows = kFiltKindCount + 1;   // + the confidence row

bool filterIsAll() {
  return s_kindMask == kAllKinds && s_minConf == 0;
}

/* Whether a row is drawn. The only thing the filter does. */
bool shown(const Hit& h) {
  if ((uint8_t)h.conf < s_minConf) {
    return false;
  }
  return (s_kindMask & (uint16_t)(1u << (uint8_t)h.kind)) != 0;
}

int shownCount() {
  int n = 0;
  for (int i = 0; i < s_hitCount; i++) {
    if (shown(s_hits[i])) n++;
  }
  return n;
}

/* The nth row that passes the filter, or null. A linear walk over at most 48
 * entries, run once per drawn row, which is nothing next to the SPI write
 * that follows it. */
const Hit* nthShown(int n) {
  for (int i = 0; i < s_hitCount; i++) {
    if (shown(s_hits[i]) && n-- == 0) {
      return &s_hits[i];
    }
  }
  return nullptr;
}

const char* confText(uint8_t c) {
  switch (c) {
    case 2:  return "Strong only";
    case 1:  return "Likely and up";
    default: return "everything";
  }
}

uint16_t confColour(Conf c) {
  switch (c) {
    case Conf::Strong: return TFT_RED;
    case Conf::Likely: return ORANGE;
    default:           return TFT_DARKGREY;
  }
}

/* ── why the list is painted a line at a time ─────────────────────────────
 *
 * drawList() used to clear the whole list area and redraw every row, four
 * times a second. With nothing in range that is invisible: the area is black
 * and stays black. With a steady stream of hits it is a black flash behind
 * every row, four times a second, which is what the bench transmitter made
 * obvious the first time it was pointed at this screen.
 *
 * Hunt had the same fault and the same fix: remember what each line says and
 * repaint only the lines whose text changed. Most of them do not change most
 * of the time -- a MAC never does, a label never does, and the counters move
 * far more slowly than the redraw does. */
constexpr int kMaxVisRows = 16;
char s_shownRow[kMaxVisRows][3][48];
char s_shownStar[kMaxVisRows];
char s_shownHdr[48];
char s_shownTag[28];   // REC, rows, and a drop count when there is one

void forgetDrawn() {
  memset(s_shownRow, 0, sizeof(s_shownRow));
  memset(s_shownStar, 0, sizeof(s_shownStar));
  memset(s_shownHdr, 0, sizeof(s_shownHdr));
  memset(s_shownTag, 0, sizeof(s_shownTag));
}


/* In range long enough to be worth mentioning, and still here. */
bool dwelling(const Hit& h) {
  const uint32_t now = millis();
  if ((uint32_t)(now - h.lastMs) > kDwellStaleMs) {
    return false;
  }
  return (uint32_t)(h.lastMs - h.firstMs) >= kDwellAlarmMs;
}

int dwellCount() {
  int n = 0;
  for (int i = 0; i < s_hitCount; i++) {
    if (dwelling(s_hits[i])) n++;
  }
  return n;
}

/* ── the dwell alert ──────────────────────────────────────────────────────
 *
 * Shown once, when the first row crosses the threshold, and then gone. Not
 * a banner that stays: this is a passive monitoring screen and the thing
 * covering the list would be the thing you wanted to read.
 *
 * It fades rather than cutting, which costs almost nothing -- drawBitmap
 * paints only the set bits, so redrawing the same mark in a dimmer colour
 * walks it down to the background without touching anything else. */
constexpr uint32_t kAlertHoldMs = 2200;
constexpr uint16_t kBeepMs      = 140;
constexpr uint16_t kBeepGapMs   = 70;
constexpr int      kAlertSteps  = 6;
constexpr uint32_t kAlertStepMs = 90;

bool s_dwellAlerted = false;

/* RGB565 blend, t = 0 keeps `a`, t = 255 reaches `b`. */
uint16_t blend565(uint16_t a, uint16_t b, uint8_t t) {
  const int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  const int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  const int r = ar + ((br - ar) * t) / 255;
  const int g = ag + ((bg - ag) * t) / 255;
  const int bl = ab + ((bb - ab) * t) / 255;
  return (uint16_t)((r << 11) | (g << 5) | bl);
}

void dwellAlert(const Hit& h) {
  const int mw = 200, mh = 200;
  const int x = (PUEO_SCREEN_W - mw) / 2;
  /* Where the whole block sits, rather than where the mark sits.
   *
   * It used to centre the mark and let the four text lines fall below it,
   * which left the bottom of the screen empty and read as a screen that had
   * run out of things to say. This positions mark-plus-text together and
   * gives two thirds of the leftover height to the top margin and one third
   * to the bottom, so the block sits low and deliberate.
   *
   * Derived rather than nudged, because a fixed offset that looks right on
   * the 3.5" runs off the bottom of the 2.8": the block is 280 px tall and
   * that panel is 320, so it has 40 px of slack in total against the 3.5"'s
   * 200. The ratio degrades to a sensible place on both.
   *
   * tools/render_screens.py replays these coordinates and carries the same
   * arithmetic. Change one and the pictures on the website stop matching the
   * screen. */
  const int kBlockH = mh + 80;   /* mark, then four lines of font 2 */
  const int y = (PUEO_SCREEN_H - kBlockH) * 2 / 3;

  tft.fillScreen(TFT_BLACK);
  tft.setTextFont(2);
  tft.setTextDatum(TC_DATUM);
  const int cx = PUEO_SCREEN_W / 2;

  char sub[48];
  const uint32_t mins = (h.lastMs - h.firstMs) / 60000u;
  snprintf(sub, sizeof(sub), "%s  %lu min", h.label ? h.label : "device",
           (unsigned long)mins);

  /* Hunt only lists BLE trackers -- Find My and the tracker service UUIDs.
   * A dwelling plate reader or body camera will not appear in its picker,
   * so sending someone there for one would be sending them to an empty
   * screen and calling it advice. The suggestion is shown when it is
   * actionable and withheld when it is not.
   *
   * "may be" is doing real work in that line. Pueo has no position of its
   * own -- not the firmware, the device: there is no GNSS fix behind this
   * screen and no dead reckoning, so it cannot tell a tracker moving with
   * you from a beacon you
   * have been sitting next to. It reports duration and offers the tool that
   * would settle it. It does not decide. */
  const bool huntable = (h.viaBle && h.kind == Kind::Tracker);
  const char* hint1 = huntable ? "may be travelling with you"
                               : "in range this whole time";
  const char* hint2 = huntable ? "Hunt can walk you to it" : nullptr;

  for (int step = 0; step <= kAlertSteps; step++) {
    const uint8_t t = (uint8_t)((255 * step) / kAlertSteps);
    const uint16_t markC = blend565(UI_WARN, TFT_BLACK, t);
    const uint16_t textC = blend565(TFT_WHITE, TFT_BLACK, t);
    const uint16_t dimC  = blend565(UI_DIM_TEXT, TFT_BLACK, t);

    tft.drawBitmap(x, y, bitmap_pueo_dwell, mw, mh, markC);
    tft.setTextColor(markC, TFT_BLACK);
    tft.drawString("DWELL", cx, y + mh + 6);
    tft.setTextColor(textC, TFT_BLACK);
    tft.drawString(sub, cx, y + mh + 26);
    tft.setTextColor(dimC, TFT_BLACK);
    tft.drawString(hint1, cx, y + mh + 46);
    if (hint2 != nullptr) {
      tft.setTextColor(huntable ? markC : dimC, TFT_BLACK);
      tft.drawString(hint2, cx, y + mh + 64);
    }

    if (step == 0) {
      /* Two rising notes, while the mark is up rather than before it.
       *
       * This is the alert you are specifically not looking at the screen
       * for -- the device is in a bag or face down on a table, and a
       * silent alarm on a passive monitor is no alarm at all. Silent on
       * the 2.8", which has no amplifier, and silent on a 3.5" with
       * nothing plugged into its speaker connector, which is how they
       * ship. uiBeep compiles to nothing on the former and does nothing
       * audible on the latter.
       *
       * The beeps are inside the hold rather than added to it, so the
       * alert still takes kAlertHoldMs whether or not it makes a sound. */
      uiBeep(1800, kBeepMs);
      delay(kBeepGapMs);
      uiBeep(2600, kBeepMs);
      const uint32_t spent = 2u * kBeepMs + kBeepGapMs;
      if (kAlertHoldMs > spent) {
        delay(kAlertHoldMs - spent);
      }
    } else {
      delay(kAlertStepMs);
    }
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
}

void drawHeader() {
  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);

  /* 42 was enough for the old line and one short of the dwell one --
   * gcc's -Wformat-truncation caught it, not the screen, because every
   * real value here is two or three digits wide. */
  char buf[72];
  const int dwell = dwellCount();

  /* "hits 13" becomes "hits 4/13" while a filter is on, and the second
   * number is the one that has not changed. That is the whole guard against
   * the failure this feature could introduce: a filtered list looks exactly
   * like a quiet street, and the only difference is here. */
  char hits[20];
  if (filterIsAll()) {
    snprintf(hits, sizeof(hits), "%d", s_hitCount);
  } else {
    snprintf(hits, sizeof(hits), "%d/%d", shownCount(), s_hitCount);
  }

  if (dwell > 0) {
    snprintf(buf, sizeof(buf), "ch %2u  frames %lu  hits %s  dwell %d",
             (unsigned)s_chan, (unsigned long)s_frames, hits, dwell);
  } else {
    snprintf(buf, sizeof(buf), "ch %2u  frames %lu  hits %s",
             (unsigned)s_chan, (unsigned long)s_frames, hits);
  }
  /* The whole 18 px band is this line's, so clearing it here also clears
   * the recording tag, which is why the tag is repainted unconditionally
   * whenever the line above it changed. */
  const bool hdrChanged = strncmp(s_shownHdr, buf, sizeof(s_shownHdr) - 1) != 0;
  /* 18 on both panels: it is the band that gets cleared, and font 2 is
   * 16 tall, so the one number covers the taller glyph without shrinking
   * the strip the 2.8" has always wiped. */
  uiShowLine(s_shownHdr, sizeof(s_shownHdr), buf, 8, 24, 18,
             TFT_WHITE, TFT_BLACK);

  /* 28 is what two %lu plus "REC  !" can need in the worst case, which is
   * what keeps -Wformat-truncation quiet without the number being capped. */
  char tag[28] = "";
  uint16_t tagColour = TFT_RED;
  if (s_logging) {
    /* The drop count appears only when it is not zero, and turns the tag
     * orange when it does. A capture that is quietly incomplete is worse than
     * one that says so: the ring fills when the card cannot keep up, and
     * until now the only evidence was rows that did not add up to what the
     * radio saw. */
    if (s_capDropped) {
      snprintf(tag, sizeof(tag), "REC %lu !%lu",
               (unsigned long)s_logRows, (unsigned long)s_capDropped);
      tagColour = ORANGE;
    } else {
      snprintf(tag, sizeof(tag), "REC %lu", (unsigned long)s_logRows);
    }
  } else if (s_logBlocked) {
    snprintf(tag, sizeof(tag), "log off");
    tagColour = ORANGE;
  } else if (s_logFailed) {
    snprintf(tag, sizeof(tag), "no SD");
    tagColour = ORANGE;
  }
  if (hdrChanged || strncmp(s_shownTag, tag, sizeof(s_shownTag) - 1) != 0) {
    if (tag[0] != '\0') {
      /* Right-aligned rather than at a column chosen per tag.
       *
       * The three tags were at 180, 180 and 196, picked so each cleared the
       * header in font 1. Font 2 is wider: the header runs to x = 244 at its
       * longest, and "REC 41" at 180 was printed straight through the hit
       * count. Numbers that were right for one font and three strings are
       * three chances to be wrong for the next one.
       *
       * Right-aligning is not a complete fix and should not be read as one.
       * A long header and a long tag can still meet in the middle -- on the
       * 2.8" they already could, before any of this. It removes the
       * per-tag guesswork and buys the widest gap the panel has. */
      tft.setTextDatum(TR_DATUM);
      tft.setTextColor(tagColour, TFT_BLACK);
      tft.drawString(tag, PUEO_SCREEN_W - 4, 24);
      tft.setTextDatum(TL_DATUM);
    }
    snprintf(s_shownTag, sizeof(s_shownTag), "%s", tag);
  }
}

/* ── The filter screen ──────────────────────────────────────────────────── */
/* The filter borrows the list's cache rather than keeping its own.
 *
 * It needs fourteen lines -- ten kinds, the confidence row, a title and two
 * of footer -- and a 14 x 44 array of its own costs 616 bytes of DRAM, which
 * this image does not have: adding one overflowed .dram0.bss by 456 bytes and
 * the link failed. DRAM is the scarce thing here and has been since the
 * WebServer had to go on the heap.
 *
 * Borrowing is safe because the two screens are mutually exclusive -- one of
 * them is drawing or the other is, never both -- and because clearBody()
 * wipes the cache on every transition between them, so neither can ever read
 * a line the other wrote. s_shownRow is 16 rows of 3, which is 48 slots for
 * the 14 this wants.
 *
 * FILT(n) is a slot, not a row: the filter does not care about the row/column
 * shape the list imposes on that array. */
#define FILT(n) s_shownRow[(n) / 3][(n) % 3]

/* Clear the band the filter and the list share, and forget both caches.
 * Called on the way in and on the way out, because the two screens are
 * different heights: the filter draws past the end of a short list, so
 * returning to the list repaints its rows and leaves the filter's lower
 * ones underneath. A cache reset fixes what will be drawn and not what is
 * already on the glass. */
void clearBody() {
  const int top = 42;
  tft.fillRect(0, top - 18, PUEO_SCREEN_W, contentBottom() - (top - 18),
               TFT_BLACK);
  forgetDrawn();
}

void drawFilter() {
  const int top = 42;
  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);

  int n = 0;
  uiShowLine(FILT(n), sizeof(FILT(n)),
             "Filter  (display only)", 8, top - 16, PUEO_BODY_H,
             ORANGE, TFT_BLACK);
  n++;

  int y = top;
  for (int i = 0; i < kFiltKindCount; i++, n++) {
    const bool on = (s_kindMask & (uint16_t)(1u << (uint8_t)kFiltKinds[i])) != 0;
    const bool sel = (i == s_filtSel);
    char line[44];
    /* The marker and the box are in the string, not only in the colour.
     * uiShowLine compares text and nothing else, so a row that changed
     * state while its text stayed the same would keep the pixels it had. */
    snprintf(line, sizeof(line), "%s %s  %s", sel ? ">" : " ",
             on ? "[x]" : "[ ]", kindText(kFiltKinds[i]));
    uiShowLine(FILT(n), sizeof(FILT(n)), line, 8, y,
               PUEO_BODY_H, sel ? ORANGE : (on ? TFT_WHITE : TFT_DARKGREY),
               TFT_BLACK);
    y += PUEO_BODY_H + 4;
  }

  y += 6;
  const bool sel = (s_filtSel == kFiltKindCount);
  char line[44];
  snprintf(line, sizeof(line), "%s Confidence: %s", sel ? ">" : " ",
           confText(s_minConf));
  uiShowLine(FILT(n), sizeof(FILT(n)), line, 8, y,
             PUEO_BODY_H, sel ? ORANGE : TFT_WHITE, TFT_BLACK);
  n++;

  y += PUEO_BODY_H + 10;
  uiShowLine(FILT(n), sizeof(FILT(n)),
             "Nothing is hidden from the log.", 8, y, PUEO_BODY_H,
             TFT_DARKGREY, TFT_BLACK);
  n++;
  uiShowLine(FILT(n), sizeof(FILT(n)),
             "Everything is still recorded.", 8, y + PUEO_BODY_H + 2,
             PUEO_BODY_H, TFT_DARKGREY, TFT_BLACK);
}

/* Entering and leaving are their own functions because of rule 4 in
 * check_nav_labels.py: nothing may clear the screen between a nav-bar
 * repaint and the end of the function that did it. spotterLoop ends in a
 * redraw() on the dwell path, so the repaint cannot live there. */
void enterFilter() {
  s_filtOpen = true;
  s_filtSel = 0;
  clearBody();
  setTouchNavLabels("Back", "Down", "Exit", "Up", "Toggle");
  redrawTouchButtonBar();
  s_dirty = true;
  delay(160);
  waitForButtonRelease(BTN_LEFT);
}

void leaveFilter() {
  s_filtOpen = false;
  s_scroll = 0;
  clearBody();
  setTouchNavLabels("Filter", "Down", "Exit", "Up", "Log");
  redrawTouchButtonBar();
  s_dirty = true;
  delay(160);
  waitForButtonRelease(BTN_LEFT);
}

void filterButtons() {
  if (isButtonPressed(BTN_UP)) {
    s_filtSel = (s_filtSel + kFiltRows - 1) % kFiltRows;
    s_dirty = true;
    delay(140);
  } else if (isButtonPressed(BTN_DOWN)) {
    s_filtSel = (s_filtSel + 1) % kFiltRows;
    s_dirty = true;
    delay(140);
  } else if (isButtonPressed(BTN_RIGHT)) {
    if (s_filtSel == kFiltKindCount) {
      s_minConf = (uint8_t)((s_minConf + 1) % 3);
    } else {
      s_kindMask ^= (uint16_t)(1u << (uint8_t)kFiltKinds[s_filtSel]);
    }
    s_dirty = true;
    delay(160);
    waitForButtonRelease(BTN_RIGHT);
  } else if (isButtonPressed(BTN_LEFT)) {
    leaveFilter();
  }
}

void drawList() {
  const int top = 42;
  const int bottom = contentBottom();
  const int rows = (bottom - top) / kRowH;

  tft.setTextFont(PUEO_BODY_FONT);
  tft.setTextSize(1);

  const int visible = shownCount();

  if (visible == 0) {
    uiShowLine(s_shownRow[0][0], sizeof(s_shownRow[0][0]), "listening...",
           8, top + 6, PUEO_BODY_H, TFT_DARKGREY, TFT_BLACK);
    uiShowLine(s_shownRow[0][1], sizeof(s_shownRow[0][1]),
           (s_hitCount == 0) ? "nothing matched yet"
                             : "everything here is filtered out",
           8, top + 6 + kLine2, PUEO_BODY_H, TFT_DARKGREY, TFT_BLACK);
    return;
  }

  if (s_scroll > visible - rows) {
    s_scroll = visible - rows;
  }
  if (s_scroll < 0) {
    s_scroll = 0;
  }

  /* The list scrolled, so row 3 is now a different device and every cached
   * line is about the wrong one. Nothing else invalidates the whole list. */
  static int s_lastScroll = -1;
  const bool moved = (s_scroll != s_lastScroll);
  s_lastScroll = s_scroll;
  if (moved) {
    forgetDrawn();
  }

  int i = 0;
  for (; i < rows && i < kMaxVisRows && (s_scroll + i) < visible; i++) {
    const Hit* hp = nthShown(s_scroll + i);
    if (hp == nullptr) {
      break;
    }
    const Hit& h = *hp;
    const int y = top + i * kRowH;

    char line[48];
    snprintf(line, sizeof(line), "%-9s %s", kindText(h.kind), h.label);
    uiShowLine(s_shownRow[i][0], sizeof(s_shownRow[i][0]), line,
               8, y, PUEO_BODY_H, confColour(h.conf), TFT_BLACK);

    snprintf(line, sizeof(line), "%02X:%02X:%02X:%02X:%02X:%02X %s %ddBm x%u",
             h.mac[0], h.mac[1], h.mac[2], h.mac[3], h.mac[4], h.mac[5],
             h.viaBle ? "BLE" : "WiFi", (int)h.rssiBest, (unsigned)h.hits);
    uiShowLine(s_shownRow[i][1], sizeof(s_shownRow[i][1]), line,
           8, y + kLine2, PUEO_BODY_H, TFT_LIGHTGREY, TFT_BLACK);

    /* Third line: what is true of the device rather than of its address.
     *
     * The fingerprint is here to be written down -- it is what a signature
     * table gets built out of, and there is nothing to match it against yet.
     * "rnd" means the address is locally administered, which is to say made
     * up, which is to say the OUI on the line above means nothing. "+N" is
     * how many times this row has changed address underneath us. The last
     * field is how long it has been in range, because a handset walks past
     * and a camera is bolted to a pole. */
    {
      const uint32_t secs = (h.lastMs - h.firstMs) / 1000u;
      char age[10];
      if (secs < 60u) {
        snprintf(age, sizeof(age), "%lus", (unsigned long)secs);
      } else if (secs < 3600u) {
        snprintf(age, sizeof(age), "%lum", (unsigned long)(secs / 60u));
      } else {
        snprintf(age, sizeof(age), "%luh", (unsigned long)(secs / 3600u));
      }

      char fpTxt[14] = "";
      if (h.fingerprint != 0) {
        snprintf(fpTxt, sizeof(fpTxt), "fp %08lX ", (unsigned long)h.fingerprint);
      }

      char rot[10] = "";
      if (h.addrChanges > 0) {
        snprintf(rot, sizeof(rot), "+%u ", (unsigned)h.addrChanges);
      }

      /* DWELL goes in the text, not only in the colour.
       *
       * uiShowLine repaints when the string changes and compares nothing
       * else, so a row that crossed the threshold while its text stayed the
       * same would keep the old colour until something else moved. The
       * marker changes the string, which is what makes the repaint happen.
       * The age is in that string and ticks, so in practice it would repaint
       * anyway -- but relying on that would be relying on a coincidence. */
      const bool dwell = dwelling(h);
      snprintf(line, sizeof(line), "%s%s%s%s%s", fpTxt,
               (!h.viaBle && (h.mac[0] & 0x02)) ? "rnd " : "", rot,
               dwell ? "DWELL " : "", age);
      uiShowLine(s_shownRow[i][2], sizeof(s_shownRow[i][2]), line,
                 8, y + kLine3, PUEO_BODY_H,
                 dwell ? UI_WARN : TFT_DARKGREY, TFT_BLACK);
    }

    /* Drawn after the first line, which clears the band it sits in. */
    const char want = h.corroborated ? '*' : ' ';
    if (s_shownStar[i] != want) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString(h.corroborated ? "**" : "  ", PUEO_SCREEN_W - 32, y);
      s_shownStar[i] = want;
    } else if (h.corroborated) {
      tft.setTextColor(TFT_RED, TFT_BLACK);
      tft.drawString("**", PUEO_SCREEN_W - 32, y);
    }
  }

  /* Rows that existed a moment ago and do not now. Without this a list that
   * shrinks leaves the tail of the old one on screen, which no amount of
   * per-line caching would ever overwrite. */
  for (; i < rows && i < kMaxVisRows; i++) {
    if (s_shownRow[i][0][0] == '\0') {
      continue;
    }
    tft.fillRect(0, top + i * kRowH, PUEO_SCREEN_W, kRowH, TFT_BLACK);
    s_shownRow[i][0][0] = s_shownRow[i][1][0] = s_shownRow[i][2][0] = '\0';
    s_shownStar[i] = 0;
  }
}

void redraw(bool full) {
  if (full) {
    forgetDrawn();          // the screen is about to be black; the cache lies
    tft.fillScreen(TFT_BLACK);
    drawStatusBar(readBatteryVoltage(), true);
  }
  drawHeader();
  drawList();

  if (full) {
    /* fillScreen took the nav bar with it, so every caller of redraw(true)
     * needs it back. setup() knew that and did it by hand; the dwell alert
     * did not, and the first time a device dwelled long enough to alert, the
     * buttons vanished for the rest of the session. Reported from the board
     * as "the bottom menu disappeared after it was running a while".
     *
     * Putting it here rather than at the call sites means a caller cannot
     * forget, which is the only version of this that stays true as callers
     * are added. */
    redrawTouchButtonBar();
  }
}

void hopChannel() {
  s_chan++;
  if (s_chan > kChanLast) {
    s_chan = kChanFirst;
  }
  esp_wifi_set_channel(s_chan, WIFI_SECOND_CHAN_NONE);
}

}  // namespace

/* ── Feature entry points ────────────────────────────────────────────────── */

void spotterSetup() {
  showFeatureMark(bitmap_pueo_spotter, "Surveillance");

  s_hitCount = 0;
  s_frames = 0;
  s_scroll = 0;
  s_chan = kChanFirst;
  s_dirty = true;
  memset(s_hits, 0, sizeof(s_hits));

  s_logging = false;
  s_logFailed = false;
  s_logBlocked = false;
  s_dwellAlerted = false;
  s_logRows = 0;
  s_capHead = 0;
  s_capTail = 0;
  s_capDropped = 0;
  memset(s_capSeen, 0, sizeof(s_capSeen));

  setTouchButtonInputEnabled(true);
  /* The left slot used to be null, which draws the dots icon meaning the
   * button does nothing. It opens the filter now. */
  setTouchNavLabels("Filter", "Down", "Exit", "Up", "Log");

  /* Per session. See the note on s_kindMask: a filter that survives the
   * screen is one somebody forgets is on, and the way that fails is a clean
   * street that was never looked at. */
  s_kindMask = kAllKinds;
  s_minConf  = 0;
  s_filtOpen = false;
  s_filtSel  = 0;

  // Radio up in station mode, unassociated, purely to listen.
  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(60);
  esp_wifi_set_promiscuous(false);
  /* Management frames only, stated rather than inherited: the filter is
   * global and the feature that ran before this one may have set anything.
   * onPacket returns on everything else, so asking for more would only cost
   * callbacks. */
  {
    wifi_promiscuous_filter_t filt = {};
    filt.filter_mask = WIFI_PROMIS_FILTER_MASK_MGMT;
    esp_wifi_set_promiscuous_filter(&filt);
  }
  esp_wifi_set_promiscuous_rx_cb(&onPacket);
  esp_wifi_set_promiscuous(true);
  esp_wifi_set_channel(s_chan, WIFI_SECOND_CHAN_NONE);

  if (ensureBleStackReady()) {
    s_scan = BLEDevice::getScan();
    if (s_scan) {
      s_scan->setAdvertisedDeviceCallbacks(&s_advCb, true);
      s_scan->setActiveScan(false);   // passive: never ask, only listen
      /* Window under interval, on purpose.
       *
       * The ESP32 has one radio. A BLE scan whose window equals its interval
       * asks for it continuously, and the coexistence arbiter then gives the
       * WiFi side almost nothing -- which is fine for a BLE-only feature and
       * ruinous for this one, because it listens on both.
       *
       * Measured with the bench beacon, which counts what it sends: 495 ALPR
       * probe requests transmitted, and this screen found the first one after
       * about five minutes. The frames were going out; there was no receiver
       * awake to hear them. Hunt and Fast Pair keep a full window because
       * they have no WiFi side to starve.
       *
       * 100 ms interval with a 50 ms window is half the airtime each. BLE
       * sightings get rarer in exchange, which is the trade. */
      s_scan->setInterval(160);   // 160 * 0.625 ms = 100 ms
      s_scan->setWindow(80);      //  80 * 0.625 ms =  50 ms
    }
  }

  s_running = true;
  s_lastHop = millis();
  s_lastDraw = 0;
  redraw(true);

  /* Last, and after redraw(true). Storing the labels does not paint them,
   * and redraw(full) starts with fillScreen -- painting the bar before that
   * puts it on screen and then wipes it, which is how this read as "the
   * feature has no buttons" while the centre slot was quietly exiting. */
  redrawTouchButtonBar();
}

void spotterLoop() {
  const uint32_t now = millis();

  if ((uint32_t)(now - s_lastHop) >= kHopMs) {
    s_lastHop = now;
    hopChannel();
    s_dirty = true;
  }

  if (s_scan && !s_scan->isScanning()) {
    s_scan->start(kBleWindowMs / 1000, nullptr, false);
  }

  if (s_filtOpen) {
    filterButtons();
    if (s_dirty && (uint32_t)(now - s_lastDraw) >= kRedrawMs) {
      s_lastDraw = now;
      s_dirty = false;
      drawHeader();
      drawFilter();
    }
    delay(4);
    return;
  }

  if (isButtonPressed(BTN_LEFT)) {
    enterFilter();
    /* Nothing else this pass. The screen has just changed, and the dwell
     * alert below ends in redraw(), whose first act is fillScreen: it would
     * wipe the nav bar enterFilter just painted and leave the filter screen
     * with no labels on it. check_nav_labels.py is the reason this is a
     * return rather than a fall-through, and it is the fourth time that
     * check has been right about this exact sequence. */
    return;
  }

  if (isButtonPressed(BTN_UP)) {
    s_scroll--;
    s_dirty = true;
    delay(120);
  } else if (isButtonPressed(BTN_DOWN)) {
    s_scroll++;
    s_dirty = true;
    delay(120);
  } else if (isButtonPressed(BTN_RIGHT)) {
    if (s_logging) {
      captureStop();
    } else {
      captureStart();                   // sets s_logFailed when there is no card
    }
    s_dirty = true;
    delay(200);
    waitForButtonRelease(BTN_RIGHT);
  }

  captureFlush();

  /* Fire once on the edge, not every pass. Re-arms when the last dwelling
   * row goes quiet, so a device that leaves and comes back alerts again --
   * which is the case worth hearing about twice. */
  {
    const Hit* first = nullptr;
    for (int i = 0; i < s_hitCount && first == nullptr; i++) {
      if (dwelling(s_hits[i])) first = &s_hits[i];
    }
    if (first != nullptr && !s_dwellAlerted) {
      s_dwellAlerted = true;
      dwellAlert(*first);
      redraw(true);          // the alert painted over the list
      s_lastDraw = millis();
    } else if (first == nullptr) {
      s_dwellAlerted = false;
    }
  }

  if (s_dirty && (uint32_t)(now - s_lastDraw) >= kRedrawMs) {
    s_lastDraw = now;
    s_dirty = false;
    drawHeader();
    drawList();
  }

  delay(4);
}

void exit() {
  s_running = false;
  captureStop();                        // never leave a file open on the card

  esp_wifi_set_promiscuous(false);
  esp_wifi_set_promiscuous_rx_cb(nullptr);

  if (s_scan) {
    s_scan->stop();
    s_scan->setAdvertisedDeviceCallbacks(nullptr);
    s_scan->clearResults();
    s_scan = nullptr;
  }

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  requestStatusBarRedraw();
}

int        hitCount()        { return s_hitCount; }
const Hit* hitAt(int i)      { return (i >= 0 && i < s_hitCount) ? &s_hits[i] : nullptr; }
uint32_t   framesSeen()      { return s_frames; }
uint8_t    currentChannel()  { return s_chan; }
bool       captureActive()   { return s_logging; }
uint32_t   captureRows()     { return s_logRows; }
uint32_t   captureDropped()  { return s_capDropped; }

}  // namespace Spotter
