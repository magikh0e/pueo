/* ─────────────────────────────────────────────────────────────────────────────
 * Pueo Beacon — the bench transmitter that proves Pueo's receivers work.
 *
 * Pueo's detectors are checked against synthetic frames and, where one
 * exists, a reference implementation. None of that proves a radio path: a
 * decoder can be correct while the scan never hands it a byte, and the
 * symptom is an empty list, which is also what an empty room looks like.
 *
 * This is the other half. Flash it to a second board, put the two side by
 * side, and every detector in Pueo has something to find.
 *
 *   Drones     Remote ID over Wi-Fi Beacon and BLE 4 legacy
 *   Spotter    a plate-reader probe, a body-camera beacon, smart glasses,
 *              a vehicle module
 *   Hunt       a Find My tracker advertisement
 *   Fast Pair  both payload shapes, discoverable and not
 *
 * Build and flash:
 *
 *   PUEO_ROLE=beacon tools/build.sh
 *   PUEO_ROLE=beacon tools/build.sh upload COM5
 *
 * It is a separate image from the detector on purpose. See Emit.h.
 *
 * Everything it sends is built to be recognised as a decoy rather than to be
 * convincing: PUEO-TEST identifiers, DEAD in the device half of every
 * address, transmit power at the floor, and an auto-stop. A test
 * kit you cannot tell from the real article is a worse test kit, not a
 * better one.
 * ────────────────────────────────────────────────────────────────────────── */

#include "BeaconArt.h"
#include "Emit.h"

#include <TFT_eSPI.h>

/* The detector's own headers. shared.h carries the panel geometry, the
 * backlight pin and the rotation, all of which move with PUEO_PANEL -- so
 * the two images cannot disagree about the board they are on. */
#include "Branding.h"
#include "icon.h"
#include "shared.h"
#include "UiLine.h"   // after shared.h: it needs PUEO_SCREEN_W

TFT_eSPI tft = TFT_eSPI();

