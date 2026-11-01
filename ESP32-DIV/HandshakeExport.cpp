#include "HandshakeExport.h"

#if PUEO_HANDSHAKE_EXPORT

#include <Arduino.h>
#include <WiFi.h>
#include <SD.h>
#include <string.h>

#include "shared.h"   // CAPTURE_DIR, PUEO_DIR
#include "utils.h"    // sdEnsureDir

/* Through VENDOR_INC (see build.sh); the vendored headers are not in the
 * sketch, and a relative path would not survive arduino-cli's staging copy. */
extern "C" {
#include "frame_analyzer_types.h"
#include "hccapx_serializer.h"
}

namespace HandshakeExport {

namespace {

/* Frames are copied here by the callback and read back by the task. One
 * spinlock covers the indices and the slots. Key frames are small; a QoS
 * header plus the largest key message with its GTK still sits well under this,
 * and offer() rejects anything bigger rather than truncating it. */
constexpr int      kSlots    = 6;
constexpr uint16_t kSlotSize = 400;

struct Slot {
  uint8_t  buf[kSlotSize];
  uint16_t len;
  uint8_t  msg;   // Eapol::Msg
};

Slot        s_ring[kSlots];
volatile int s_head = 0;   // next write (offer)
volatile int s_tail = 0;   // next read  (pump)
volatile uint32_t s_dropped = 0;
portMUX_TYPE s_mux = portMUX_INITIALIZER_UNLOCKED;

/* The one handshake being assembled. risinek's serializer is a single global
 * state machine, so this follows it: one access point at a time. A frame for a
 * different BSSID starts a new assembly. */
bool     s_haveTarget = false;
uint8_t  s_bssid[6]   = {0};
bool     s_written    = false;
bool     s_seenM1 = false, s_seenM2 = false, s_seenM3 = false, s_seenM4 = false;
uint8_t  s_noSsidReportedFor[6] = {0};

bool addrEq(const uint8_t* a, const uint8_t* b) { return memcmp(a, b, 6) == 0; }

/* The SSID is the salt a hashcat record is cracked against, so a record with
 * the wrong one is useless. Read it from the last scan's results by BSSID.
 * Returns the length written to out (not counting the terminator), or -1 when
 * this BSSID is not in the scan. */
int lookupSsid(const uint8_t* bssid, char* out, size_t outSz) {
  const int n = WiFi.scanComplete();
  if (n <= 0) return -1;
  for (int i = 0; i < n; i++) {
    const uint8_t* b = WiFi.BSSID(i);
    if (b != nullptr && addrEq(b, bssid)) {
      String s = WiFi.SSID(i);
      size_t len = s.length();
      if (len > outSz - 1) len = outSz - 1;
      memcpy(out, s.c_str(), len);
      out[len] = '\0';
      return (int)len;
    }
  }
  return -1;
}

bool ensureCaptureDir() {
  /* sdEnsureDir walks and creates the parents too, the same helper the rest
   * of the firmware uses for card paths. */
  return sdEnsureDir(CAPTURE_DIR);
}

bool nextHccapxPath(String& out) {
  for (uint16_t i = 0; i < 10000; i++) {
    char buf[64];
    snprintf(buf, sizeof(buf), "%s/hs_%04u.hccapx", CAPTURE_DIR, (unsigned)i);
    if (!SD.exists(buf)) { out = String(buf); return true; }
  }
  return false;
}

/* A record is worth writing when it has both nonces, a MIC and an EAPOL body.
 * The serializer leaves the AP nonce zero until it has seen M1 or M3 and the
 * STA nonce zero until M2, so this also rejects a half-built record. */
bool recordLooksComplete(const hccapx_t* h) {
  if (h == nullptr) return false;
  if (h->eapol_len == 0 || h->eapol_len > 256) return false;
  bool apNonce = false, staNonce = false, mic = false;
  for (int i = 0; i < 32; i++) { apNonce  |= h->nonce_ap[i]  != 0; staNonce |= h->nonce_sta[i] != 0; }
  for (int i = 0; i < 16; i++)   mic |= h->keymic[i] != 0;
  return apNonce && staNonce && mic;
}

void tryWrite() {
  if (s_written) return;
  const bool usable = (s_seenM2 && (s_seenM1 || s_seenM3));
  if (!usable) return;

  hccapx_t* h = hccapx_serializer_get();
  if (!recordLooksComplete(h)) return;

  /* The essid is set at init(); fill or correct it now in case the scan that
   * named this BSSID only completed after assembly began. Without it, hold
   * off and say so once, rather than writing an uncrackable record. */
  char ssid[33];
  const int sl = lookupSsid(s_bssid, ssid, sizeof(ssid));
  if (sl < 0) {
    if (!addrEq(s_noSsidReportedFor, s_bssid)) {
      memcpy(s_noSsidReportedFor, s_bssid, 6);
      Serial.printf("[hccapx] handshake for %02x:%02x:%02x:%02x:%02x:%02x is ready, "
                    "but no SSID yet, scan it and it will write\n",
                    s_bssid[0], s_bssid[1], s_bssid[2], s_bssid[3], s_bssid[4], s_bssid[5]);
    }
    return;
  }
  memset(h->essid, 0, sizeof(h->essid));
  memcpy(h->essid, ssid, (size_t)sl);
  h->essid_len = (uint8_t)sl;

  if (!ensureCaptureDir()) { Serial.println("[hccapx] no SD, not written"); return; }
  String path;
  if (!nextHccapxPath(path)) { Serial.println("[hccapx] no free filename"); return; }

  File f = SD.open(path.c_str(), FILE_WRITE);
  if (!f) { Serial.printf("[hccapx] could not open %s\n", path.c_str()); return; }
  const size_t wrote = f.write((const uint8_t*)h, sizeof(hccapx_t));
  f.close();
  if (wrote != sizeof(hccapx_t)) {
    Serial.printf("[hccapx] short write to %s (%u/%u)\n", path.c_str(),
                  (unsigned)wrote, (unsigned)sizeof(hccapx_t));
    return;
  }
  s_written = true;
  Serial.printf("[hccapx] wrote %s for \"%s\"\n", path.c_str(), ssid);
}

void startTarget(const uint8_t* bssid) {
  memcpy(s_bssid, bssid, 6);
  s_haveTarget = true;
  s_written = false;
  s_seenM1 = s_seenM2 = s_seenM3 = s_seenM4 = false;

  char ssid[33] = {0};
  const int sl = lookupSsid(bssid, ssid, sizeof(ssid));
  /* init() clears any previous record. Pass the SSID if the scan knows it; an
   * empty one is corrected in tryWrite() before anything is saved. */
  hccapx_serializer_init((const uint8_t*)ssid, (sl < 0 ? 1 : (unsigned)sl + 1));
}

void feed(const uint8_t* frame, uint16_t len, uint8_t msg) {
  const uint8_t* bssid = nullptr;
  const uint8_t* sta   = nullptr;
  if (!Eapol::addresses(frame, len, &bssid, &sta) || bssid == nullptr) return;

  if (!s_haveTarget || !addrEq(bssid, s_bssid)) {
    startTarget(bssid);
  }

  switch ((Eapol::Msg)msg) {
    case Eapol::Msg::M1: s_seenM1 = true; break;
    case Eapol::Msg::M2: s_seenM2 = true; break;
    case Eapol::Msg::M3: s_seenM3 = true; break;
    case Eapol::Msg::M4: s_seenM4 = true; break;
    default: break;
  }

  /* The frame has already passed Eapol's bounded checks in offer(), so its
   * header is 24 or 26 bytes and the EAPOL payload is within len. That is the
   * precondition risinek's unguarded parser needs, so this cast is safe. */
  hccapx_serializer_add_frame((data_frame_t*)frame);
  tryWrite();
}

}  // namespace

void offer(const uint8_t* frame, uint16_t len, Eapol::Msg m, uint32_t /*nowMs*/) {
  if (frame == nullptr || m == Eapol::Msg::None) return;

  /* Only forward layouts risinek can read: a 24-byte header, or 26 with QoS.
   * A fourth address or an HT Control field moves the payload somewhere its
   * parser does not look, so drop those here rather than let it misread. And
   * confirm, with the bounded parser, that an EAPOL payload really is present
   * and within the frame. */
  const int hdr = Eapol::headerLength(frame, len);
  if (hdr != 24 && hdr != 26) return;
  if (Eapol::findPayload(frame, len) < 0) return;
  if (len == 0 || len > kSlotSize) return;

  portENTER_CRITICAL_ISR(&s_mux);
  const int next = (s_head + 1) % kSlots;
  if (next == s_tail) {
    s_dropped++;                 // ring full; the task is behind
  } else {
    memcpy(s_ring[s_head].buf, frame, len);
    s_ring[s_head].len = len;
    s_ring[s_head].msg = (uint8_t)m;
    s_head = next;
  }
  portEXIT_CRITICAL_ISR(&s_mux);
}

void pump() {
  for (;;) {
    Slot local;
    bool have = false;
    portENTER_CRITICAL(&s_mux);
    if (s_tail != s_head) {
      memcpy(&local, &s_ring[s_tail], sizeof(Slot));
      s_tail = (s_tail + 1) % kSlots;
      have = true;
    }
    portEXIT_CRITICAL(&s_mux);
    if (!have) break;
    feed(local.buf, local.len, local.msg);
  }
}

}  // namespace HandshakeExport

#endif  // PUEO_HANDSHAKE_EXPORT