namespace {

constexpr uint16_t kBg     = 0x0000;
constexpr uint16_t kText   = 0xFFFF;
constexpr uint16_t kDim    = 0x8410;
constexpr uint16_t kLive   = 0x07E0;
constexpr uint16_t kWarn   = 0xFBE0;
constexpr uint16_t kStop   = 0xF800;

/* Wi-Fi signals are events and go out every kWifiMs. BLE advertising is a
 * state, so only one BLE signal can be live at a time and they take turns
 * for kBleSliceMs each. */
constexpr uint32_t kWifiMs     = 250;
constexpr uint32_t kBleSliceMs = 3000;
constexpr uint32_t kDrawMs     = 400;

uint32_t s_started   = 0;
uint32_t s_lastWifi  = 0;
uint32_t s_lastSlice = 0;
uint32_t s_lastDraw  = 0;
bool     s_running   = true;
uint8_t  s_bleIdx    = 0;

const Emit::Signal kWifiSignals[] = {
  Emit::RemoteIdWifi, Emit::AlprProbe, Emit::BodycamBeacon,
  Emit::PentestWifi,
};
const Emit::Signal kBleSignals[] = {
  Emit::RemoteIdBle, Emit::GlassesBle, Emit::VehicleBle,
  Emit::TrackerBle, Emit::FindHubBle, Emit::DultBle,
  Emit::FleetBle, Emit::TpmsBle, Emit::CarBle, Emit::FastPairBle,
};
constexpr uint8_t kWifiCount = sizeof(kWifiSignals) / sizeof(kWifiSignals[0]);
constexpr uint8_t kBleCount  = sizeof(kBleSignals) / sizeof(kBleSignals[0]);

uint8_t s_wifiIdx = 0;

/* ── the splash ──────────────────────────────────────────────────────────
 *
 * Two boards, same case, same panel, and one of them transmits. Telling them
 * apart mattered enough already -- a detector was overwritten with this
 * firmware because nothing on screen or in the build output distinguished
 * them -- so this one says what it is before it says anything else, and says
 * it in the warning colour rather than the owl's.
 *
 * The artwork is the beacon's own: an owl calling from a mast with the arcs
 * going outward, the mirror of the Hunt mark where they come inward. See
 * BeaconArt.h. */
/* Long enough to read what the board is and pull the power if it is the
 * wrong one. The splash is the only moment before it starts transmitting,
 * so it counts down out loud rather than just sitting there. */
constexpr uint32_t kSplashMs = 5000;

void drawSplash() {
  tft.fillScreen(kBg);

  constexpr int kMarkW = 200, kMarkH = 200;
  const int lx = (PUEO_SCREEN_W - kMarkW) / 2;
  const int ly = (PUEO_SCREEN_H - kMarkH) / 2 - 40;
  tft.drawBitmap(lx, ly, bitmap_pueo_beacon, kMarkW, kMarkH, kWarn);
  int y = ly + kMarkH + 12;

  tft.setTextDatum(TC_DATUM);
  const int cx = PUEO_SCREEN_W / 2;

  tft.setTextFont(2);
  tft.setTextColor(kWarn, kBg);
  tft.drawString("BEACON", cx, y);
  y += 22;

  tft.setTextFont(1);
  tft.setTextColor(kText, kBg);
  tft.drawString("bench transmitter", cx, y);
  y += 14;
  tft.setTextColor(kDim, kBg);
  tft.drawString(PUEO_VERSION, cx, y);

  /* The one thing worth reading before it starts. */
  tft.setTextColor(kStop, kBg);
  tft.drawString("THIS BOARD TRANSMITS", cx, PUEO_SCREEN_H - 54);
  /* Built from kAutoStopMs rather than typed. It said 10 while the
   * constant said 15, which on this screen is not a typo: this is the line
   * somebody reads in the five seconds before the board starts
   * transmitting, and it was promising to stop a third of the way in. */
  char lim[40];
  snprintf(lim, sizeof(lim), "WiFi + BLE, lowest power, %lu min",
           (unsigned long)(Emit::kAutoStopMs / 60000u));
  tft.setTextColor(kDim, kBg);
  tft.drawString(lim, cx, PUEO_SCREEN_H - 40);

  /* Counts down rather than waiting quietly, so the delay reads as a
   * deliberate hold and not as a slow boot. Only the line that changes is
   * repainted -- a full clear once a second is a flash once a second, which
   * is the same fault four feature screens had. */
  const int countY = PUEO_SCREEN_H - 22;
  const uint32_t until = millis() + kSplashMs;
  int shown = -1;
  for (;;) {
    const uint32_t now = millis();
    if ((int32_t)(until - now) <= 0) {
      break;
    }
    const int left = (int)((until - now + 999u) / 1000u);
    if (left != shown) {
      shown = left;
      char msg[40];
      snprintf(msg, sizeof(msg), "broadcasting in %d...", left);
      tft.fillRect(0, countY, PUEO_SCREEN_W, 16, kBg);
      tft.setTextColor(kWarn, kBg);
      tft.drawString(msg, cx, countY);
    }
    delay(20);
  }

  tft.fillRect(0, countY, PUEO_SCREEN_W, 16, kBg);
  tft.setTextColor(kLive, kBg);
  tft.drawString("broadcasting", cx, countY);
  delay(300);

  tft.setTextDatum(TL_DATUM);
}

void drawFrame() {
  tft.fillScreen(kBg);
  tft.setTextFont(2);
  tft.setTextColor(kWarn, kBg);
  tft.drawString("PUEO BEACON", 8, 6);
  tft.setTextFont(1);
  tft.setTextColor(kDim, kBg);
  tft.drawString("bench transmitter - " PUEO_VERSION, 8, 28);
  tft.drawFastHLine(0, 42, PUEO_SCREEN_W, kDim);
}

/* One buffer per line. This screen had the same fault as the four it exists
 * to test: clear the whole area, redraw every row, four times a second. See
 * UiLine.h. */
char s_shownHead[2][48];
char s_shownName[Emit::kSignalCount][40];
char s_shownCnt[Emit::kSignalCount][16];
char s_shownBy[Emit::kSignalCount][32];

void forgetDrawn() {
  memset(s_shownHead, 0, sizeof(s_shownHead));
  memset(s_shownName, 0, sizeof(s_shownName));
  memset(s_shownCnt, 0, sizeof(s_shownCnt));
  memset(s_shownBy, 0, sizeof(s_shownBy));
}

void drawBody() {
  const int top = 50;
  tft.setTextFont(1);

  int y = top;
  if (!s_running) {
    uiShowLine(s_shownHead[0], sizeof(s_shownHead[0]),
               "STOPPED - the auto-stop ran out", 8, y, 12, kStop, kBg);
    uiShowLine(s_shownHead[1], sizeof(s_shownHead[1]),
               "reset the board to transmit again", 8, y + 12, 12, kDim, kBg);
    y += 32;
  } else {
    const uint32_t leftS = (Emit::kAutoStopMs - (millis() - s_started)) / 1000u;
    char hdr[48];
    snprintf(hdr, sizeof(hdr), "TRANSMITTING - stops in %lu:%02lu",
             (unsigned long)(leftS / 60), (unsigned long)(leftS % 60));
    uiShowLine(s_shownHead[0], sizeof(s_shownHead[0]), hdr, 8, y, 14, kLive, kBg);
    y += 16;
  }

  const Emit::Signal live = Emit::currentBle();
  for (uint8_t i = 0; i < Emit::kSignalCount; i++) {
    const Emit::Signal s = (Emit::Signal)i;
    const uint32_t n = Emit::sentCount(s);
    const bool isLiveBle = (s == live);

    /* The live marker is in the string, not only in the colour. uiShowLine
     * compares text and nothing else, so a row that went live while its name
     * stayed the same would keep the colour it had. */
    char nm[40];
    snprintf(nm, sizeof(nm), "%s%s", isLiveBle ? "> " : "  ", Emit::name(s));
    const bool nameRedrawn =
        uiShowLine(s_shownName[i], sizeof(s_shownName[i]), nm, 8, y, 12,
                   isLiveBle ? kLive : (n ? kText : kDim), kBg);

    /* Shares a band with the name, so it repaints whenever that cleared. */
    char cnt[16];
    snprintf(cnt, sizeof(cnt), "%lu", (unsigned long)n);
    if (nameRedrawn ||
        strncmp(s_shownCnt[i], cnt, sizeof(s_shownCnt[i]) - 1) != 0) {
      tft.setTextColor(kDim, kBg);
      tft.drawString(cnt, PUEO_SCREEN_W - 44, y);
      snprintf(s_shownCnt[i], sizeof(s_shownCnt[i]), "%s", cnt);
    }

    uiShowLine(s_shownBy[i], sizeof(s_shownBy[i]), Emit::detectedBy(s),
               20, y + 10, 10, kDim, kBg);
    y += 24;
  }

  /* A refused frame is the difference between "nobody is listening" and
   * "nothing is being said", and those two look identical from the other
   * board. Say which it is here. */
  static char shownFail[48];
  char warn[48] = "";
  const uint32_t fails = Emit::txFailures();
  if (fails) {
    snprintf(warn, sizeof(warn), "WiFi TX REFUSED x%lu - frames not sent",
             (unsigned long)fails);
  }
  uiShowLine(shownFail, sizeof(shownFail), warn, 8, PUEO_SCREEN_H - 26, 12,
             kStop, kBg);

  static char shownFoot[40];
  uiShowLine(shownFoot, sizeof(shownFoot), "all payloads say PUEO-TEST",
             8, PUEO_SCREEN_H - 14, 12, kDim, kBg);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(50);
  /* The version goes out here as well as onto the panel. It used to be on
   * the TFT alone, which meant the only way to know what a beacon was
   * running was to look at it: over USB it said "[beacon] start" and
   * nothing else, so a board flashed weeks ago and one flashed minutes ago
   * were indistinguishable in a log. The detector has always printed its
   * version, and the same reasoning applies harder here, because this
   * board's whole job is to be the known-good half of a bench pair. */
  Serial.println("[beacon] start, bench transmitter " PUEO_VERSION);

  pinMode(BACKLIGHT_PIN, OUTPUT);
  digitalWrite(BACKLIGHT_PIN, HIGH);

  tft.init();
  tft.setRotation(TFT_ROTATION);

  drawSplash();
  drawFrame();
  forgetDrawn();

  Emit::begin();
  s_started = millis();
  drawBody();
}

void loop() {
  const uint32_t now = millis();

  if (s_running && (now - s_started) >= Emit::kAutoStopMs) {
    /* Stops on its own. A bench unit left powered should not still be
     * shouting an hour later because nobody walked back to it. */
    Emit::allStop();
    s_running = false;
    drawBody();
  }

  if (s_running) {
    if ((uint32_t)(now - s_lastWifi) >= kWifiMs) {
      s_lastWifi = now;
      Emit::send(kWifiSignals[s_wifiIdx]);
      s_wifiIdx = (uint8_t)((s_wifiIdx + 1) % kWifiCount);
    }

    if ((uint32_t)(now - s_lastSlice) >= kBleSliceMs) {
      s_lastSlice = now;
      Emit::send(kBleSignals[s_bleIdx]);
      s_bleIdx = (uint8_t)((s_bleIdx + 1) % kBleCount);
    }
  }

  if ((uint32_t)(now - s_lastDraw) >= kDrawMs) {
    s_lastDraw = now;
    drawBody();
  }

  delay(4);
}
