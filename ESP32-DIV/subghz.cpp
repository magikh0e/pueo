#include "Stealth.h"
#include <algorithm>
#include <vector>
#include "KeyboardUI.h"
#include "Touchscreen.h"
#include "config.h"
#include "icon.h"
#include "SettingsStore.h"
#include "shared.h"
#include "SpiBus.h"
#include "SubFile.h"



namespace {
  /* One list, because there were two.
   *
   * replayat and subjammer each carried an identical copy of this array.
   * Nothing kept them in step: adding a frequency to one left the other
   * quietly disagreeing, and the two features would then be tuning to
   * different things from the same index. Same class of drift that
   * gen_netlist.py exists to stop on the pin map.
   *
   * What is deliberately NOT shared is the index into it. replayat and
   * subjammer keep their own currentFrequencyIndex, of different types and
   * with different defaults -- the jammer starts at 315 MHz, the replay at
   * 300 -- because each feature remembers where the operator left it. */
  const uint32_t subghz_frequency_list[] = {
      300000000, 303875000, 304250000, 310000000, 314000000, 315000000,
      318000000, 390000000, 418000000, 433075000, 433420000, 433920000,
      434420000, 434775000, 438900000, 868350000, 915000000, 925000000
  };
  constexpr size_t kSubghzFreqCount =
      sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]);

  /* The status line for the two file screens.
   *
   * Import and Export are the same screen pointing opposite ways and
   * cannot both be up, so they share one. That is not tidiness: DRAM on
   * this part is full enough that adding Export with its own String
   * overflowed dram0_0_seg by 16 bytes and the image would not link. */
  String subghzFileStatus;
  bool   subghzFileStatusWarn = false;

  static constexpr const char* SUBGHZ_DIR = SUBGHZ_SD_DIR;
  static constexpr const char* SUBGHZ_EXPORT_PREFIX = SUBGHZ_SD_DIR "/profiles_";
  static constexpr const char* SUBGHZ_CURRENT_PATH =
      SUBGHZ_SD_DIR "/profiles_current.bin";
  static constexpr uint32_t SUBGHZ_EXPORT_MAGIC = 0x315A4753;

  struct __attribute__((packed)) SubGhzProfile {
    uint32_t frequency;
    uint32_t value;
    uint16_t bitLength;
    uint16_t protocol;
    char     name[16];
  };

  static constexpr uint16_t MAX_NAME_LENGTH = 16;
  static constexpr uint16_t PROFILE_SIZE = sizeof(SubGhzProfile);

  static constexpr uint16_t ADDR_VALUE = 1280;
  static constexpr uint16_t ADDR_BITLEN = 1284;
  static constexpr uint16_t ADDR_PROTO = 1286;
  static constexpr uint16_t ADDR_FREQ = 1288;
  static constexpr uint16_t ADDR_PROFILE_COUNT = 1296;
  static constexpr uint16_t ADDR_PROFILE_START = 1300;
  static constexpr uint16_t MAX_PROFILES = 5;

  struct __attribute__((packed)) SubGhzExportHeader {
    uint32_t magic;
    uint16_t version;
    uint16_t count;
    uint16_t profileSize;
    uint16_t reserved;
  };

  static bool subghz_sd_mounted = false;
  static bool subghzMountSD() {
    if (subghz_sd_mounted) {
      if (SD.exists("/")) return true;
      subghz_sd_mounted = false;
    }

#if defined(CC1101_CS)
    pinMode(CC1101_CS, OUTPUT);
    digitalWrite(CC1101_CS, HIGH);
#endif

    restoreSdAfterSharedSpi();
    if (isSDCardAvailable()) {
      subghz_sd_mounted = true;
      return true;
    }
    return false;
  }

  static bool subghzEnsureDir(const char* dirPath) {
    if (!subghzMountSD()) return false;
    return sdEnsureDir(dirPath);
  }

  static void clearProfilesInEeprom() {

    uint16_t zero = 0;
    EEPROM.put(ADDR_PROFILE_COUNT, zero);
    SubGhzProfile empty{};
    for (uint16_t i = 0; i < MAX_PROFILES; i++) {
      EEPROM.put(ADDR_PROFILE_START + (i * PROFILE_SIZE), empty);
    }
    EEPROM.commit();
  }

  static bool makeNextExportPath(String& outPath) {

    for (uint16_t i = 0; i < 10000; i++) {
      char buf[48];
      snprintf(buf, sizeof(buf), "%s%04u.bin", SUBGHZ_EXPORT_PREFIX, (unsigned)i);
      if (!SD.exists(buf)) { outPath = String(buf); return true; }
    }
    return false;
  }


  static bool exportProfilesToSD(String& outPath, String* errOut = nullptr) {
    if (!subghzEnsureDir(SUBGHZ_DIR)) {
      if (errOut) *errOut = "SD not mounted";
      return false;
    }

    uint16_t count = 0;
    EEPROM.get(ADDR_PROFILE_COUNT, count);
    if (count > MAX_PROFILES) count = MAX_PROFILES;

    if (!makeNextExportPath(outPath)) {
      if (errOut) *errOut = "No free filename";
      return false;
    }

    File f = SD.open(outPath.c_str(), FILE_WRITE);
    if (!f) {
      if (errOut) *errOut = "Open failed";
      return false;
    }

    SubGhzExportHeader h{};
    h.magic = SUBGHZ_EXPORT_MAGIC;
    h.version = 1;
    h.count = count;
    h.profileSize = PROFILE_SIZE;
    h.reserved = 0;

    bool ok = (f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h));
    for (uint16_t i = 0; ok && i < count; i++) {
      SubGhzProfile p{};
      int addr = ADDR_PROFILE_START + (i * PROFILE_SIZE);
      EEPROM.get(addr, p);
      ok = (f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p));
    }
    f.close();

    if (!ok && errOut) *errOut = "Write failed";
    return ok;
  }

  static bool syncCurrentProfilesToSD(String* errOut = nullptr) {

    if (!subghzEnsureDir(SUBGHZ_DIR)) {
      if (errOut) *errOut = "SD not mounted";
      return false;
    }

    uint16_t count = 0;
    EEPROM.get(ADDR_PROFILE_COUNT, count);
    if (count > MAX_PROFILES) count = MAX_PROFILES;

    if (SD.exists(SUBGHZ_CURRENT_PATH)) SD.remove(SUBGHZ_CURRENT_PATH);
    File f = SD.open(SUBGHZ_CURRENT_PATH, FILE_WRITE);
    if (!f) {
      if (errOut) *errOut = "Open failed";
      return false;
    }

    SubGhzExportHeader h{};
    h.magic = SUBGHZ_EXPORT_MAGIC;
    h.version = 1;
    h.count = count;
    h.profileSize = PROFILE_SIZE;
    h.reserved = 0;

    bool ok = (f.write((const uint8_t*)&h, sizeof(h)) == sizeof(h));
    for (uint16_t i = 0; ok && i < count; i++) {
      SubGhzProfile p{};
      int addr = ADDR_PROFILE_START + (i * PROFILE_SIZE);
      EEPROM.get(addr, p);
      ok = (f.write((const uint8_t*)&p, sizeof(p)) == sizeof(p));
    }
    f.close();
    if (!ok && errOut) *errOut = "Write failed";
    return ok;
  }

  static bool importProfilesFromSD(const String& path, String* errOut = nullptr) {
    if (!subghzMountSD()) {
      if (errOut) *errOut = "SD not mounted";
      return false;
    }
    if (path.isEmpty() || !SD.exists(path.c_str())) {
      if (errOut) *errOut = "File not found";
      return false;
    }

    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) {
      if (errOut) *errOut = "Open failed";
      return false;
    }

    SubGhzExportHeader h{};
    if (f.read((uint8_t*)&h, sizeof(h)) != sizeof(h)) { f.close(); if (errOut) *errOut="Bad header"; return false; }
    if (h.magic != SUBGHZ_EXPORT_MAGIC || h.version != 1) { f.close(); if (errOut) *errOut="Wrong file"; return false; }
    if (h.profileSize != PROFILE_SIZE) { f.close(); if (errOut) *errOut="Size mismatch"; return false; }

    uint16_t count = h.count;
    if (count > MAX_PROFILES) count = MAX_PROFILES;

    clearProfilesInEeprom();
    for (uint16_t i = 0; i < count; i++) {
      SubGhzProfile p{};
      if (f.read((uint8_t*)&p, sizeof(p)) != sizeof(p)) { f.close(); if (errOut) *errOut="Read failed"; return false; }
      p.name[MAX_NAME_LENGTH - 1] = '\0';
      EEPROM.put(ADDR_PROFILE_START + (i * PROFILE_SIZE), p);
    }
    EEPROM.put(ADDR_PROFILE_COUNT, count);
    EEPROM.commit();
    f.close();
    return true;
  }

  struct SubGhzFileEntry {
    String path;
    uint16_t count = 0;
    bool isCurrent = false;
  };

  static bool readExportHeader(File& f, SubGhzExportHeader& out, String* errOut = nullptr) {
    if (f.read((uint8_t*)&out, sizeof(out)) != sizeof(out)) { if (errOut) *errOut="Bad header"; return false; }
    if (out.magic != SUBGHZ_EXPORT_MAGIC || out.version != 1) { if (errOut) *errOut="Wrong file"; return false; }
    if (out.profileSize != PROFILE_SIZE) { if (errOut) *errOut="Size mismatch"; return false; }
    return true;
  }

  static bool readProfileAt(const String& path, uint16_t localIndex, SubGhzProfile& out, String* errOut = nullptr) {
    if (!subghzMountSD()) { if (errOut) *errOut="SD not mounted"; return false; }
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) { if (errOut) *errOut="Open failed"; return false; }
    SubGhzExportHeader h{};
    if (!readExportHeader(f, h, errOut)) { f.close(); return false; }
    uint16_t count = h.count; if (count > MAX_PROFILES) count = MAX_PROFILES;
    if (localIndex >= count) { f.close(); if (errOut) *errOut="Index OOR"; return false; }
    uint32_t off = (uint32_t)sizeof(SubGhzExportHeader) + (uint32_t)localIndex * (uint32_t)PROFILE_SIZE;
    if (!f.seek(off)) { f.close(); if (errOut) *errOut="Seek failed"; return false; }
    if (f.read((uint8_t*)&out, sizeof(out)) != sizeof(out)) { f.close(); if (errOut) *errOut="Read failed"; return false; }
    out.name[MAX_NAME_LENGTH - 1] = '\0';
    f.close();
    return true;
  }

  static bool listAllProfileFiles(std::vector<SubGhzFileEntry>& out, String* errOut = nullptr) {
    out.clear();
    if (!subghzMountSD()) { if (errOut) *errOut="SD not mounted"; return false; }
    if (!SD.exists(SUBGHZ_DIR)) { if (errOut) *errOut="No /subghz"; return false; }
    File d = SD.open(SUBGHZ_DIR);
    if (!d) { if (errOut) *errOut="Open dir failed"; return false; }

    for (;;) {
      File f = d.openNextFile();
      if (!f) break;
      if (f.isDirectory()) { f.close(); continue; }
      String name = String(f.name());

      String fullPath = String(SUBGHZ_DIR) + "/" + name;

      bool isCurrent = (name == "profiles_current.bin");
      bool isArchive = name.startsWith("profiles_") && name.endsWith(".bin") && !isCurrent;
      if (!isCurrent && !isArchive) { f.close(); continue; }

      SubGhzExportHeader h{};
      String herr;
      bool ok = readExportHeader(f, h, &herr);
      f.close();
      if (!ok) continue;

      uint16_t cnt = h.count;
      if (cnt > MAX_PROFILES) cnt = MAX_PROFILES;
      if (cnt == 0) continue;

      SubGhzFileEntry e;
      e.path = fullPath;
      e.count = cnt;
      e.isCurrent = isCurrent;
      out.push_back(e);
    }
    d.close();

    std::sort(out.begin(), out.end(), [](const SubGhzFileEntry& a, const SubGhzFileEntry& b) {
      if (a.isCurrent != b.isCurrent) return a.isCurrent > b.isCurrent;
      return a.path > b.path;
    });
    return true;
  }

  static uint16_t totalProfilesInIndex(const std::vector<SubGhzFileEntry>& files) {
    uint32_t total = 0;
    for (auto& f : files) total += f.count;
    if (total > 65535) total = 65535;
    return (uint16_t)total;
  }

  static bool locateGlobalIndex(const std::vector<SubGhzFileEntry>& files, uint16_t globalIndex,
                                String& outPath, uint16_t& outLocalIdx) {
    uint32_t idx = globalIndex;
    for (auto& fe : files) {
      if (idx < fe.count) {
        outPath = fe.path;
        outLocalIdx = (uint16_t)idx;
        return true;
      }
      idx -= fe.count;
    }
    return false;
  }

  static bool deleteProfileFromFile(const String& path, uint16_t localIndex, String* errOut = nullptr) {
    if (!subghzMountSD()) { if (errOut) *errOut="SD not mounted"; return false; }
    File f = SD.open(path.c_str(), FILE_READ);
    if (!f) { if (errOut) *errOut="Open failed"; return false; }
    SubGhzExportHeader h{};
    if (!readExportHeader(f, h, errOut)) { f.close(); return false; }
    uint16_t count = h.count; if (count > MAX_PROFILES) count = MAX_PROFILES;
    if (localIndex >= count) { f.close(); if (errOut) *errOut="Index OOR"; return false; }

    SubGhzProfile buf[MAX_PROFILES]{};
    for (uint16_t i = 0; i < count; i++) {
      if (f.read((uint8_t*)&buf[i], sizeof(SubGhzProfile)) != sizeof(SubGhzProfile)) { f.close(); if (errOut) *errOut="Read failed"; return false; }
      buf[i].name[MAX_NAME_LENGTH - 1] = '\0';
    }
    f.close();

    for (uint16_t i = localIndex; i + 1 < count; i++) buf[i] = buf[i + 1];
    count--;

    if (SD.exists(path.c_str())) SD.remove(path.c_str());
    File w = SD.open(path.c_str(), FILE_WRITE);
    if (!w) { if (errOut) *errOut="Open write failed"; return false; }

    SubGhzExportHeader nh{};
    nh.magic = SUBGHZ_EXPORT_MAGIC;
    nh.version = 1;
    nh.count = count;
    nh.profileSize = PROFILE_SIZE;
    nh.reserved = 0;
    bool ok = (w.write((const uint8_t*)&nh, sizeof(nh)) == sizeof(nh));
    for (uint16_t i = 0; ok && i < count; i++) {
      ok = (w.write((const uint8_t*)&buf[i], sizeof(SubGhzProfile)) == sizeof(SubGhzProfile));
    }
    w.close();
    if (!ok && errOut) *errOut="Write failed";

    if (ok && path.endsWith("profiles_current.bin")) {
      importProfilesFromSD(path, nullptr);
    }
    return ok;
  }
}

#ifdef TFT_BLACK
#undef TFT_BLACK
#endif
#define TFT_BLACK FEATURE_BG

#ifndef FEATURE_TEXT
#define FEATURE_TEXT ORANGE
#endif
#ifndef FEATURE_WHITE
#define FEATURE_WHITE 0xFFFF
#endif

#ifdef TFT_WHITE
#undef TFT_WHITE
#endif
#define TFT_WHITE FEATURE_TEXT

#ifdef WHITE
#undef WHITE
#endif
#define WHITE FEATURE_WHITE

#ifdef DARK_GRAY
#undef DARK_GRAY
#endif
#define DARK_GRAY UI_FG

static constexpr int kSubghzScreenH = 320;

static int subghzContentBottom() {
  return featureHasTouchNavBar() ? touchNavContentBottomY() : kSubghzScreenH;
}

static void subghzClearBody(uint16_t color = TFT_BLACK) {
  if (featureHasTouchNavBar()) {
    featureClearContent(color);
  } else {
    tft.fillScreen(color);
  }
}

static constexpr unsigned long kSubghzNavDebounceMs = 200;

static void subghzWaitNavRelease(int pin) {
  while (isTouchNavButtonPressed(pin)) {
    delay(10);
  }
  delay(kSubghzNavDebounceMs);
}

static void subghzRedrawNavChrome() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  invalidateTouchButtonCue();
  redrawTouchButtonBar();
  maintainTouchNavBar();
}

static bool subghzWaitWithNav(uint32_t ms) {
  const uint32_t until = millis() + ms;
  while ((int32_t)(millis() - until) < 0) {
    if (feature_exit_requested || featureExitButtonPressed()) {
      return false;
    }
    if (featureHasTouchNavBar()) {
      maintainTouchNavBar();
    }
    delay(50);
  }
  return true;
}

static void subghzSetReplayNavLabels() {
  setTouchNavLabels("Freq-", "Save", "Exit", "Send", "Freq+");
}

static void subghzSetJammerNavLabels() {
  setTouchNavLabels("Freq-", "Auto", "Exit", "Toggle", "Freq+");
}

static void subghzSetProfileNavLabels() {
  setTouchNavLabels("Delete", "Next", "Exit", "Prev", "TX");
}

static void subghzSetBruteNavLabels() {
  setTouchNavLabels("Prev", "Sel", "Exit", "Go", "Next");
}

/* ── is there actually a CC1101 on the end of the bus? ───────────────────
 *
 * ELECHOUSE_CC1101's reset sequence is `while(digitalRead(MISO_PIN));` --
 * it waits for the chip to pull MISO low to say it is ready, and it waits
 * forever. There are eight such loops in the library. With no module wired,
 * MISO floats high and the first one never returns: the feature does not
 * fail, it stops, and the whole device stops with it because this runs on
 * the main task.
 *
 * Every SubGHz feature reached the library through Init() with nothing in
 * front of it, so every one of them froze a board that had no radio -- which
 * is every board, until the carrier exists. Opening the jamming detector on
 * a bare CYD is how this was found.
 *
 * This asks the chip who it is. Drive CS low with a deadline, then read the
 * PARTNUM and VERSION status registers over SPI and check the answer is one
 * a CC1101 would give.
 *
 * It used to sample MISO and call a low read "present", which a floating
 * input satisfies about as often as not. The board then went into
 * ELECHOUSE_CC1101::Init(), whose first act is `while(digitalRead(MISO_PIN));`
 * with no deadline, and locked up with the feature's screen already drawn.
 * A guard with a timeout was protecting a driver without one.
 *
 * A false here means "nothing that answers like a CC1101", not "the chip is
 * broken": a mis-wired MISO looks identical. That is the right thing to put
 * on screen either way. */
/* Read one CC1101 status register. Status registers need the burst bit set
 * as well as the read bit, so the header byte is addr | 0xC0.
 *
 * Deliberately not ELECHOUSE_CC1101::SpiReadStatus(), which opens with the
 * same unbounded `while(digitalRead(MISO_PIN));` as Init() and would hang in
 * the probe instead of in the driver. */
static uint8_t cc1101ReadStatusReg(uint8_t addr) {
  digitalWrite(CC1101_CS, LOW);

  /* The chip pulls MISO low when its crystal is stable. Bounded, because
   * that is the whole point of this file. */
  const uint32_t deadline = millis() + 5;
  while (digitalRead(CC1101_MISO) == HIGH && millis() < deadline) {
  }

  SPI.transfer(addr | 0xC0);
  const uint8_t value = SPI.transfer(0x00);
  digitalWrite(CC1101_CS, HIGH);
  return value;
}

static bool cc1101Present();

/* The menu needs to ask this too, and the probe is in here. */
bool subghzCc1101Present() {
    return cc1101Present();
}

static bool cc1101Present() {
  /* Probing means driving the bus, so own it first. claim() re-points the
   * GPIO matrix and applies the CC1101's clock, which is what makes a plain
   * SPI.transfer() below correct. */
  SpiBus::claim(SpiBus::Dev::Cc1101);

  pinMode(CC1101_CS, OUTPUT);
  pinMode(CC1101_MISO, INPUT);
  digitalWrite(CC1101_CS, HIGH);
  delayMicroseconds(50);

  /* PARTNUM is 0x00 on every CC1101. VERSION is 0x04 or 0x14 on genuine
   * parts and 0x07 or 0x17 on the clones these modules are usually built
   * from, so it is checked for being a plausible value rather than against a
   * list that would reject a working radio.
   *
   * Read twice. An absent chip leaves MISO floating, and a floating line can
   * return 0x00, 0xFF, or noise that happens to look like a version once.
   * Returning the same non-trivial value twice is what a real part does. */
  const uint8_t part1 = cc1101ReadStatusReg(0x30);   // PARTNUM
  const uint8_t ver1  = cc1101ReadStatusReg(0x31);   // VERSION
  delayMicroseconds(200);
  const uint8_t part2 = cc1101ReadStatusReg(0x30);
  const uint8_t ver2  = cc1101ReadStatusReg(0x31);

  const bool ok = (part1 == 0x00 && part2 == 0x00) &&
                  (ver1 == ver2) &&
                  (ver1 != 0x00 && ver1 != 0xFF);

  if (!ok) {
    /* Hand the bus back before the caller draws the "No CC1101" screen and
     * waits for a tap on it. That modal reads the touch controller, which
     * reads MISO on a different pad, so leaving the matrix pointed here
     * would trade a hang in the driver for a hang in the message about it. */
    if (SpiBus::touchSharesRadioBus()) {
      SpiBus::claim(SpiBus::Dev::Touch);
    }
  }
  return ok;
}

/* Probe, and if nothing answers say so and ask to leave. Returns false when
 * the caller must abort, and sets feature_exit_requested so the dispatch
 * loop in ESP32-DIV.ino unwinds the feature the same way a normal exit does. */
static bool cc1101Ready(const char* feature);

/* The same probe and the same message, without the leaving. For a screen
 * that needs the radio for one of the things it does rather than for all of
 * them. Returns false when the action must not run. */
static bool cc1101ReadyForAction(const char* action);

/* Draw the "no radio" screen and wait for the user to leave. Returns when
 * they do; the caller must then exit the feature. */
static void cc1101ReportMissing(const char* feature) {
  /* The shared panel, as the nRF24 and the Rubber Ducky use. */
  char msg[200];
  snprintf(msg, sizeof(msg),
           "%s needs the sub-GHz radio, and nothing answered on the SPI bus. "
           "Check the module is fitted and that MISO, CS, SCK and MOSI are "
           "wired.", feature);
  showNotification("No CC1101", msg);
}

static bool cc1101ReadyForAction(const char* action) {
  if (cc1101Present()) {
    return true;
  }
  cc1101ReportMissing(action);

  /* Modal, deliberately. Drawing the message and returning would put it on
   * screen for one frame, which reads as the device ignoring the press. */
  delay(250);
  for (;;) {
    int x, y;
    if (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT) ||
        readTouchXY(x, y)) {
      break;
    }
    delay(20);
  }
  while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
    delay(10);
  }
  return false;
}

/* A feature that cannot do anything without the radio asks for it here, and
 * leaves when it is not there. */
static bool cc1101Ready(const char* feature) {
  if (cc1101ReadyForAction(feature)) {
    return true;
  }
  feature_exit_requested = true;
  return false;
}

namespace replayat { void replayHandleNavButtons(); }
namespace subjammer { void subjammerHandleNavButtons(); }
namespace SavedProfile { void profileHandleNavButtons(); }
namespace SubBrute { void bruteHandleNavButtons(); }

namespace replayat {

#define EEPROM_SIZE 1440

#define ADDR_VALUE         1280
#define ADDR_BITLEN        1284
#define ADDR_PROTO         1286
#define ADDR_FREQ          1288
#define ADDR_PROFILE_COUNT 1296
#define ADDR_PROFILE_START 1300
#define MAX_PROFILES       5

constexpr int SCREEN_WIDTH = PUEO_SCREEN_W;
#define SCREENHEIGHT PUEO_SCREEN_H

static bool uiDrawn = false;

void runUI();
void sendSignal();
void saveProfile();
void updateDisplay();

#define MAX_NAME_LENGTH 16

const char* randomNames[] = {
  "Signal", "Remote", "KeyFob", "GateOpener", "DoorLock",
  "RFTest", "Profile", "Control", "Switch", "Beacon"
};
const uint8_t numRandomNames = 10;

/* The record that goes into EEPROM, declared once for the whole file.
 *
 * This was three structs with the same five fields: one here, one in
 * SavedProfile, and SubGhzProfile at the top. The export and sync paths
 * used SubGhzProfile, saveProfile used this one, and the browser used the
 * third. They agreed, which is the only reason a record written by one was
 * readable by the others. Editing one would have reinterpreted every
 * profile already in EEPROM, and a saved capture would have come back wrong
 * rather than anything failing.
 *
 * PROFILE_SIZE was doubled the same way, a constexpr at file scope and a
 * macro in each namespace shadowing it from there on. The constexpr lives
 * in the anonymous namespace at the top and is visible here, so the macros
 * are gone. */
using Profile = SubGhzProfile;

uint16_t profileCount = 0;

RCSwitch mySwitch = RCSwitch();
arduinoFFT FFTSUB = arduinoFFT();

const uint16_t samplesSUB = ESP32DIV_FFT_SAMPLES;
const double FrequencySUB = 5000;

double attenuation_num = 10;

unsigned int sampling_period;
unsigned long micro_s;

double vRealSUB[samplesSUB];
double vImagSUB[samplesSUB];

byte red[ESP32DIV_FFT_PALETTE_SIZE], green[ESP32DIV_FFT_PALETTE_SIZE],
     blue[ESP32DIV_FFT_PALETTE_SIZE];

unsigned int epochSUB = 0;
unsigned int colorcursor = 2016;

int rssi;

static constexpr uint8_t REPLAY_RX_PIN = SUBGHZ_RX_PIN;
static constexpr uint8_t REPLAY_TX_PIN = SUBGHZ_TX_PIN;

/** RCSwitch::disableReceive() errors if no ISR was ever attached — track arm state. */
static bool s_replayRxArmed = false;

static void replayArmReceive() {
  pinMode(REPLAY_RX_PIN, INPUT);
  pinMode(REPLAY_TX_PIN, INPUT);
  mySwitch.enableReceive(REPLAY_RX_PIN);
  mySwitch.resetAvailable();
  s_replayRxArmed = true;
}

static void replayDisarmReceive() {
  if (!s_replayRxArmed) {
    return;
  }
  mySwitch.disableReceive();
  s_replayRxArmed = false;
}

uint32_t receivedValue = 0;
uint16_t receivedBitLength = 0;
uint16_t receivedProtocol = 0;
const int rssi_threshold = -75;

uint16_t currentFrequencyIndex = 0;
int yshift = 20;

static bool autoScanEnabled = false;
static uint16_t scanIndex = 0;
static uint32_t lastHopMs = 0;
static uint32_t lockUntilMs = 0;
static constexpr uint32_t SCAN_DWELL_MS = 220;
static constexpr uint32_t SCAN_DWELL_LOW_MS = 360;
static constexpr uint32_t SCAN_SETTLE_MS = 70;
static constexpr uint32_t SCAN_SETTLE_LOW_MS = 95;
static constexpr uint32_t SCAN_SETTLE_BAND_MS = 140;
static constexpr uint32_t LOCK_HOLD_MS  = 2500;
static constexpr uint32_t RSSI_LOCK_MS  = 1200;
static constexpr int      RSSI_DETECT_THRESHOLD = -58;
static constexpr int      RSSI_DETECT_THRESHOLD_LOW = -70;
static constexpr int      RSSI_CLEAR_THRESHOLD  = -66;
static constexpr int      RSSI_CLEAR_THRESHOLD_LOW = -76;
static constexpr int      RSSI_DECODE_THRESHOLD = -55;
static constexpr int      RSSI_DECODE_THRESHOLD_LOW = -66;
static constexpr uint32_t RSSI_SAMPLE_MS = 45;
static constexpr uint8_t  RSSI_DETECT_HITS = 3;
static constexpr uint8_t  RSSI_DETECT_HITS_LOW = 2;
static constexpr uint32_t DECODE_MIN_DWELL_MS = 130;
static constexpr uint32_t DECODE_MIN_DWELL_LOW_MS = 180;
static constexpr uint32_t UI_SCAN_UPDATE_MS = 250;
static uint32_t lastUiScanUpdateMs = 0;
static uint32_t scanSettledAtMs = 0;
static uint32_t lastRssiSampleMs = 0;
static uint8_t  rssiDetectStreak = 0;
static bool     rssiHot = false;

static uint32_t lastDetectAlertMs = 0;
static uint16_t lastDetectAlertFreq = 0xFFFF;
static uint32_t notifHideAtMs = 0;
static bool notifActive = false;

#if PUEO_HAS_BUZZER
static constexpr uint8_t BUZZER_LEDC_CH = 7;
static bool buzzerArmed = false;
static uint32_t buzzerOffAtMs = 0;
#endif
static void replayBeep(uint16_t hz = 2200, uint16_t ms = 60) {
  #if PUEO_HAS_BUZZER
  ledcSetup(BUZZER_LEDC_CH, 4000, 8);
  ledcAttachPin(BUZZER_PIN, BUZZER_LEDC_CH);
  ledcWriteTone(BUZZER_LEDC_CH, hz);
  buzzerArmed = true;
  buzzerOffAtMs = millis() + ms;
  #endif
}

static void replayBeepPoll() {
  #if PUEO_HAS_BUZZER
  if (!buzzerArmed) return;
  if ((int32_t)(millis() - buzzerOffAtMs) < 0) return;
  ledcWriteTone(BUZZER_LEDC_CH, 0);

  ledcDetachPin(BUZZER_PIN);
  buzzerArmed = false;
  #endif
}

static void replayShowDetectNotice(const String& reason, int rssi = 0) {
  uint32_t now = millis();

  if (now - lastDetectAlertMs < 1200 && lastDetectAlertFreq == currentFrequencyIndex) return;
  lastDetectAlertMs = now;
  lastDetectAlertFreq = currentFrequencyIndex;

  char msg[96];
  float mhz = subghz_frequency_list[currentFrequencyIndex] / 1000000.0f;

  snprintf(msg, sizeof(msg), "%s @ %.2f MHz | RSSI %d", reason.c_str(), mhz, rssi);
  showNotificationActions("SubGHz Detected", msg, true);
  replayBeep(reason == "DECODE" ? 2600 : 2000, 70);
  notifActive = true;
  notifHideAtMs = 0;
}

static inline uint16_t freqCount() {
  return (uint16_t)(sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
}

static bool replayFreqIsLowBand(uint16_t idx) {
  return subghz_frequency_list[idx % freqCount()] < 350000000UL;
}

static uint8_t replayFreqBandId(uint16_t idx) {
  const uint32_t hz = subghz_frequency_list[idx % freqCount()];
  if (hz < 350000000UL) {
    return 0;
  }
  if (hz < 500000000UL) {
    return 1;
  }
  return 2;
}

static bool replayFreqBandChanged(uint16_t prevIdx, uint16_t newIdx) {
  return replayFreqBandId(prevIdx) != replayFreqBandId(newIdx);
}

static int replayRssiDetectThreshold() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_DETECT_THRESHOLD_LOW
                                                    : RSSI_DETECT_THRESHOLD;
}

static int replayRssiClearThreshold() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_CLEAR_THRESHOLD_LOW
                                                    : RSSI_CLEAR_THRESHOLD;
}

static int replayRssiDecodeThreshold() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_DECODE_THRESHOLD_LOW
                                                    : RSSI_DECODE_THRESHOLD;
}

static uint32_t replayScanDwellMs() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? SCAN_DWELL_LOW_MS : SCAN_DWELL_MS;
}

static uint32_t replayDecodeMinDwellMs() {
  return replayFreqIsLowBand(currentFrequencyIndex) ? DECODE_MIN_DWELL_LOW_MS
                                                    : DECODE_MIN_DWELL_MS;
}

static void tuneToIndex(uint16_t idx, bool persist = true) {
  currentFrequencyIndex = idx % freqCount();
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
  ELECHOUSE_cc1101.SetRx();
  if (persist) {
    EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
    EEPROM.commit();
  }
}

static void replayClearScanLock() {
  lockUntilMs = 0;
  rssiHot = false;
  rssiDetectStreak = 0;
}

static bool replayLooksLikeRealDecode(uint32_t value, uint16_t bits, uint16_t proto) {
  if (value == 0) {
    return false;
  }
  if (bits < 8 || bits > 64) {
    return false;
  }
  if (proto < 1 || proto > 12) {
    return false;
  }
  return true;
}

static void replayScanHopTo(uint16_t idx, uint16_t fromIdx) {
  tuneToIndex(idx, false);
  mySwitch.resetAvailable();
  mySwitch.setReceiveTolerance(replayFreqIsLowBand(idx) ? 50 : 40);

  uint32_t settleMs = SCAN_SETTLE_MS;
  if (replayFreqBandChanged(fromIdx, idx)) {
    settleMs = SCAN_SETTLE_BAND_MS;
  } else if (replayFreqIsLowBand(idx)) {
    settleMs = SCAN_SETTLE_LOW_MS;
  }
  scanSettledAtMs = millis() + settleMs;
  rssiDetectStreak = 0;
  lastRssiSampleMs = 0;
}

static void replayBeginAutoScan() {
  scanIndex = currentFrequencyIndex;
  lastHopMs = 0;
  scanSettledAtMs = 0;
  lastRssiSampleMs = 0;
  replayClearScanLock();
  lastUiScanUpdateMs = 0;
  mySwitch.resetAvailable();
}

static bool replayAutoScanReadyForDecode(uint32_t now) {
  if (lastHopMs == 0 || (now - lastHopMs) < replayDecodeMinDwellMs()) {
    return false;
  }
  if (now < scanSettledAtMs) {
    return false;
  }
  return ELECHOUSE_cc1101.getRssi() > replayRssiDecodeThreshold();
}

static void replaySampleRssiForScan(uint32_t now) {
  if (now < scanSettledAtMs) {
    return;
  }
  if (lastRssiSampleMs != 0 && (now - lastRssiSampleMs) < RSSI_SAMPLE_MS) {
    return;
  }

  const int rssi = ELECHOUSE_cc1101.getRssi();
  const int detectThreshold = replayRssiDetectThreshold();
  const uint8_t detectHits = replayFreqIsLowBand(currentFrequencyIndex) ? RSSI_DETECT_HITS_LOW
                                                                        : RSSI_DETECT_HITS;
  if (rssi > detectThreshold) {
    if (rssiDetectStreak < 255) {
      rssiDetectStreak++;
    }
    if (!rssiHot && rssiDetectStreak >= detectHits) {
      rssiHot = true;
      lockUntilMs = now + RSSI_LOCK_MS;
      EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
      EEPROM.commit();
      replayShowDetectNotice("RSSI", rssi);
    }
  } else {
    rssiDetectStreak = 0;
    if (rssiHot && rssi < replayRssiClearThreshold()) {
      rssiHot = false;
    }
  }
  lastRssiSampleMs = now;
}

static void replayFreqNext() {
  autoScanEnabled = false;
  replayClearScanLock();
  tuneToIndex((uint16_t)((currentFrequencyIndex + 1) % freqCount()), true);
  updateDisplay();
}

static void replayFreqPrev() {
  autoScanEnabled = false;
  replayClearScanLock();
  tuneToIndex((uint16_t)((currentFrequencyIndex + freqCount() - 1) % freqCount()), true);
  updateDisplay();
}


static void replayTrySave() {
  if (receivedValue == 0) {
    return;
  }
  autoScanEnabled = false;
  replayClearScanLock();
  saveProfile();
}

static bool s_replayStaticDrawn = false;

struct ReplayDisplayCache {
  uint16_t freqIndex = 0xFFFF;
  uint8_t modeState = 0xFF;
  uint16_t bitLength = 0xFFFF;
  int16_t rssi = -9999;
  uint16_t protocol = 0xFFFF;
  uint32_t value = 0xFFFFFFFF;
  bool valid = false;
};

static ReplayDisplayCache s_replayDisp;

static void replayInvalidateDisplay() {
  s_replayStaticDrawn = false;
  s_replayDisp = ReplayDisplayCache{};
}

static void replayRestoreStatusPanel() {
  replayInvalidateDisplay();
  updateDisplay();
}

static uint8_t replayModeState() {
  const bool locked = (autoScanEnabled && lockUntilMs != 0 &&
                       (int32_t)(millis() - lockUntilMs) < 0);
  if (locked) {
    return 2;
  }
  return autoScanEnabled ? 1 : 0;
}

static constexpr int kReplayStatusLineY = 80;
static constexpr int kReplayValueLineH = 11;

static void replayDrawStatusSeparator() {
  tft.drawFastHLine(0, kReplayStatusLineY, PUEO_SCREEN_W, UI_LINE);
}

static void replayDrawValueCell(int x, int y, int w, int h, const String& text, uint16_t color) {
  const int maxH = kReplayStatusLineY - y;
  if (maxH <= 0) {
    return;
  }
  const int clipH = min(h, maxH);
  tft.fillRect(x, y, w, clipH, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(text);
}

static void replayDrawStaticChrome() {
  if (s_replayStaticDrawn) {
    return;
  }

  const int bodyBottom = subghzContentBottom();
  const int infoH = min(kReplayStatusLineY - 40, bodyBottom - 40);
  if (infoH > 0) {
    tft.fillRect(0, 40, PUEO_SCREEN_W, infoH, TFT_BLACK);
  }
  replayDrawStatusSeparator();

  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(5, 20 + yshift);
  tft.print("Freq:");
  tft.setCursor(5, 35 + yshift);
  tft.print("Bit:");
  tft.setCursor(130, 35 + yshift);
  tft.print("RSSI:");
  tft.setCursor(130, 20 + yshift);
  tft.print("Ptc:");
  tft.setCursor(5, 50 + yshift);
  tft.print("Val:");

  s_replayStaticDrawn = true;
}

void replayHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    replayFreqPrev();
    subghzWaitNavRelease(BTN_LEFT);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    replayFreqNext();
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    if (receivedValue != 0) {
      autoScanEnabled = false;
      replayClearScanLock();
      sendSignal();
    }
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    replayTrySave();
    subghzWaitNavRelease(BTN_DOWN);
  }
}

void updateDisplay() {
    replayDrawStaticChrome();

    const uint8_t modeState = replayModeState();
    const int16_t rssi = ELECHOUSE_cc1101.getRssi();
    char freqBuf[16];
    char modeBuf[8];
    char bitBuf[8];
    char rssiBuf[8];
    char ptcBuf[8];
    char valBuf[16];

    snprintf(freqBuf, sizeof(freqBuf), "%.2f MHz",
             subghz_frequency_list[currentFrequencyIndex] / 1000000.0);
    if (modeState == 2) {
      snprintf(modeBuf, sizeof(modeBuf), "LOCK");
    } else {
      snprintf(modeBuf, sizeof(modeBuf), "%s", modeState == 1 ? "AUTO" : "MAN ");
    }
    snprintf(bitBuf, sizeof(bitBuf), "%d", receivedBitLength);
    snprintf(rssiBuf, sizeof(rssiBuf), "%d", rssi);
    snprintf(ptcBuf, sizeof(ptcBuf), "%d", receivedProtocol);
    snprintf(valBuf, sizeof(valBuf), "%lu", (unsigned long)receivedValue);

    const bool fullRedraw = !s_replayDisp.valid;
    if (fullRedraw || s_replayDisp.freqIndex != currentFrequencyIndex) {
      replayDrawValueCell(50, 20 + yshift, 72, kReplayValueLineH, freqBuf, UI_WARN);
      s_replayDisp.freqIndex = currentFrequencyIndex;
    }
    if (fullRedraw || s_replayDisp.modeState != modeState) {
      replayDrawValueCell(175, 20 + yshift, 40, kReplayValueLineH, modeBuf, UI_WARN);
      s_replayDisp.modeState = modeState;
    }
    if (fullRedraw || s_replayDisp.bitLength != receivedBitLength) {
      replayDrawValueCell(50, 35 + yshift, 40, kReplayValueLineH, bitBuf, UI_WARN);
      s_replayDisp.bitLength = receivedBitLength;
    }
    if (fullRedraw || s_replayDisp.rssi != rssi) {
      replayDrawValueCell(170, 35 + yshift, 48, kReplayValueLineH, rssiBuf, UI_WARN);
      s_replayDisp.rssi = rssi;
    }
    if (fullRedraw || s_replayDisp.protocol != receivedProtocol) {
      replayDrawValueCell(170, 20 + yshift, 40, kReplayValueLineH, ptcBuf, UI_WARN);
      s_replayDisp.protocol = receivedProtocol;
    }
    if (fullRedraw || s_replayDisp.value != receivedValue) {
      replayDrawValueCell(50, 50 + yshift, 180, kReplayValueLineH, valBuf, UI_WARN);
      s_replayDisp.value = receivedValue;
    }

    replayDrawStatusSeparator();

    s_replayDisp.valid = true;
    /* Do NOT idle/retune here — that drops RCSwitch pulse timing mid-receive. */
}

String getUserInputName() {
  OnScreenKeyboardConfig cfg;
  cfg.titleLine1     = "[!] Set a name for the saved profile.";
  cfg.titleLine2     = "(max 15 chars, ^ caps, # sym)";
  osKeyboardUseStandardLayout(cfg);
  cfg.maxLen         = MAX_NAME_LENGTH - 1;
  cfg.shuffleNames   = randomNames;
  cfg.shuffleCount   = numRandomNames;
  cfg.buttonsY       = 195;
  cfg.backLabel      = "Back";
  cfg.middleLabel    = "Shuffle";
  cfg.okLabel        = "OK";
  cfg.enableShuffle  = true;
  cfg.requireNonEmpty = true;
  cfg.emptyErrorMsg  = "Name cannot be empty!";

  OnScreenKeyboardResult r = showOnScreenKeyboard(cfg, "");

  if (!r.accepted) {

    subghzClearBody(TFT_BLACK);
    uiDrawn = false;
    replayRestoreStatusPanel();
    runUI();
    subghzRedrawNavChrome();
  }
  return r.text;
}

void sendSignal() {

    replayDisarmReceive();
    delay(100);
    pinMode(REPLAY_TX_PIN, OUTPUT);
    mySwitch.enableTransmit(REPLAY_TX_PIN);
    ELECHOUSE_cc1101.SetTx();

    tft.fillRect(0, 40, PUEO_SCREEN_W, kReplayStatusLineY - 40, TFT_BLACK);

    tft.setCursor(10, 30 + yshift);
    tft.print("Sending...");
    tft.setCursor(10, 40 + yshift);
    tft.print(receivedValue);

    mySwitch.setProtocol(receivedProtocol);
    mySwitch.send(receivedValue, receivedBitLength);

    delay(500);
    tft.fillRect(0, 40, PUEO_SCREEN_W, kReplayStatusLineY - 40, TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    mySwitch.disableTransmit();
    pinMode(REPLAY_TX_PIN, INPUT);
    pinMode(REPLAY_RX_PIN, INPUT);
    ELECHOUSE_cc1101.SetRx();
    delay(50);
    replayArmReceive();

    delay(500);
    replayRestoreStatusPanel();
}

void do_sampling() {
  constexpr unsigned int kGraphYOffset = 81;
  const int plotY = (int)epochSUB + (int)kGraphYOffset;
  if (plotY >= subghzContentBottom()) {
    return;
  }

  micro_s = micros();

  #define ALPHA 0.2
  float ewmaRSSI = -50;

for (int i = 0; i < samplesSUB; i++) {
    int rssi = ELECHOUSE_cc1101.getRssi();
    rssi += 100;

    ewmaRSSI = (ALPHA * rssi) + ((1 - ALPHA) * ewmaRSSI);

    vRealSUB[i] = ewmaRSSI * 2;
    vImagSUB[i] = 1;

    while (micros() < micro_s + sampling_period);
    micro_s += sampling_period;
}

  double mean = 0;

  for (uint16_t i = 0; i < samplesSUB; i++)
        mean += vRealSUB[i];
        mean /= samplesSUB;
  for (uint16_t i = 0; i < samplesSUB; i++)
        vRealSUB[i] -= mean;

  micro_s = micros();

  FFTSUB.Windowing(vRealSUB, samplesSUB, FFT_WIN_TYP_HAMMING, FFT_FORWARD);
  FFTSUB.Compute(vRealSUB, vImagSUB, samplesSUB, FFT_FORWARD);
  FFTSUB.ComplexToMagnitude(vRealSUB, vImagSUB, samplesSUB);

unsigned int left_x = 120;
unsigned int graph_y_offset = kGraphYOffset;
int max_k = 0;

for (int j = 0; j < samplesSUB >> 1; j++) {
    int k = vRealSUB[j] / attenuation_num;
    if (k > max_k)
        max_k = k;
    if (k > 127) k = 127;

    unsigned int color = red[k] << 11 | green[k] << 5 | blue[k];
    unsigned int vertical_x = left_x + j;

    tft.drawPixel(vertical_x, epochSUB + graph_y_offset, color);
}

for (int j = 0; j < samplesSUB >> 1; j++) {
    int k = vRealSUB[j] / attenuation_num;
    if (k > max_k)
        max_k = k;
    if (k > 127) k = 127;

    unsigned int color = red[k] << 11 | green[k] << 5 | blue[k];
    unsigned int mirrored_x = left_x - j;
    tft.drawPixel(mirrored_x, epochSUB + graph_y_offset, color);
}

  double tattenuation = max_k / 127.0;

  if (tattenuation > attenuation_num)
    attenuation_num = tattenuation;

    delay(10);
}

void readProfileCount() {
    EEPROM.get(ADDR_PROFILE_COUNT, profileCount);
    if (profileCount > MAX_PROFILES) profileCount = 0;
}

/* Make a slot available, exporting and clearing when EEPROM is full.
 *
 * Split out of saveProfile() because it is the part that can fail, and it
 * has to run before anything prompts: the original checked for room first
 * and only then asked for a name, which is the right order. Folding this
 * into the write would mean asking for a name and then saying there is no
 * space for it.
 *
 * Returns false only when EEPROM is full and the export failed, with `err`
 * naming why. True means storeProfile() will find a slot.
 *
 * `rotated` says the five that were there went to the card and EEPROM was
 * cleared, which the caller should mention rather than leave to be noticed.
 */
bool makeRoomForProfile(String* err = nullptr, bool* rotated = nullptr) {
    readProfileCount();
    if (rotated != nullptr) *rotated = false;

    if (profileCount < MAX_PROFILES) {
        return true;
    }

    String outPath;
    if (!exportProfilesToSD(outPath, err)) {
        return false;
    }
    clearProfilesInEeprom();
    profileCount = 0;
    syncCurrentProfilesToSD(nullptr);
    if (rotated != nullptr) *rotated = true;
    return true;
}

/* Write a record to the next free slot. Assumes makeRoomForProfile() said
 * yes, which is the caller's job and is why this cannot fail. Returns the
 * slot it used. */
uint16_t storeProfile(const Profile& p) {
    const int addr = ADDR_PROFILE_START + (profileCount * PROFILE_SIZE);
    EEPROM.put(addr, p);
    EEPROM.commit();

    const uint16_t slot = profileCount;
    profileCount++;
    EEPROM.put(ADDR_PROFILE_COUNT, profileCount);
    EEPROM.commit();

    syncCurrentProfilesToSD(nullptr);
    return slot;
}

/* Build a record from a capture. The name is the caller's, because only the
 * caller knows whether it came from a keyboard or out of a file. */
Profile makeProfile(uint32_t frequency, uint32_t value, uint16_t bitLength,
                    uint16_t protocol, const char* name) {
    Profile p{};
    p.frequency = frequency;
    p.value = value;
    p.bitLength = bitLength;
    p.protocol = protocol;
    strncpy(p.name, name ? name : "", MAX_NAME_LENGTH - 1);
    p.name[MAX_NAME_LENGTH - 1] = '\0';
    return p;
}

void saveProfile() {
    String err;
    bool rotated = false;

    if (!makeRoomForProfile(&err, &rotated)) {
        subghzClearBody(TFT_BLACK);
        tft.setTextSize(1);
        tft.setCursor(10, 30 + yshift);
        tft.setTextColor(UI_WARN, TFT_BLACK);
        tft.print("Storage full!");
        tft.setCursor(10, 45 + yshift);
        tft.setTextColor(UI_TEXT, TFT_BLACK);
        tft.print("Insert SD / export fail");
        tft.setCursor(10, 60 + yshift);
        tft.print(err);
        uiDrawn = false;
        runUI();
        subghzRedrawNavChrome();
        if (!subghzWaitWithNav(2000)) {
          return;
        }
        replayRestoreStatusPanel();
        float v = readBatteryVoltage();
        drawStatusBar(v, true);
        uiDrawn = false;
        runUI();
        subghzRedrawNavChrome();
        return;
    }

    String customName = getUserInputName();
    tft.setTextSize(1);

    const Profile newProfile = makeProfile(
        subghz_frequency_list[currentFrequencyIndex],
        (uint32_t)receivedValue,
        (uint16_t)receivedBitLength,
        (uint16_t)receivedProtocol,
        customName.c_str());

    storeProfile(newProfile);

    subghzClearBody(TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Profile saved!");
    tft.setCursor(10, 40 + yshift);
    tft.print("Name: ");
    tft.print(newProfile.name);
    tft.setCursor(10, 50 + yshift);
    tft.print("Profiles saved: ");
    tft.println(profileCount);
    if (rotated) {
        /* Said, rather than left to be noticed later: the five that were
         * there are on the card now and not in EEPROM. */
        tft.setCursor(10, 60 + yshift);
        tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
        tft.print("earlier five exported to SD");
        tft.setTextColor(UI_TEXT, TFT_BLACK);
    }

    uiDrawn = false;
    runUI();
    subghzRedrawNavChrome();
    if (!subghzWaitWithNav(2000)) {
      return;
    }
    replayRestoreStatusPanel();
    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    uiDrawn = false;
    runUI();
    subghzRedrawNavChrome();
}

void loadProfileCount() {

    readProfileCount();
}

void runUI() {

    #undef STATUS_BAR_Y_OFFSET
    constexpr int STATUS_BAR_Y_OFFSET = 20;
    constexpr int STATUS_BAR_HEIGHT = 16;
    constexpr int ICON_SIZE = 16;
    constexpr int ICON_NUM = 6;

    static int iconX[ICON_NUM] = {PUEO_SCREEN_W - 150, PUEO_SCREEN_W - 110, PUEO_SCREEN_W - 70, PUEO_SCREEN_W - 30, PUEO_SCREEN_W - 190, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_sort_up_plus,
        bitmap_icon_sort_down_minus,
        bitmap_icon_antenna,
        bitmap_icon_floppy,
        bitmap_icon_random,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_ICON);
            }
        }
        tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
        tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, PUEO_SCREEN_W, UI_LINE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_ICON);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                    autoScanEnabled = false;
                    currentFrequencyIndex = (currentFrequencyIndex + 1) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    tuneToIndex(currentFrequencyIndex, true);
                    updateDisplay();
                    break;
                case 1:
                    autoScanEnabled = false;
                    currentFrequencyIndex = (currentFrequencyIndex - 1 + (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]))) % (sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
                    tuneToIndex(currentFrequencyIndex, true);
                    updateDisplay();
                    break;
                case 2:
                    sendSignal();
                    break;
                case 3:
                    saveProfile();
                    break;
                case 4:
                    autoScanEnabled = !autoScanEnabled;
                    if (autoScanEnabled) {
                      replayBeginAutoScan();
                    } else {
                      replayClearScanLock();
                    }
                    updateDisplay();
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        int x, y;
        if (feature_active && readTouchXY(x, y)) {
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 5) {
                                feature_exit_requested = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                                animationState = 1;
                                activeIcon = i;
                                lastAnimationTime = millis();
                            }
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
}

void ReplayAttackSetup() {
  if (Stealth::refuse("Replay Attack")) return;

  if (!cc1101Ready("Replay Attack")) return;
  pauseBackgroundRadioTasks();
  setTouchButtonInputEnabled(true);
  subghzSetReplayNavLabels();

  replayDisarmReceive();
  mySwitch.resetAvailable();

  reclaimSharedSpiBus();
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);

  EEPROM.begin(EEPROM_SIZE);
  readProfileCount();

  EEPROM.get(ADDR_VALUE, receivedValue);
  EEPROM.get(ADDR_BITLEN, receivedBitLength);
  EEPROM.get(ADDR_PROTO, receivedProtocol);
  EEPROM.get(ADDR_FREQ, currentFrequencyIndex);

  const uint16_t freqCount = (uint16_t)(sizeof(subghz_frequency_list) / sizeof(subghz_frequency_list[0]));
  if (currentFrequencyIndex >= freqCount) currentFrequencyIndex = 0;

  autoScanEnabled = false;
  replayClearScanLock();

  subghzClearBody(TFT_BLACK);
  tft.setRotation(TFT_ROTATION);

  drawStatusBar(readBatteryVoltage(), true);
  subghzRedrawNavChrome();
  setupTouchscreen();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  sampling_period = round(1000000*(1.0/FrequencySUB));

  for (int i = 0; i < 32; i++) {
    red[i] = i / 2;
    green[i] = 0;
    blue[i] = i;
  }
  for (int i = 32; i < 64; i++) {
    red[i] = i / 2;
    green[i] = 0;
    blue[i] = 63 - i;
  }
#if ESP32DIV_FFT_PALETTE_SIZE > 64
  for (int i = 64; i < 96; i++) {
    red[i] = 31;
    green[i] = (i - 64) * 2;
    blue[i] = 0;
  }
  for (int i = 96; i < 128; i++) {
    red[i] = 31;
    green[i] = 63;
    blue[i] = i - 96;
  }
#endif

  replayInvalidateDisplay();
  updateDisplay();
  uiDrawn = false;
  subghzRedrawNavChrome();


  /* Bring radio up after UI/SPI activity so first entry RX matches re-entry. */
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(500.0);

  pinMode(REPLAY_RX_PIN, INPUT);
  pinMode(REPLAY_TX_PIN, INPUT);

  tuneToIndex(currentFrequencyIndex, false);
  mySwitch.setReceiveTolerance(replayFreqIsLowBand(currentFrequencyIndex) ? 50 : 40);
  mySwitch.setRepeatTransmit(8);

  delay(50);
  replayArmReceive();
}

void ReplayAttackLoop() {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
        replayDisarmReceive();
        feature_exit_requested = true;
        return;
    }

    maintainTouchNavBar();
    runUI();
    if (uiDrawn) {
      tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
      tft.drawFastHLine(0, 36, PUEO_SCREEN_W, UI_LINE);
      if (s_replayDisp.valid) {
        replayDrawStatusSeparator();
      }
    }
    replayHandleNavButtons();

    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    static bool prevLeft = false, prevRight = false, prevUp = false, prevDown = false;
    const bool leftPressed  = isPhysicalButtonPressed(BTN_LEFT);
    const bool rightPressed = isPhysicalButtonPressed(BTN_RIGHT);
    const bool upPressed    = isPhysicalButtonPressed(BTN_UP);
    const bool downPressed  = isPhysicalButtonPressed(BTN_DOWN);

    replayBeepPoll();

    if (notifActive && isNotificationVisible()) {
      int x, y;
      if (readTouchXY(x, y)) {
        NotificationAction act = notificationHandleTouch(x, y);
        if (act == NotificationAction::Save) {
          notifActive = false;

          subghzClearBody(TFT_BLACK);
          uiDrawn = false;
          replayInvalidateDisplay();
          float v = readBatteryVoltage();
          drawStatusBar(v, true);
          runUI();
          updateDisplay();
          subghzRedrawNavChrome();

          autoScanEnabled = false;
          saveProfile();

          subghzClearBody(TFT_BLACK);
          uiDrawn = false;
          replayInvalidateDisplay();
          v = readBatteryVoltage();
          drawStatusBar(v, true);
          runUI();
          updateDisplay();
          subghzRedrawNavChrome();
        } else if (act == NotificationAction::Ok || act == NotificationAction::Close) {
          notifActive = false;

          lastDetectAlertMs = millis();
          lastDetectAlertFreq = currentFrequencyIndex;
          lockUntilMs = millis() + 1500;
          rssiHot = true;

          subghzClearBody(TFT_BLACK);
          uiDrawn = false;
          replayInvalidateDisplay();
          float v = readBatteryVoltage();
          drawStatusBar(v, true);
          runUI();
          updateDisplay();
          subghzRedrawNavChrome();
        }
      }

      return;
    } else if (notifActive && !isNotificationVisible()) {

      notifActive = false;
      subghzClearBody(TFT_BLACK);
      uiDrawn = false;
      replayInvalidateDisplay();
      float v = readBatteryVoltage();
      drawStatusBar(v, true);
      runUI();
      updateDisplay();
      subghzRedrawNavChrome();
    }

    if (rightPressed && !prevRight && millis() - lastDebounceTime > debounceDelay) {
        replayFreqNext();
        lastDebounceTime = millis();
    }
    if (leftPressed && !prevLeft && millis() - lastDebounceTime > debounceDelay) {
        replayFreqPrev();
        lastDebounceTime = millis();
    }
    if (upPressed && !prevUp && receivedValue != 0 && millis() - lastDebounceTime > debounceDelay) {
        autoScanEnabled = false;
        replayClearScanLock();
        sendSignal();
        lastDebounceTime = millis();
    }
    if (downPressed && !prevDown && millis() - lastDebounceTime > debounceDelay) {
        replayTrySave();
        lastDebounceTime = millis();
    }

    prevLeft = leftPressed;
    prevRight = rightPressed;
    prevUp = upPressed;
    prevDown = downPressed;

    if (autoScanEnabled) {
      const uint32_t now = millis();
      const bool scanLocked = (lockUntilMs != 0 && (int32_t)(now - lockUntilMs) < 0);

      if (!scanLocked &&
          (lastHopMs == 0 || (now - lastHopMs) >= replayScanDwellMs())) {
        const uint16_t fromIdx = currentFrequencyIndex;
        scanIndex = (uint16_t)((scanIndex + 1) % freqCount());
        replayScanHopTo(scanIndex, fromIdx);
        lastHopMs = now;
        rssiHot = false;
      }

      replaySampleRssiForScan(now);

      if (lastUiScanUpdateMs == 0 || (now - lastUiScanUpdateMs) >= UI_SCAN_UPDATE_MS) {
        updateDisplay();
        lastUiScanUpdateMs = now;
      }
    }

    if (!autoScanEnabled) {
      do_sampling();
    }
    delay(10);
    epochSUB++;

    if (epochSUB >= tft.width())
      epochSUB = 0;

    if (mySwitch.available()) {
        const uint32_t val = mySwitch.getReceivedValue();
        const uint16_t bits = mySwitch.getReceivedBitlength();
        const uint16_t proto = mySwitch.getReceivedProtocol();
        mySwitch.resetAvailable();

        const uint32_t now = millis();
        const bool validDecode = replayLooksLikeRealDecode(val, bits, proto) &&
            (!autoScanEnabled || replayAutoScanReadyForDecode(now));

        if (validDecode) {
          receivedValue = val;
          receivedBitLength = bits;
          receivedProtocol = proto;

          EEPROM.put(ADDR_VALUE, receivedValue);
          EEPROM.put(ADDR_BITLEN, receivedBitLength);
          EEPROM.put(ADDR_PROTO, receivedProtocol);
          EEPROM.commit();

          updateDisplay();

          if (autoScanEnabled) {
            lockUntilMs = now + LOCK_HOLD_MS;
            scanIndex = currentFrequencyIndex;
            rssiHot = false;
            rssiDetectStreak = 0;

            EEPROM.put(ADDR_FREQ, currentFrequencyIndex);
            EEPROM.commit();
            replayShowDetectNotice("DECODE", ELECHOUSE_cc1101.getRssi());
          }
        }
    }

  }
}

namespace SavedProfile {

void updateDisplay();
void runUI();
void transmitProfile(int index);
void deleteProfile(int index);

static bool uiDrawn = false;

#define EEPROM_SIZE 1440

#define ADDR_PROFILE_COUNT 1296
#define ADDR_PROFILE_START 1300
#define MAX_PROFILES       5
#define MAX_NAME_LENGTH    16

constexpr int SCREEN_WIDTH = PUEO_SCREEN_W;

RCSwitch mySwitch = RCSwitch();
/* The same record as replayat's, and the same one SubGhzProfile describes.
 * See the note on the alias in replayat for why there is only one now. */
using Profile = SubGhzProfile;

uint16_t profileCount = 0;
uint16_t currentProfileIndex = 0;

int yshift = 16;

static std::vector<SubGhzFileEntry> sdFiles;
static uint16_t sdTotalProfiles = 0;
static String sdLastErr = "";
static String selectedPath = "";
static uint16_t selectedLocalIdx = 0;
static Profile selectedProfile{};
static bool selectedValid = false;

static constexpr uint8_t ITEMS_PER_PAGE = 7;
static constexpr int PROFILE_PAD_X = 10;
static constexpr int LIST_X = PROFILE_PAD_X;
static constexpr int LIST_W = 220;

static constexpr int PROFILE_HEADER_Y = 50;
static constexpr int PROFILE_HEADER_H = 14;
static constexpr int LIST_Y = PROFILE_HEADER_Y + PROFILE_HEADER_H + 2;
static constexpr int ROW_H  = 18;
static constexpr int PROFILE_LINE_H = 14;
static constexpr int PROFILE_LINE_GAP = 3;
static constexpr int PROFILE_LINE_STEP = PROFILE_LINE_H + PROFILE_LINE_GAP;
static constexpr int PROFILE_INFO_LINES = 4;
static constexpr int PROFILE_INFO_CONTENT_H =
    PROFILE_LINE_STEP * (PROFILE_INFO_LINES - 1) + PROFILE_LINE_H;
static constexpr int PROFILE_LABEL_X = PROFILE_PAD_X;
static constexpr int PROFILE_VALUE_X = 50;
static constexpr int PROFILE_COL2_LABEL_X = 130;
static constexpr int PROFILE_COL2_VALUE_X = 165;

static constexpr int UI_GAP_Y = 6;

static int profileBottomY() {
  return subghzContentBottom();
}

static int profileListBottom() {
  return LIST_Y + (ITEMS_PER_PAGE * ROW_H);
}

static int profileDetailsY() {
  const int areaTop = profileListBottom();
  const int areaBottom = profileBottomY();
  const int areaH = areaBottom - areaTop;
  if (areaH <= PROFILE_INFO_CONTENT_H) {
    return areaTop + UI_GAP_Y;
  }
  return areaTop + (areaH - PROFILE_INFO_CONTENT_H) / 2;
}

static void profileClearContentArea(uint16_t color = TFT_BLACK) {
  const int top = 40;
  const int h = subghzContentBottom() - top;
  if (h > 0) {
    tft.fillRect(0, top, PUEO_SCREEN_W, h, color);
  }
}

static void profileRestoreChrome() {
  drawStatusBar(readBatteryVoltage(), true);
  uiDrawn = false;
  updateDisplay();
  runUI();
  subghzRedrawNavChrome();
}

static void updateSelectionUI(uint16_t oldIndex, bool forceListRedraw = false);

static uint16_t cachedPageStart = 0xFFFF;
static SubGhzProfile cachedPage[ITEMS_PER_PAGE]{};
static bool cachedOk[ITEMS_PER_PAGE]{};
static bool cacheDirty = true;

static bool deleteArmed = false;
static uint32_t deleteArmUntilMs = 0;

static void refreshSdIndex(bool keepSelection = true) {
    uint16_t oldIdx = currentProfileIndex;
    String err;
    if (!listAllProfileFiles(sdFiles, &err)) {
        sdFiles.clear();
        sdTotalProfiles = 0;
        currentProfileIndex = 0;
        selectedValid = false;
        sdLastErr = err;
        cacheDirty = true;
        return;
    }
    sdLastErr = "";
    sdTotalProfiles = totalProfilesInIndex(sdFiles);
    if (sdTotalProfiles == 0) {
        currentProfileIndex = 0;
        selectedValid = false;
        sdLastErr = "No profiles found";
        cacheDirty = true;
        return;
    }
    if (keepSelection) currentProfileIndex = oldIdx;
    if (currentProfileIndex >= sdTotalProfiles) currentProfileIndex = (uint16_t)(sdTotalProfiles - 1);
    selectedValid = false;
    cacheDirty = true;
}

static bool loadSelectedFromSd(String* errOut = nullptr) {
    if (sdTotalProfiles == 0) { selectedValid = false; return false; }
    if (!locateGlobalIndex(sdFiles, currentProfileIndex, selectedPath, selectedLocalIdx)) {
        selectedValid = false; if (errOut) *errOut="Locate failed"; return false;
    }
    SubGhzProfile p{};
    if (!readProfileAt(selectedPath, selectedLocalIdx, p, errOut)) {
        selectedValid = false; return false;
    }

    selectedProfile.frequency = p.frequency;
    selectedProfile.value = p.value;
    selectedProfile.bitLength = p.bitLength;
    selectedProfile.protocol = p.protocol;
    memcpy(selectedProfile.name, p.name, MAX_NAME_LENGTH);
    selectedProfile.name[MAX_NAME_LENGTH - 1] = '\0';
    selectedValid = true;
    return true;
}

static uint16_t pageStartForIndex(uint16_t idx) {
  return (uint16_t)((idx / ITEMS_PER_PAGE) * ITEMS_PER_PAGE);
}

static void ensurePageCache() {
  if (sdTotalProfiles == 0) return;
  uint16_t start = pageStartForIndex(currentProfileIndex);
  if (!cacheDirty && cachedPageStart == start) return;
  cachedPageStart = start;
  for (uint8_t i = 0; i < ITEMS_PER_PAGE; i++) {
    cachedOk[i] = false;
    uint16_t globalIdx = (uint16_t)(start + i);
    if (globalIdx >= sdTotalProfiles) continue;
    String pth; uint16_t li = 0;
    if (!locateGlobalIndex(sdFiles, globalIdx, pth, li)) continue;
    String err;
    cachedOk[i] = readProfileAt(pth, li, cachedPage[i], &err);
    if (!cachedOk[i]) memset(&cachedPage[i], 0, sizeof(SubGhzProfile));
  }
  cacheDirty = false;
}

static void drawHeaderLine() {
  const int hy = PROFILE_HEADER_Y;
  tft.fillRect(LIST_X, hy, LIST_W, PROFILE_HEADER_H, TFT_BLACK);
  tft.setTextColor(UI_WARN, TFT_BLACK);
  tft.setCursor(LIST_X, hy);
  tft.printf("Profile %d/%d", (int)currentProfileIndex + 1, (int)sdTotalProfiles);
}

static void profileSelectNext() {
  if (sdTotalProfiles == 0) {
    return;
  }
  uint16_t oldIdx = currentProfileIndex;
  currentProfileIndex = (uint16_t)((currentProfileIndex + 1) % sdTotalProfiles);
  selectedValid = false;
  updateSelectionUI(oldIdx, false);
}

static void profileSelectPrev() {
  if (sdTotalProfiles == 0) {
    return;
  }
  uint16_t oldIdx = currentProfileIndex;
  currentProfileIndex = (uint16_t)((currentProfileIndex + sdTotalProfiles - 1) % sdTotalProfiles);
  selectedValid = false;
  updateSelectionUI(oldIdx, false);
}


void profileHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    profileSelectPrev();
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    profileSelectNext();
    subghzWaitNavRelease(BTN_DOWN);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    if (sdTotalProfiles > 0) {
      transmitProfile(currentProfileIndex);
    }
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    if (sdTotalProfiles > 0) {
      deleteProfile(currentProfileIndex);
    }
    subghzWaitNavRelease(BTN_LEFT);
  }
}

static void drawRow(uint16_t pageStart, uint8_t row) {
  uint16_t globalIdx = (uint16_t)(pageStart + row);
  if (globalIdx >= sdTotalProfiles) return;

  bool isSel = (globalIdx == currentProfileIndex);
  int y = LIST_Y + (row * ROW_H);

  uint16_t bg = isSel ? DARK_GRAY : TFT_BLACK;
  uint16_t fg = isSel ? UI_WARN : UI_DIM_TEXT;
  tft.fillRect(LIST_X, y, LIST_W, ROW_H - 1, bg);
  tft.setTextColor(fg, bg);
  tft.setCursor(LIST_X + 2, y + 4);
  tft.printf("%2d.", (int)globalIdx + 1);
  tft.setCursor(LIST_X + 34, y + 4);

  if (cachedOk[row]) {

    char nameBuf[17];
    memcpy(nameBuf, cachedPage[row].name, 16);
    nameBuf[16] = '\0';
    String nm = String(nameBuf);
    if (nm.length() > 10) nm = nm.substring(0, 10);
    tft.print(nm);

    char fbuf[16];
    snprintf(fbuf, sizeof(fbuf), "%.2f", cachedPage[row].frequency / 1000000.0);
    int tw = tft.textWidth(fbuf, 1);
    tft.setCursor(LIST_X + LIST_W - 4 - tw, y + 4);
    tft.print(fbuf);
  } else {
    tft.print("<?>");
  }
}

static void drawListPage(uint16_t pageStart) {
  ensurePageCache();

  tft.fillRect(LIST_X, LIST_Y, LIST_W, (ITEMS_PER_PAGE * ROW_H), TFT_BLACK);
  for (uint8_t row = 0; row < ITEMS_PER_PAGE; row++) {
    if ((uint16_t)(pageStart + row) >= sdTotalProfiles) break;
    drawRow(pageStart, row);
  }
}

static void drawDetails() {
  const int detailsY = profileDetailsY();
  const int gapTop = profileListBottom();
  const int gapH = profileBottomY() - gapTop;
  if (gapH > 0) {
    tft.fillRect(LIST_X, gapTop, LIST_W, gapH, TFT_BLACK);
  }
  tft.drawFastHLine(LIST_X, profileListBottom(), LIST_W, UI_LINE);

  String err;
  if (!selectedValid) {
    loadSelectedFromSd(&err);
  }

  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  if (!selectedValid) {
    tft.setCursor(PROFILE_LABEL_X, detailsY);
    tft.print("Read failed:");
    tft.setCursor(PROFILE_VALUE_X, detailsY);
    tft.print(err);
    return;
  }

  tft.setCursor(PROFILE_LABEL_X, detailsY);
  tft.print("Name:");
  tft.setCursor(PROFILE_VALUE_X, detailsY);
  tft.print(selectedProfile.name);

  tft.setCursor(PROFILE_LABEL_X, detailsY + PROFILE_LINE_STEP);
  tft.print("Freq:");
  tft.setCursor(PROFILE_VALUE_X, detailsY + PROFILE_LINE_STEP);
  tft.printf("%.2f MHz", selectedProfile.frequency / 1000000.0);
  tft.setCursor(PROFILE_COL2_LABEL_X, detailsY + PROFILE_LINE_STEP);
  tft.print("Ptc:");
  tft.setCursor(PROFILE_COL2_VALUE_X, detailsY + PROFILE_LINE_STEP);
  /* 0 is not an rc-switch protocol number, it is the absence of one: a .sub
   * whose protocol name this firmware will not guess at. Printing the digit
   * made that look like an answer. */
  if (selectedProfile.protocol == 0) {
    tft.setTextColor(UI_WARN);
    tft.print("none");
    tft.setTextColor(UI_TEXT);
  } else {
    tft.print(selectedProfile.protocol);
  }

  tft.setCursor(PROFILE_LABEL_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print("Val:");
  tft.setCursor(PROFILE_VALUE_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print((unsigned long)selectedProfile.value);
  tft.setCursor(PROFILE_COL2_LABEL_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print("Bit:");
  tft.setCursor(PROFILE_COL2_VALUE_X, detailsY + (PROFILE_LINE_STEP * 2));
  tft.print(selectedProfile.bitLength);

  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(PROFILE_LABEL_X, detailsY + (PROFILE_LINE_STEP * 3));
  tft.print("SRC:");
  tft.setCursor(PROFILE_VALUE_X, detailsY + (PROFILE_LINE_STEP * 3));
  if (selectedPath.endsWith("profiles_current.bin")) {
    tft.print("current");
  } else {
    const int slash = selectedPath.lastIndexOf('/');
    tft.print(slash >= 0 ? selectedPath.substring(slash + 1) : selectedPath);
  }

  if (deleteArmed && (int32_t)(millis() - deleteArmUntilMs) < 0) {
    int hintY = detailsY + (PROFILE_LINE_STEP * 4);
    if (hintY >= profileBottomY() - 12) {
      hintY = profileBottomY() - 12;
    }
    tft.setCursor(PROFILE_LABEL_X, hintY);
    tft.setTextColor(UI_WARN, TFT_BLACK);
    tft.print("Press Delete again to confirm");
  }
}

static void updateSelectionUI(uint16_t oldIndex, bool forceListRedraw) {
  if (sdTotalProfiles == 0) return;
  uint16_t oldPage = pageStartForIndex(oldIndex);
  uint16_t newPage = pageStartForIndex(currentProfileIndex);

  tft.startWrite();
  drawHeaderLine();

  if (forceListRedraw || oldPage != newPage) {
    drawListPage(newPage);
  } else {

    uint8_t oldRow = (uint8_t)(oldIndex - oldPage);
    uint8_t newRow = (uint8_t)(currentProfileIndex - newPage);
    ensurePageCache();

    drawRow(newPage, oldRow);
    drawRow(newPage, newRow);
  }

  drawDetails();
  tft.endWrite();
}

void updateDisplay() {

    tft.startWrite();
    const int bodyH = subghzContentBottom() - 40;
    if (bodyH > 0) {
      tft.fillRect(0, 40, PUEO_SCREEN_W, bodyH, TFT_BLACK);
    }

    if (sdTotalProfiles == 0) {
        tft.setTextSize(1);
        tft.setCursor(PROFILE_LABEL_X, PROFILE_HEADER_Y + PROFILE_LINE_H);
        tft.setTextColor(UI_TEXT, TFT_BLACK);
        if (sdLastErr.indexOf("SD not mounted") >= 0) {
          tft.print("SD card not inserted.");
        } else {
          tft.print("No profiles on SD.");
        }
        if (sdLastErr.length()) {
          tft.setCursor(PROFILE_LABEL_X, PROFILE_HEADER_Y + (PROFILE_LINE_H * 2));
          tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
          tft.print(sdLastErr);
        }
        tft.endWrite();
        return;
    }

    drawHeaderLine();
    drawListPage(pageStartForIndex(currentProfileIndex));
    drawDetails();
    tft.endWrite();
}

void transmitProfile(int index) {
    (void)index;

    /* Both gates live here rather than on the screen, because this is the
     * only thing in Saved Profile that drives the radio. Each returns to
     * the list rather than closing it. */
    if (Stealth::refuseAction("Sending a saved profile")) {
        profileRestoreChrome();
        return;
    }
    if (!cc1101ReadyForAction("Sending a profile")) {
        profileRestoreChrome();
        return;
    }

    String err;
    loadSelectedFromSd(&err);
    if (!selectedValid) return;
    Profile profileToSend = selectedProfile;

    /* rc-switch clamps anything below 1 up to 1, so sending a profile with
     * no mapping would transmit Princeton timing under another protocol's
     * name: no result, and no reason given. Refuse instead. */
    if (profileToSend.protocol == 0) {
      profileClearContentArea(TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.setTextColor(UI_WARN);
      tft.print("No protocol mapping");
      tft.setTextColor(TFT_WHITE);
      tft.setCursor(10, 50 + yshift);
      tft.print("This profile came from a .sub whose");
      tft.setCursor(10, 66 + yshift);
      tft.print("protocol this firmware will not guess");
      tft.setCursor(10, 82 + yshift);
      tft.print("at. Sending it would transmit the");
      tft.setCursor(10, 98 + yshift);
      tft.print("wrong timing.");
      delay(2600);
      profileRestoreChrome();
      return;
    }

    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setMHZ(profileToSend.frequency / 1000000.0);

    mySwitch.disableReceive();
    delay(100);
    pinMode(SUBGHZ_TX_PIN, OUTPUT);
    mySwitch.enableTransmit(SUBGHZ_TX_PIN);
    ELECHOUSE_cc1101.SetTx();

    profileClearContentArea(TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.setTextColor(TFT_WHITE);
    tft.print("Sending ");
    tft.print(profileToSend.name);
    tft.print("...");
    tft.setCursor(10, 50 + yshift);
    tft.print("Value: ");
    tft.print(profileToSend.value);

    mySwitch.setProtocol(profileToSend.protocol);
    mySwitch.send(profileToSend.value, profileToSend.bitLength);

    delay(500);
    profileClearContentArea(TFT_BLACK);
    tft.setCursor(10, 30 + yshift);
    tft.print("Done!");

    mySwitch.disableTransmit();
    pinMode(SUBGHZ_TX_PIN, INPUT);
    pinMode(SUBGHZ_RX_PIN, INPUT);
    ELECHOUSE_cc1101.SetRx();
    delay(50);
    mySwitch.enableReceive(SUBGHZ_RX_PIN);

    delay(500);
    profileRestoreChrome();
}

void loadProfileCount() {

    refreshSdIndex(true);
}

void printProfiles() {
    refreshSdIndex(false);
}

void deleteProfile(int index) {
    (void)index;
    if (sdTotalProfiles == 0) return;
    String err;
    loadSelectedFromSd(&err);
    if (!selectedValid) return;

    String path = selectedPath;
    uint16_t local = selectedLocalIdx;

    uint32_t now = millis();
    if (!deleteArmed || (int32_t)(now - deleteArmUntilMs) >= 0) {
      deleteArmed = true;
      deleteArmUntilMs = now + 3000;
      updateDisplay();
      return;
    }
    deleteArmed = false;

    if (!deleteProfileFromFile(path, local, &err)) {
      profileClearContentArea(TFT_BLACK);
      tft.setCursor(10, 30 + yshift);
      tft.setTextColor(UI_WARN);
      tft.print("Delete FAILED");
      tft.setCursor(10, 45 + yshift);
      tft.setTextColor(TFT_WHITE);
      tft.print(err);
      delay(1200);
      profileRestoreChrome();
      return;
    }

    refreshSdIndex(false);
    if (sdTotalProfiles == 0) currentProfileIndex = 0;
    else if (currentProfileIndex >= sdTotalProfiles) currentProfileIndex = (uint16_t)(sdTotalProfiles - 1);
    selectedValid = false;
    cacheDirty = true;
    updateDisplay();
}

void runUI() {
    #undef STATUS_BAR_Y_OFFSET
    constexpr int STATUS_BAR_Y_OFFSET = 20;
    constexpr int STATUS_BAR_HEIGHT = 16;
    constexpr int ICON_SIZE = 16;
    constexpr int ICON_NUM = 4;

    static int iconX[ICON_NUM] = {PUEO_SCREEN_W - 110, PUEO_SCREEN_W - 70, PUEO_SCREEN_W - 30, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_antenna,
        bitmap_icon_recycle,
        bitmap_icon_undo,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_ICON);
            }
        }
        tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
        tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, PUEO_SCREEN_W, UI_LINE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_ICON);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                    if (sdTotalProfiles > 0) {
                        transmitProfile(currentProfileIndex);
                    }
                    break;
                case 1:
                    if (sdTotalProfiles > 0) {
                        deleteProfile(currentProfileIndex);
                    }
                    break;
                case 2: {
                    refreshSdIndex(true);
                    selectedValid = false;
                    cacheDirty = true;
                    deleteArmed = false;
                    updateDisplay();
                    break;
                }
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        int x, y;
        if (feature_active && readTouchXY(x, y)) {
            if (y >= LIST_Y && y < (LIST_Y + (ITEMS_PER_PAGE * ROW_H)) && x >= LIST_X && x < (LIST_X + LIST_W)) {
              uint8_t row = (uint8_t)((y - LIST_Y) / ROW_H);
              uint16_t oldIdx = currentProfileIndex;
              uint16_t start = pageStartForIndex(currentProfileIndex);
              uint16_t idx = (uint16_t)(start + row);
              if (idx < sdTotalProfiles) {
                currentProfileIndex = idx;
                selectedValid = false;
                cacheDirty = true;
                deleteArmed = false;
                updateSelectionUI(oldIdx, false);
              }
            }
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 3) {
                                feature_exit_requested = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                                animationState = 1;
                                activeIcon = i;
                                lastAnimationTime = millis();
                            }
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
}

void saveSetup() {
    /* No gate on the screen. It lists profiles off the card, shows one and
     * deletes one, none of which touches the radio, and transmitProfile()
     * refuses on its own behalf. Gating here meant a profile imported from
     * a .sub file could not be read back on a board with no module fitted. */
    Serial.begin(115200);
    setTouchButtonInputEnabled(true);
    subghzSetProfileNavLabels();

    /* Brought up only when it is both fitted and allowed. Under Stealth
     * Mode nothing here may transmit, so the part stays unpowered rather
     * than sitting in RX for a screen that is only going to read the card.
     * cc1101Present() hands the bus back to the touch controller when
     * nothing answers, so the false path leaves SPI where SD wants it. */
    const bool radioUp = !Stealth::on() && cc1101Present();

    if (radioUp) {
      // No reclaim on this path: before arbitration the CC1101 inherited
      // whatever clock the last feature left on the bus.
      SpiBus::claim(SpiBus::Dev::Cc1101);
      ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI,
                                 CC1101_CS);
      ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
    }

    EEPROM.begin(EEPROM_SIZE);
    loadProfileCount();
    printProfiles();

#if HAS_PCF8574_BUTTONS
    pcf.pinMode(BTN_UP, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
    pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

    subghzClearBody(TFT_BLACK);
    tft.setTextColor(UI_TEXT);

    setupTouchscreen();

    float currentBatteryVoltage = readBatteryVoltage();
    drawStatusBar(currentBatteryVoltage, true);
    subghzRedrawNavChrome();
    uiDrawn = false;

    if (radioUp) {
      ELECHOUSE_cc1101.Init();
      ELECHOUSE_cc1101.setCCMode(0);
      ELECHOUSE_cc1101.setModulation(2);
      pinMode(SUBGHZ_RX_PIN, INPUT);
      pinMode(SUBGHZ_TX_PIN, INPUT);
      ELECHOUSE_cc1101.SetRx();

      mySwitch.enableReceive(SUBGHZ_RX_PIN);
      mySwitch.setRepeatTransmit(8);
    }

    refreshSdIndex(false);
    cacheDirty = true;
    deleteArmed = false;
    updateDisplay();
    uiDrawn = false;
    runUI();
    subghzRedrawNavChrome();
}

void saveLoop() {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
        feature_exit_requested = true;
        return;
    }

    maintainTouchNavBar();
    runUI();
    profileHandleNavButtons();

    static unsigned long lastDebounceTime = 0;
    const unsigned long debounceDelay = 200;

    static bool prevUp = false;
    static bool prevDown = false;
    static bool prevRight = false;
    static bool prevLeft = false;
    const bool prevPressed    = isPhysicalButtonPressed(BTN_UP);
    const bool nextPressed    = isPhysicalButtonPressed(BTN_DOWN);
    const bool txPressed      = isPhysicalButtonPressed(BTN_RIGHT);
    const bool deletePressed = isPhysicalButtonPressed(BTN_LEFT);

    if (sdTotalProfiles > 0) {

        if (nextPressed && !prevDown && millis() - lastDebounceTime > debounceDelay) {
            profileSelectNext();
            lastDebounceTime = millis();
        }

        if (prevPressed && !prevUp && millis() - lastDebounceTime > debounceDelay) {
            profileSelectPrev();
            lastDebounceTime = millis();
        }

        if (txPressed && !prevRight && millis() - lastDebounceTime > debounceDelay) {
            transmitProfile(currentProfileIndex);
            lastDebounceTime = millis();
        }

        if (deletePressed && !prevLeft && millis() - lastDebounceTime > debounceDelay) {
            deleteProfile(currentProfileIndex);
            lastDebounceTime = millis();
        }
    }

    prevUp = prevPressed;
    prevDown = nextPressed;
    prevRight = txPressed;
    prevLeft = deletePressed;
}

}

namespace subjammer {

void updateDisplay();

static bool uiDrawn = false;

static unsigned long lastDebounceTime = 0;
const unsigned long debounceDelay = 200;

constexpr int SCREEN_WIDTH = PUEO_SCREEN_W;

static constexpr uint8_t JAM_BTN_LEFT  = 4;
static constexpr uint8_t JAM_BTN_RIGHT = 5;
static constexpr uint8_t JAM_BTN_DOWN  = 3;
static constexpr uint8_t JAM_BTN_UP    = 6;

bool jammingRunning = false;
bool continuousMode = true;
bool autoMode = false;
unsigned long lastSweepTime = 0;
const unsigned long sweepInterval = 1000;

const int numFrequencies = (int)kSubghzFreqCount;
int currentFrequencyIndex = 5;
float targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;

static constexpr int kJammerStatusLineY = 79;
static constexpr int kJammerYSHIFT = 20;
static constexpr int kJammerValueLineH = 11;
static constexpr int kJammerProgressY = 60 + kJammerYSHIFT;

static bool s_jammerStaticDrawn = false;

struct JammerDisplayCache {
  bool valid = false;
  int freqMHz100 = -1;
  bool autoMode = false;
  bool continuousMode = false;
  bool jammingRunning = false;
  int progress = -1;
  bool blinkOn = false;
};

static JammerDisplayCache s_jammerDisp;

static void jammerInvalidateDisplay() {
  s_jammerStaticDrawn = false;
  s_jammerDisp = JammerDisplayCache{};
}

static void subjammerToggleJam() {
  jammingRunning = !jammingRunning;
  if (jammingRunning) {
    Serial.println("Jamming started");
    ELECHOUSE_cc1101.setMHZ(targetFrequency);
    ELECHOUSE_cc1101.SetTx();
  } else {
    Serial.println("Jamming stopped");
    ELECHOUSE_cc1101.setSidle();
    digitalWrite(TX_PIN, LOW);
  }
  updateDisplay();
  lastDebounceTime = millis();
}

static void subjammerFreqNext() {
  if (autoMode) {
    return;
  }
  currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
  ELECHOUSE_cc1101.setMHZ(targetFrequency);
  updateDisplay();
  lastDebounceTime = millis();
}

static void subjammerFreqPrev() {
  if (autoMode) {
    return;
  }
  currentFrequencyIndex = (currentFrequencyIndex - 1 + numFrequencies) % numFrequencies;
  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
  ELECHOUSE_cc1101.setMHZ(targetFrequency);
  updateDisplay();
  lastDebounceTime = millis();
}

static void subjammerApplyFrequency() {
  ELECHOUSE_cc1101.setMHZ(targetFrequency);
  if (jammingRunning) {
    ELECHOUSE_cc1101.SetTx();
  } else {
    ELECHOUSE_cc1101.setSidle();
    digitalWrite(TX_PIN, LOW);
  }
}

static void subjammerAutoSweepIfDue() {
  if (!autoMode || millis() - lastSweepTime < sweepInterval) {
    return;
  }

  currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
  subjammerApplyFrequency();
  updateDisplay();
  lastSweepTime = millis();
}

static void subjammerToggleAuto() {
  autoMode = !autoMode;
  Serial.print("Frequency mode: ");
  Serial.println(autoMode ? "Automatic" : "Manual");
  if (autoMode) {
    currentFrequencyIndex = 0;
    targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
    lastSweepTime = millis();
    subjammerApplyFrequency();
    s_jammerDisp.freqMHz100 = -1;
  }
  updateDisplay();
  lastDebounceTime = millis();
}

void subjammerHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    subjammerToggleJam();
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    subjammerFreqPrev();
    subghzWaitNavRelease(BTN_LEFT);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    subjammerFreqNext();
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    subjammerToggleAuto();
    subghzWaitNavRelease(BTN_DOWN);
  }
}

static void jammerDrawStatusSeparator() {
  tft.drawFastHLine(0, kJammerStatusLineY, PUEO_SCREEN_W, UI_LINE);
}

static void jammerDrawValueCell(int x, int y, int w, int h, const String& text, uint16_t color) {
  const int maxH = kJammerStatusLineY - y;
  if (maxH <= 0) {
    return;
  }
  const int clipH = min(h, maxH);
  tft.fillRect(x, y, w, clipH, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(text);
}

static void jammerDrawStaticChrome() {
  if (s_jammerStaticDrawn) {
    return;
  }

  const int bodyBottom = subghzContentBottom();
  const int bodyH = min(kJammerStatusLineY - 40, bodyBottom - 40);
  if (bodyH > 0) {
    tft.fillRect(0, 40, PUEO_SCREEN_W, bodyH, TFT_BLACK);
  }
  jammerDrawStatusSeparator();

  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(5, 22 + kJammerYSHIFT);
  tft.print("Freq:");
  tft.setCursor(130, 22 + kJammerYSHIFT);
  tft.print("Mode:");
  tft.setCursor(5, 42 + kJammerYSHIFT);
  tft.print("Status:");

  s_jammerStaticDrawn = true;
}

static void jammerDrawProgressBar(int progress) {
  tft.fillRect(0, kJammerProgressY, PUEO_SCREEN_W, 4, TFT_BLACK);
  if (progress > 0) {
    tft.fillRect(0, kJammerProgressY, progress, 4, UI_WARN);
  }
}

static void jammerDrawBlinkDot(bool on) {
  const int cx = 220;
  const int cy = 22 + kJammerYSHIFT;
  const int r = 2;
  if (on) {
    tft.fillCircle(cx, cy, r, UI_WARN);
  } else {
    tft.fillRect(cx - r, cy - r, r * 2 + 1, r * 2 + 1, TFT_BLACK);
  }
}

static void jammerPollBlinkIndicator() {
  const bool wantBlink = autoMode && jammingRunning;
  const bool blinkOn = wantBlink && ((millis() % 1000) < 500);
  if (!s_jammerDisp.valid) {
    return;
  }
  if (blinkOn != s_jammerDisp.blinkOn) {
    jammerDrawBlinkDot(blinkOn);
    s_jammerDisp.blinkOn = blinkOn;
  }
}

void updateDisplay() {
    jammerDrawStaticChrome();

    char freqBuf[20];
    char modeBuf[8];
    char statusBuf[8];

    if (autoMode) {
      snprintf(freqBuf, sizeof(freqBuf), "Auto:%.1f", targetFrequency);
    } else {
      snprintf(freqBuf, sizeof(freqBuf), "%.2f MHz", targetFrequency);
    }
    snprintf(modeBuf, sizeof(modeBuf), "%s", continuousMode ? "Cont" : "Noise");
    snprintf(statusBuf, sizeof(statusBuf), "%s", jammingRunning ? "Jamming" : "Idle   ");

    const int freqKey = (int)(targetFrequency * 100.0f + 0.5f);
    const bool fullRedraw = !s_jammerDisp.valid;
    if (fullRedraw || s_jammerDisp.freqMHz100 != freqKey ||
        s_jammerDisp.autoMode != autoMode) {
      jammerDrawValueCell(40, 22 + kJammerYSHIFT, 96, kJammerValueLineH, freqBuf,
                          autoMode ? UI_WARN : UI_TEXT);
      s_jammerDisp.freqMHz100 = freqKey;
      s_jammerDisp.autoMode = autoMode;
    }

    if (fullRedraw || s_jammerDisp.continuousMode != continuousMode) {
      jammerDrawValueCell(165, 22 + kJammerYSHIFT, 40, kJammerValueLineH, modeBuf,
                          continuousMode ? UI_WARN : UI_TEXT);
      s_jammerDisp.continuousMode = continuousMode;
    }

    if (fullRedraw || s_jammerDisp.jammingRunning != jammingRunning) {
      jammerDrawValueCell(50, 42 + kJammerYSHIFT, 72, kJammerValueLineH, statusBuf,
                          jammingRunning ? UI_WARN : UI_TEXT);
      s_jammerDisp.jammingRunning = jammingRunning;
    }

    if (autoMode) {
      const int progress = ::map(currentFrequencyIndex, 0, numFrequencies - 1, 0, 240);
      if (fullRedraw || s_jammerDisp.progress != progress) {
        jammerDrawProgressBar(progress);
        s_jammerDisp.progress = progress;
      }
    } else if (s_jammerDisp.progress != -1) {
      jammerDrawProgressBar(0);
      s_jammerDisp.progress = -1;
      if (s_jammerDisp.blinkOn) {
        jammerDrawBlinkDot(false);
        s_jammerDisp.blinkOn = false;
      }
    }

    jammerDrawStatusSeparator();
    s_jammerDisp.valid = true;
}

void runUI() {
    constexpr int SCREEN_WIDTH = PUEO_SCREEN_W;
    #define SCREENHEIGHT PUEO_SCREEN_H
    #undef STATUS_BAR_Y_OFFSET
    constexpr int STATUS_BAR_Y_OFFSET = 20;
    constexpr int STATUS_BAR_HEIGHT = 16;
    constexpr int ICON_SIZE = 16;
    constexpr int ICON_NUM = 6;

    static int iconX[ICON_NUM] = {PUEO_SCREEN_W - 190, PUEO_SCREEN_W - 150, PUEO_SCREEN_W - 110, PUEO_SCREEN_W - 70, PUEO_SCREEN_W - 30, 10};
    static int iconY = STATUS_BAR_Y_OFFSET;

    static const unsigned char* icons[ICON_NUM] = {
        bitmap_icon_power,
        bitmap_icon_antenna,
        bitmap_icon_random,
        bitmap_icon_sort_down_minus,
        bitmap_icon_sort_up_plus,
        bitmap_icon_go_back
    };

    if (!uiDrawn) {
        tft.fillRect(0, STATUS_BAR_Y_OFFSET, SCREEN_WIDTH, STATUS_BAR_HEIGHT, DARK_GRAY);

        for (int i = 0; i < ICON_NUM; i++) {
            if (icons[i] != NULL) {
                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, UI_ICON);
            }
        }
        tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
        tft.drawFastHLine(0, STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT, PUEO_SCREEN_W, UI_LINE);
        uiDrawn = true;
    }

    static unsigned long lastAnimationTime = 0;
    static int animationState = 0;
    static int activeIcon = -1;

    if (animationState > 0 && millis() - lastAnimationTime >= 150) {
        if (animationState == 1) {
            tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], ICON_SIZE, ICON_SIZE, UI_ICON);
            animationState = 2;

            switch (activeIcon) {
                case 0:
                  jammingRunning = !jammingRunning;
                    if (jammingRunning) {
                        Serial.println("Jamming started");
                        ELECHOUSE_cc1101.setMHZ(targetFrequency);
                        ELECHOUSE_cc1101.SetTx();
                    } else {
                        Serial.println("Jamming stopped");
                        ELECHOUSE_cc1101.setSidle();
                        digitalWrite(TX_PIN, LOW);
                    }
                    updateDisplay();
                    lastDebounceTime = millis();
                    break;
                case 1:
                 continuousMode = !continuousMode;
                  Serial.print("Jamming mode: ");
                  Serial.println(continuousMode ? "Continuous Carrier" : "Noise");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 2:
                  autoMode = !autoMode;
                  Serial.print("Frequency mode: ");
                  Serial.println(autoMode ? "Automatic" : "Manual");
                  if (autoMode) {
                      currentFrequencyIndex = 0;
                      targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                      lastSweepTime = millis();
                      subjammerApplyFrequency();
                      s_jammerDisp.freqMHz100 = -1;
                  }
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 3:
                  currentFrequencyIndex = (currentFrequencyIndex - 1 + numFrequencies) % numFrequencies;
                  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                  ELECHOUSE_cc1101.setMHZ(targetFrequency);
                  Serial.print("Switched to: ");
                  Serial.print(targetFrequency);
                  Serial.println(" MHz");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                 case 4:
                  currentFrequencyIndex = (currentFrequencyIndex + 1) % numFrequencies;
                  targetFrequency = subghz_frequency_list[currentFrequencyIndex] / 1000000.0;
                  ELECHOUSE_cc1101.setMHZ(targetFrequency);
                  Serial.print("Switched to: ");
                  Serial.print(targetFrequency);
                  Serial.println(" MHz");
                  updateDisplay();
                  lastDebounceTime = millis();
                    break;
                case 5:
                    feature_exit_requested = true;
                    break;
            }
        } else if (animationState == 2) {
            animationState = 0;
            activeIcon = -1;
        }
        lastAnimationTime = millis();
    }

    static unsigned long lastTouchCheck = 0;
    const unsigned long touchCheckInterval = 50;

    if (millis() - lastTouchCheck >= touchCheckInterval) {
        int x, y;
        if (feature_active && readTouchXY(x, y)) {
            if (y > STATUS_BAR_Y_OFFSET && y < STATUS_BAR_Y_OFFSET + STATUS_BAR_HEIGHT) {
                for (int i = 0; i < ICON_NUM; i++) {
                    if (x > iconX[i] && x < iconX[i] + ICON_SIZE) {
                        if (icons[i] != NULL && animationState == 0) {

                            if (i == 5) {
                                feature_exit_requested = true;
                            } else {

                                tft.drawBitmap(iconX[i], iconY, icons[i], ICON_SIZE, ICON_SIZE, TFT_BLACK);
                                animationState = 1;
                                activeIcon = i;
                                lastAnimationTime = millis();
                            }
                        }
                        break;
                    }
                }
            }
        }
        lastTouchCheck = millis();
    }
#undef SCREEN_WIDTH
#undef SCREENHEIGHT
#undef STATUS_BAR_Y_OFFSET
#undef STATUS_BAR_HEIGHT
#undef ICON_SIZE
#undef ICON_NUM
}

void subjammerSetup() {
  if (Stealth::refuse("SubGHz Jammer")) return;

  if (!cc1101Ready("Sub-GHz Jammer")) return;
    Serial.begin(115200);
    setTouchButtonInputEnabled(true);
    subghzSetJammerNavLabels();
    subghzClearBody(TFT_BLACK);
    drawStatusBar(readBatteryVoltage(), true);
    subghzRedrawNavChrome();

    // As in saveSetup(): this path never reclaimed the bus either.
    SpiBus::claim(SpiBus::Dev::Cc1101);
    ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);

    ELECHOUSE_cc1101.Init();
    ELECHOUSE_cc1101.setModulation(0);
    ELECHOUSE_cc1101.setRxBW(500.0);
    ELECHOUSE_cc1101.setPA(12);
    ELECHOUSE_cc1101.setMHZ(targetFrequency);
    ELECHOUSE_cc1101.SetTx();

    /* esp_random(), not analogRead(0): an unconnected ADC pin can sit
     * at a stable value, and the jammer's noise payload would then be the
     * same sequence on every boot. Noise that repeats is a signature. */
    randomSeed(esp_random());

#if HAS_PCF8574_BUTTONS
    pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
    pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
    pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
    pcf.pinMode(BTN_UP, INPUT_PULLUP);
#endif
    delay(100);

    subghzClearBody(TFT_BLACK);
    drawStatusBar(readBatteryVoltage(), true);

    setupTouchscreen();

   jammerInvalidateDisplay();
   updateDisplay();
   uiDrawn = false;
   subghzRedrawNavChrome();
}

void subjammerLoop() {

    if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
        feature_exit_requested = true;
        return;
    }

    maintainTouchNavBar();
    runUI();
    if (uiDrawn) {
      tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
      tft.drawFastHLine(0, 36, PUEO_SCREEN_W, UI_LINE);
      if (s_jammerDisp.valid) {
        jammerDrawStatusSeparator();
      }
    }
    jammerPollBlinkIndicator();
    subjammerHandleNavButtons();

#if HAS_PCF8574_BUTTONS
    int btnLeftState = pcf.digitalRead(JAM_BTN_LEFT);
    int btnRightState = pcf.digitalRead(JAM_BTN_RIGHT);
    int btnUpState = pcf.digitalRead(JAM_BTN_UP);
    int btnDownState = pcf.digitalRead(JAM_BTN_DOWN);
#else
    int btnLeftState = isPhysicalButtonPressed(BTN_LEFT) ? LOW : HIGH;
    int btnRightState = isPhysicalButtonPressed(BTN_RIGHT) ? LOW : HIGH;
    int btnUpState = isPhysicalButtonPressed(BTN_UP) ? LOW : HIGH;
    int btnDownState = isPhysicalButtonPressed(BTN_DOWN) ? LOW : HIGH;
#endif

    if (btnUpState == LOW && millis() - lastDebounceTime > debounceDelay) {
        subjammerToggleJam();
    }

    if (btnRightState == LOW && !autoMode && millis() - lastDebounceTime > debounceDelay) {
        subjammerFreqNext();
    }

    if (btnLeftState == LOW && !autoMode && millis() - lastDebounceTime > debounceDelay) {
        subjammerFreqPrev();
    }

    if (btnDownState == LOW && millis() - lastDebounceTime > debounceDelay) {
        subjammerToggleAuto();
    }

    subjammerAutoSweepIfDue();

    if (jammingRunning) {
        ELECHOUSE_cc1101.SetTx();

        if (continuousMode) {
            ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, 0xFF);
            ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
            digitalWrite(TX_PIN, HIGH);
        } else {
            for (int i = 0; i < 10; i++) {
                uint32_t noise = random(16777216);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, noise >> 16);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, (noise >> 8) & 0xFF);
                ELECHOUSE_cc1101.SpiWriteReg(CC1101_TXFIFO, noise & 0xFF);
                ELECHOUSE_cc1101.SpiStrobe(CC1101_STX);
                delayMicroseconds(50);
              }
          }
      }
  }

/* Leaving the jammer used to leave the radio in TX.
 *
 * Nothing in this namespace had an exit and the dispatch called none, so the
 * only way to stop transmitting was to press stop before backing out:
 * jammingRunning stayed true, the part stayed in TX, and in continuousMode
 * TX_PIN stayed HIGH. Whether it was still emitting once the loop stopped
 * refilling the FIFO wants an SDR to answer, and the answer does not change
 * what this should do. An off switch you have to remember is not one.
 *
 * Same three lines subjammerToggleJam() runs when you press stop. */
void exit() {
  jammingRunning = false;
  ELECHOUSE_cc1101.setSidle();
  digitalWrite(TX_PIN, LOW);
}

}

namespace SubBrute {

static constexpr uint8_t BRUTE_TX_PIN = SUBGHZ_TX_PIN;

static const uint32_t kBruteFreqList[] = {
    300000000, 303875000, 304250000, 310000000, 314000000, 315000000,
    318000000, 390000000, 418000000, 433075000, 433420000, 433920000,
    434420000, 434775000, 438900000, 868350000, 915000000, 925000000
};
static constexpr int kBruteFreqCount =
    (int)(sizeof(kBruteFreqList) / sizeof(kBruteFreqList[0]));

static const uint8_t kBitsChoices[] = {8, 10, 12, 16, 18, 20, 24};
static constexpr int kBitsChoiceCount =
    (int)(sizeof(kBitsChoices) / sizeof(kBitsChoices[0]));

static const uint16_t kPulseChoicesUs[] = {250, 350, 400, 500};
static constexpr int kPulseChoiceCount =
    (int)(sizeof(kPulseChoicesUs) / sizeof(kPulseChoicesUs[0]));

static constexpr int kBarBottom = 36;
static constexpr int kPanelTop = 44;
static constexpr int kBoxHeaderH = 14;
static constexpr int kLineH = 14;
static constexpr int kPanelPadX = 4;
static constexpr int kPanelW = 240 - (kPanelPadX * 2);
static constexpr int kLabelX = 12;
static constexpr int kValueX = 58;
static constexpr int kValueW = 170;
static constexpr int kSettingsRows = 4;
static constexpr int kSettingsInnerH = kBoxHeaderH + (kSettingsRows * kLineH) + 4;
static constexpr int kProgressInnerH = kBoxHeaderH + (kLineH * 2) + 16;
static constexpr int kRadius = 3;

enum FocusRow : uint8_t { FOCUS_FREQ = 0, FOCUS_BITS, FOCUS_MODE, FOCUS_OPT, FOCUS_COUNT };
enum RunMode : uint8_t { MODE_DEBRUIJN = 0, MODE_BRUTE = 1 };
enum RunState : uint8_t { ST_IDLE = 0, ST_RUNNING, ST_DONE, ST_STOPPED };

static RCSwitch s_switch;
static bool s_uiDrawn = false;
static unsigned long s_lastDebounce = 0;
static constexpr unsigned long kDebounceMs = 200;

static int s_freqIndex = 11;  // 433.92 MHz
static int s_bitsIndex = 2;   // 12-bit
static int s_pulseIndex = 1;  // 350 us
static int s_protocol = 1;
static FocusRow s_focus = FOCUS_FREQ;
static RunMode s_mode = MODE_DEBRUIJN;
static RunState s_runState = ST_IDLE;
static bool s_stopRequested = false;
static bool s_running = false;

static uint32_t s_progressDone = 0;
static uint32_t s_progressTotal = 0;
static uint32_t s_lastUiProgress = 0xFFFFFFFFu;
static uint8_t s_lastUiPct = 255;
static RunState s_lastUiState = ST_IDLE;
static FocusRow s_lastUiFocus = FOCUS_COUNT;
static RunMode s_lastUiMode = MODE_BRUTE;
static int s_lastUiFreq = -1;
static int s_lastUiBits = -1;
static int s_lastUiPulse = -1;
static int s_lastUiProto = -1;
static bool s_chromeDrawn = false;

static float bruteFreqMHz() {
  return kBruteFreqList[s_freqIndex % kBruteFreqCount] / 1000000.0f;
}

static uint8_t bruteBits() {
  return kBitsChoices[s_bitsIndex % kBitsChoiceCount];
}

static uint16_t brutePulseUs() {
  return kPulseChoicesUs[s_pulseIndex % kPulseChoiceCount];
}

static int settingsPanelY() { return kPanelTop; }
static int settingsPanelH() { return kSettingsInnerH; }
static int progressPanelY() { return kPanelTop + kSettingsInnerH + 6; }
static int progressPanelH() { return kProgressInnerH; }
static int hintY0() { return progressPanelY() + kProgressInnerH + 6; }

static int rowTextY(int row) {
  return settingsPanelY() + kBoxHeaderH + 2 + row * kLineH;
}

// Fibonacci LFSR feedback: taps are 1-indexed from LSB.
static uint32_t bruteLfsrFeedback(uint8_t n, uint32_t state) {
  uint32_t fb = 0;
  switch (n) {
    case 8:
      fb ^= (state >> (8 - 1)) & 1u;
      fb ^= (state >> (6 - 1)) & 1u;
      fb ^= (state >> (5 - 1)) & 1u;
      fb ^= (state >> (4 - 1)) & 1u;
      break;
    case 10:
      fb ^= (state >> (10 - 1)) & 1u;
      fb ^= (state >> (7 - 1)) & 1u;
      break;
    case 12:
      fb ^= (state >> (12 - 1)) & 1u;
      fb ^= (state >> (11 - 1)) & 1u;
      fb ^= (state >> (8 - 1)) & 1u;
      fb ^= (state >> (6 - 1)) & 1u;
      break;
    case 16:
      fb ^= (state >> (16 - 1)) & 1u;
      fb ^= (state >> (14 - 1)) & 1u;
      fb ^= (state >> (13 - 1)) & 1u;
      fb ^= (state >> (11 - 1)) & 1u;
      break;
    case 18:
      fb ^= (state >> (18 - 1)) & 1u;
      fb ^= (state >> (11 - 1)) & 1u;
      break;
    case 20:
      fb ^= (state >> (20 - 1)) & 1u;
      fb ^= (state >> (17 - 1)) & 1u;
      break;
    case 24:
      fb ^= (state >> (24 - 1)) & 1u;
      fb ^= (state >> (23 - 1)) & 1u;
      fb ^= (state >> (22 - 1)) & 1u;
      fb ^= (state >> (17 - 1)) & 1u;
      break;
    default:
      fb = (state >> (n - 1)) & 1u;
      break;
  }
  (void)n;
  return fb & 1u;
}

static void bruteRadioIdle() {
  ELECHOUSE_cc1101.setSidle();
  digitalWrite(BRUTE_TX_PIN, LOW);
}

static void brutePrepareTx() {
  holdSdInactiveOnSharedSpi();
  reclaimSharedSpiBus();
#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(bruteFreqMHz());
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setPA(12);
  pinMode(BRUTE_TX_PIN, OUTPUT);
  digitalWrite(BRUTE_TX_PIN, LOW);
  ELECHOUSE_cc1101.SetTx();
}

static void bruteFinishTx() {
  s_switch.disableTransmit();
  s_switch.disableReceive();
  bruteRadioIdle();
  pinMode(BRUTE_TX_PIN, OUTPUT);
  digitalWrite(BRUTE_TX_PIN, LOW);
}

static bool bruteShouldAbort() {
  if (feature_exit_requested || featureExitButtonPressed()) {
    feature_exit_requested = true;
    s_stopRequested = true;
    return true;
  }
  if (s_stopRequested) {
    return true;
  }
  maintainTouchNavBar();
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    s_stopRequested = true;
    return true;
  }
  static bool prevPhysUp = false;
  const bool physUp = isPhysicalButtonPressed(BTN_UP);
  if (physUp && !prevPhysUp) {
    prevPhysUp = physUp;
    s_stopRequested = true;
    return true;
  }
  prevPhysUp = physUp;

  int x, y;
  if (readTouchXY(x, y)) {
    if (y > 20 && y < kBarBottom && x >= 10 && x < 26) {
      feature_exit_requested = true;
      s_stopRequested = true;
      return true;
    }
  }
  return false;
}

static void bruteWaitGoRelease() {
  const uint32_t t0 = millis();
  while ((isTouchNavButtonPressed(BTN_UP) || isPhysicalButtonPressed(BTN_UP)) &&
         (millis() - t0) < 800) {
    delay(5);
  }
  delay(30);
  (void)isTouchNavButtonPressedEdge(BTN_UP);
}

static void drawPanelFrame(int y, int h, const char* title) {
  tft.drawRoundRect(kPanelPadX, y, kPanelW, h, kRadius, UI_LINE);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kLabelX, y + 3);
  tft.print(title);
}

static void clearValueCell(int y) {
  tft.fillRect(kValueX, y, kValueW, kLineH - 1, TFT_BLACK);
}

static void drawSettingRow(int row, bool focused) {
  const int y = rowTextY(row);
  tft.fillRect(kLabelX, y, kValueX - kLabelX - 2, kLineH - 1, TFT_BLACK);
  clearValueCell(y);

  const char* label = "?";
  char value[28];
  value[0] = '\0';

  switch (row) {
    case FOCUS_FREQ:
      label = "Freq";
      snprintf(value, sizeof(value), "%.2f MHz", bruteFreqMHz());
      break;
    case FOCUS_BITS:
      label = "Bits";
      snprintf(value, sizeof(value), "%u", (unsigned)bruteBits());
      break;
    case FOCUS_MODE:
      label = "Mode";
      snprintf(value, sizeof(value), "%s",
               s_mode == MODE_DEBRUIJN ? "De Bruijn" : "Brute");
      break;
    case FOCUS_OPT:
      if (s_mode == MODE_DEBRUIJN) {
        label = "Pulse";
        snprintf(value, sizeof(value), "%u us", (unsigned)brutePulseUs());
      } else {
        label = "Proto";
        snprintf(value, sizeof(value), "%d", s_protocol);
      }
      break;
    default:
      break;
  }

  tft.setTextSize(1);
  tft.setTextColor(focused ? ORANGE : UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kLabelX, y);
  tft.print(focused ? ">" : " ");
  tft.print(label);

  tft.setTextColor(focused ? ORANGE : UI_TEXT, TFT_BLACK);
  tft.setCursor(kValueX, y);
  tft.print(value);
}

static void drawProgressBody(bool force) {
  const int py = progressPanelY();
  const int statusY = py + kBoxHeaderH + 2;
  const int countY = statusY + kLineH;
  const int barX = kLabelX;
  const int barW = kPanelW - 16;
  const int barH = 6;

  uint8_t pct = 0;
  if (s_progressTotal > 0) {
    pct = (uint8_t)((s_progressDone * 100UL) / s_progressTotal);
    if (pct > 100) pct = 100;
  }

  const bool stateChanged = force || s_lastUiState != s_runState;
  const bool progChanged =
      force || s_lastUiProgress != s_progressDone || s_lastUiPct != pct;

  if (stateChanged) {
    tft.fillRect(kLabelX, statusY, kPanelW - 16, kLineH - 1, TFT_BLACK);
    tft.setTextSize(1);
    const char* st = "Idle";
    uint16_t col = UI_DIM_TEXT;
    if (s_runState == ST_RUNNING) {
      st = "Running";
      col = ORANGE;
    } else if (s_runState == ST_DONE) {
      st = "Done";
      col = UI_TEXT;
    } else if (s_runState == ST_STOPPED) {
      st = "Stopped";
      col = UI_WARN;
    }
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(kLabelX, statusY);
    tft.print("Status");
    tft.setTextColor(col, TFT_BLACK);
    tft.setCursor(kValueX, statusY);
    tft.print(st);
    s_lastUiState = s_runState;
  }

  if (progChanged) {
    tft.fillRect(kLabelX, countY, kPanelW - 16, kLineH - 1, TFT_BLACK);
    char buf[40];
    if (s_progressTotal == 0) {
      snprintf(buf, sizeof(buf), "0 / 0");
    } else if (s_progressTotal >= 1000000UL) {
      snprintf(buf, sizeof(buf), "%lu/%lu %u%%",
               (unsigned long)s_progressDone,
               (unsigned long)s_progressTotal,
               (unsigned)pct);
    } else {
      snprintf(buf, sizeof(buf), "%lu / %lu  %u%%",
               (unsigned long)s_progressDone,
               (unsigned long)s_progressTotal,
               (unsigned)pct);
    }
    tft.setTextSize(1);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(kLabelX, countY);
    tft.print("Count");
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.setCursor(kValueX, countY);
    tft.print(buf);

    const int barY2 = countY + kLineH + 2;
    tft.fillRect(barX, barY2, barW, barH, DARK_GRAY);
    const int fill = (s_progressTotal > 0)
                         ? (int)((s_progressDone * (uint32_t)barW) / s_progressTotal)
                         : 0;
    if (fill > 0) {
      tft.fillRect(barX, barY2, min(fill, barW), barH, ORANGE);
    }
    s_lastUiProgress = s_progressDone;
    s_lastUiPct = pct;
  }
}

static void drawHints() {
  const int y0 = hintY0();
  const int bottom = subghzContentBottom();
  if (y0 + 20 >= bottom) {
    return;
  }
  tft.fillRect(0, y0, 240, min(28, bottom - y0), TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kLabelX, y0);
  tft.print("Sel focus   Prev/Next adjust");
  if (y0 + 12 < bottom) {
    tft.setCursor(kLabelX, y0 + 12);
    tft.print("Go start/stop");
  }
}

static void updateDisplay(bool force = false) {
  if (force || !s_chromeDrawn) {
    const int bodyBottom = subghzContentBottom();
    if (bodyBottom > kBarBottom) {
      tft.fillRect(0, kBarBottom + 1, PUEO_SCREEN_W, bodyBottom - kBarBottom - 1, TFT_BLACK);
    }
    tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
    tft.drawFastHLine(0, kBarBottom, PUEO_SCREEN_W, UI_LINE);

    drawPanelFrame(settingsPanelY(), settingsPanelH(), "Settings");
    drawPanelFrame(progressPanelY(), progressPanelH(), "Progress");
    drawHints();

    for (int r = 0; r < kSettingsRows; r++) {
      drawSettingRow(r, (FocusRow)r == s_focus);
    }
    s_lastUiFocus = s_focus;
    s_lastUiMode = s_mode;
    s_lastUiFreq = s_freqIndex;
    s_lastUiBits = s_bitsIndex;
    s_lastUiPulse = s_pulseIndex;
    s_lastUiProto = s_protocol;
    s_lastUiState = (RunState)255;
    s_lastUiProgress = 0xFFFFFFFFu;
    s_lastUiPct = 255;
    s_chromeDrawn = true;
    drawProgressBody(true);
    return;
  }

  const bool settingsDirty =
      s_lastUiFocus != s_focus || s_lastUiMode != s_mode ||
      s_lastUiFreq != s_freqIndex || s_lastUiBits != s_bitsIndex ||
      s_lastUiPulse != s_pulseIndex || s_lastUiProto != s_protocol;

  if (settingsDirty) {
    for (int r = 0; r < kSettingsRows; r++) {
      drawSettingRow(r, (FocusRow)r == s_focus);
    }
    s_lastUiFocus = s_focus;
    s_lastUiMode = s_mode;
    s_lastUiFreq = s_freqIndex;
    s_lastUiBits = s_bitsIndex;
    s_lastUiPulse = s_pulseIndex;
    s_lastUiProto = s_protocol;
  }

  drawProgressBody(false);
}

static void invalidateChrome() {
  s_chromeDrawn = false;
}

static void adjustFocused(int dir) {
  if (s_running) {
    return;
  }
  switch (s_focus) {
    case FOCUS_FREQ:
      s_freqIndex = (s_freqIndex + dir + kBruteFreqCount) % kBruteFreqCount;
      ELECHOUSE_cc1101.setMHZ(bruteFreqMHz());
      break;
    case FOCUS_BITS:
      s_bitsIndex = (s_bitsIndex + dir + kBitsChoiceCount) % kBitsChoiceCount;
      break;
    case FOCUS_MODE:
      s_mode = (s_mode == MODE_DEBRUIJN) ? MODE_BRUTE : MODE_DEBRUIJN;
      break;
    case FOCUS_OPT:
      if (s_mode == MODE_DEBRUIJN) {
        s_pulseIndex = (s_pulseIndex + dir + kPulseChoiceCount) % kPulseChoiceCount;
      } else {
        s_protocol += dir;
        if (s_protocol < 1) s_protocol = 12;
        if (s_protocol > 12) s_protocol = 1;
      }
      break;
    default:
      break;
  }
  updateDisplay();
  s_lastDebounce = millis();
}

static void cycleFocus() {
  if (s_running) {
    return;
  }
  s_focus = (FocusRow)((s_focus + 1) % FOCUS_COUNT);
  updateDisplay();
  s_lastDebounce = millis();
}

static void focusRowAtY(int y) {
  if (s_running) {
    return;
  }
  for (int r = 0; r < kSettingsRows; r++) {
    const int ry = rowTextY(r);
    if (y >= ry && y < ry + kLineH) {
      s_focus = (FocusRow)r;
      updateDisplay();
      s_lastDebounce = millis();
      return;
    }
  }
}

static void runDeBruijnStream() {
  const uint8_t n = bruteBits();
  const uint16_t pulse = brutePulseUs();
  const uint32_t mask = (n >= 32) ? 0xFFFFFFFFu : ((1UL << n) - 1UL);
  const uint32_t total = (n >= 32) ? 0xFFFFFFFFu : ((1UL << n) - 1UL);

  s_progressTotal = total;
  s_progressDone = 0;
  s_runState = ST_RUNNING;
  updateDisplay(true);

  brutePrepareTx();

  uint32_t state = 1u;
  for (uint32_t i = 0; i < total; i++) {
    if ((i & 0xFFu) == 0) {
      if (bruteShouldAbort()) {
        break;
      }
      yield();
      delay(0);
      s_progressDone = i;
      drawProgressBody(false);
    }

    const uint8_t bit = (uint8_t)(state & 1u);
    digitalWrite(BRUTE_TX_PIN, bit ? HIGH : LOW);
    delayMicroseconds(pulse);

    const uint32_t fb = bruteLfsrFeedback(n, state);
    state = ((state << 1) | fb) & mask;
  }

  digitalWrite(BRUTE_TX_PIN, LOW);
  bruteFinishTx();

  if (s_stopRequested || feature_exit_requested) {
    s_runState = ST_STOPPED;
  } else {
    s_progressDone = total;
    s_runState = ST_DONE;
  }
  s_running = false;
  s_stopRequested = false;
  updateDisplay(true);
}

static void runBruteForce() {
  const uint8_t bits = bruteBits();
  // Practical cap: framed RCSwitch brute beyond 12 bits is extremely slow.
  // Still allow larger sizes, but keep the loop responsive.
  const uint32_t total = (bits >= 31) ? 0x7FFFFFFFu : (1UL << bits);

  s_progressTotal = total;
  s_progressDone = 0;
  s_runState = ST_RUNNING;
  updateDisplay(true);

  brutePrepareTx();
  s_switch.disableReceive();
  s_switch.enableTransmit(BRUTE_TX_PIN);
  s_switch.setProtocol(s_protocol);
  s_switch.setPulseLength(brutePulseUs());
  // Default RCSwitch repeats (~10) make each code ~0.5–1s → looks frozen.
  s_switch.setRepeatTransmit(1);

  uint32_t lastUiMs = millis();
  for (uint32_t code = 0; code < total; code++) {
    // Poll stop often — send() itself blocks briefly per code.
    if ((code & 0x03u) == 0) {
      if (bruteShouldAbort()) {
        s_progressDone = code;
        break;
      }
      yield();
    }

    s_switch.send(code, bits);
    s_progressDone = code + 1;

    // Keep progress alive so the UI doesn't look hung.
    const uint32_t now = millis();
    if (now - lastUiMs >= 150) {
#if defined(CC1101_CS)
      digitalWrite(CC1101_CS, HIGH);
#endif
      drawProgressBody(false);
      maintainTouchNavBar();
      // TFT SPI can disturb CC1101 — re-enter TX for the next burst.
      ELECHOUSE_cc1101.SetTx();
      lastUiMs = now;
      yield();
    }
  }

  bruteFinishTx();

  if (s_stopRequested || feature_exit_requested) {
    s_runState = ST_STOPPED;
  } else {
    s_progressDone = total;
    s_runState = ST_DONE;
  }
  s_running = false;
  s_stopRequested = false;
  updateDisplay(true);
}

static void startOrStop() {
  if (s_running) {
    s_stopRequested = true;
    s_lastDebounce = millis();
    return;
  }

  bruteWaitGoRelease();
  s_stopRequested = false;
  s_running = true;
  s_runState = ST_RUNNING;
  s_progressDone = 0;
  s_progressTotal = 0;
  updateDisplay(true);

  if (s_mode == MODE_DEBRUIJN) {
    runDeBruijnStream();
  } else {
    runBruteForce();
  }
  s_lastDebounce = millis();
}

void bruteHandleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_LEFT)) {
    adjustFocused(-1);
    subghzWaitNavRelease(BTN_LEFT);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    adjustFocused(+1);
    subghzWaitNavRelease(BTN_RIGHT);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    cycleFocus();
    subghzWaitNavRelease(BTN_DOWN);
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    startOrStop();
    subghzWaitNavRelease(BTN_UP);
  }
}

void runUI() {
  // Avoid jammer/replay macros (SCREEN_WIDTH, ICON_NUM, …) leaking into this scope.
  static constexpr int kBarY = 20;
  static constexpr int kBarH = 16;
  static constexpr int kIconSz = 16;
  static constexpr int kIconN = 6;
  static constexpr int kScreenW = 240;

  static int iconX[kIconN] = {PUEO_SCREEN_W - 190, PUEO_SCREEN_W - 150, PUEO_SCREEN_W - 110, PUEO_SCREEN_W - 70, PUEO_SCREEN_W - 30, 10};
  static int iconY = kBarY;

  static const unsigned char* icons[kIconN] = {
      bitmap_icon_power,
      bitmap_icon_antenna,
      bitmap_icon_random,
      bitmap_icon_sort_down_minus,
      bitmap_icon_sort_up_plus,
      bitmap_icon_go_back
  };

  if (!s_uiDrawn) {
    tft.fillRect(0, kBarY, kScreenW, kBarH, DARK_GRAY);
    for (int i = 0; i < kIconN; i++) {
      if (icons[i] != NULL) {
        tft.drawBitmap(iconX[i], iconY, icons[i], kIconSz, kIconSz, UI_ICON);
      }
    }
    tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
    tft.drawFastHLine(0, kBarY + kBarH, PUEO_SCREEN_W, UI_LINE);
    s_uiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], kIconSz, kIconSz, UI_ICON);
      animationState = 2;
      switch (activeIcon) {
        case 0:
          startOrStop();
          break;
        case 1:
          if (!s_running) {
            s_mode = (s_mode == MODE_DEBRUIJN) ? MODE_BRUTE : MODE_DEBRUIJN;
            updateDisplay();
          }
          break;
        case 2:
          cycleFocus();
          break;
        case 3:
          adjustFocused(-1);
          break;
        case 4:
          adjustFocused(+1);
          break;
        case 5:
          feature_exit_requested = true;
          s_stopRequested = true;
          break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck >= 50) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > kBarY && y < kBarY + kBarH) {
        for (int i = 0; i < kIconN; i++) {
          if (x > iconX[i] && x < iconX[i] + kIconSz) {
            if (icons[i] != NULL && animationState == 0) {
              if (i == 5) {
                feature_exit_requested = true;
                s_stopRequested = true;
              } else {
                tft.drawBitmap(iconX[i], iconY, icons[i], kIconSz, kIconSz, TFT_BLACK);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
              }
            }
            break;
          }
        }
      } else if (!s_running && y >= settingsPanelY() &&
                 y < settingsPanelY() + settingsPanelH()) {
        focusRowAtY(y);
      }
    }
    lastTouchCheck = millis();
  }
}

void subBruteSetup() {
  if (Stealth::refuse("De Bruijn / Brute")) return;

  if (!cc1101Ready("Sub-GHz Brute")) return;
  Serial.begin(115200);
  setTouchButtonInputEnabled(true);
  subghzSetBruteNavLabels();
  subghzClearBody(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  subghzRedrawNavChrome();

  holdSdInactiveOnSharedSpi();
  reclaimSharedSpiBus();

#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setCCMode(0);
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(500.0);
  ELECHOUSE_cc1101.setPA(12);
  ELECHOUSE_cc1101.setMHZ(bruteFreqMHz());
  ELECHOUSE_cc1101.setSidle();
  pinMode(BRUTE_TX_PIN, INPUT);

  s_freqIndex = 11;
  s_bitsIndex = 2;
  s_pulseIndex = 1;
  s_protocol = 1;
  s_focus = FOCUS_FREQ;
  s_mode = MODE_DEBRUIJN;
  s_runState = ST_IDLE;
  s_running = false;
  s_stopRequested = false;
  s_progressDone = 0;
  s_progressTotal = 0;

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
#endif
  delay(100);

  subghzClearBody(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  setupTouchscreen();

  invalidateChrome();
  s_uiDrawn = false;
  updateDisplay(true);
  subghzRedrawNavChrome();
}

void subBruteLoop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    s_stopRequested = true;
    return;
  }

  maintainTouchNavBar();
  runUI();
  if (s_uiDrawn) {
    tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
    tft.drawFastHLine(0, kBarBottom, PUEO_SCREEN_W, UI_LINE);
  }
  bruteHandleNavButtons();

#if HAS_PCF8574_BUTTONS
  const int btnLeftState = pcf.digitalRead(BTN_LEFT);
  const int btnRightState = pcf.digitalRead(BTN_RIGHT);
  const int btnUpState = pcf.digitalRead(BTN_UP);
  const int btnDownState = pcf.digitalRead(BTN_DOWN);
#else
  const int btnLeftState = isPhysicalButtonPressed(BTN_LEFT) ? LOW : HIGH;
  const int btnRightState = isPhysicalButtonPressed(BTN_RIGHT) ? LOW : HIGH;
  const int btnUpState = isPhysicalButtonPressed(BTN_UP) ? LOW : HIGH;
  const int btnDownState = isPhysicalButtonPressed(BTN_DOWN) ? LOW : HIGH;
#endif

  if (btnLeftState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    adjustFocused(-1);
  }
  if (btnRightState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    adjustFocused(+1);
  }
  if (btnDownState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    cycleFocus();
  }
  if (btnUpState == LOW && millis() - s_lastDebounce > kDebounceMs) {
    startOrStop();
  }
}

}  // namespace SubBrute

namespace SubImport {

/* Reading a Flipper `.sub` off the card into a saved profile.
 *
 * SubFile::parse has been able to do the hard half since it was written.
 * What was missing was somewhere to put the result: the profile record was
 * declared three times and saveProfile could not store anything without
 * also prompting and drawing. Both are fixed, so this is a file list and a
 * confirmation.
 *
 * No radio, so no Stealth gate. This reads one file and writes one EEPROM
 * record; there is nothing here for Stealth to refuse.
 */

constexpr uint16_t kMaxFiles = 64;
constexpr int      kRowH = 12;

struct Entry {
  String name;      // as shown, and the source of the profile name
  String path;
};

std::vector<Entry> s_files;
int      s_sel = 0;
int      s_top = 0;
bool     s_needRedraw = true;
bool     s_mounted = false;   // this screen's own, not another feature's

/* `.sub`, case-insensitively, and nothing else. */
static bool isSubFile(const String& name) {
  if (name.length() < 5) return false;
  String tail = name.substring(name.length() - 4);
  tail.toLowerCase();
  return tail == ".sub";
}

/* The profile name: the filename without its extension, cut to fit.
 *
 * A `.sub` carries no name of its own. Asking for one would put a keyboard
 * between choosing a file and finding out whether it parses, which is the
 * wrong order: the step that can fail should come first. */
static String nameFromFile(const String& fileName) {
  int dot = fileName.lastIndexOf('.');
  String stem = (dot > 0) ? fileName.substring(0, dot) : fileName;
  if (stem.length() > MAX_NAME_LENGTH - 1) {
    stem = stem.substring(0, MAX_NAME_LENGTH - 1);
  }
  return stem;
}

static void scan() {
  s_files.clear();
  s_sel = 0;
  s_top = 0;

  /* The CC1101 and the card share the SPI bus on this board, so a
   * plain SD.open fails after any sub-GHz feature has run. The jamming
   * detector mounts it this way for the same reason. */
  if (!(s_mounted && SD.exists("/"))) {
#if defined(CC1101_CS)
    pinMode(CC1101_CS, OUTPUT);
    digitalWrite(CC1101_CS, HIGH);
#endif
    restoreSdAfterSharedSpi();
    s_mounted = isSDCardAvailable();
  }
  if (!s_mounted) {
    subghzFileStatus = "No SD card";
    subghzFileStatusWarn = true;
    return;
  }

  /* Make it rather than complain about it. The directory only exists once
   * a profile has been exported, so on a card that has never done that the
   * screen was telling somebody to create a folder it could create itself,
   * and the instruction below it to put files there named a path that was
   * not on the card. sdEnsureDir builds the parents too. */
  if (!SD.exists(SUBGHZ_SD_DIR)) {
    sdEnsureDir(SUBGHZ_SD_DIR);
  }

  File dir = SD.open(SUBGHZ_SD_DIR);
  if (!dir || !dir.isDirectory()) {
    if (dir) dir.close();
    subghzFileStatus = String("Cannot open ") + SUBGHZ_SD_DIR;
    subghzFileStatusWarn = true;
    return;
  }

  for (File e = dir.openNextFile(); e; e = dir.openNextFile()) {
    if (!e.isDirectory()) {
      String n = e.name();
      int slash = n.lastIndexOf('/');
      if (slash >= 0) n = n.substring(slash + 1);
      if (isSubFile(n) && s_files.size() < kMaxFiles) {
        Entry it;
        it.name = n;
        it.path = String(SUBGHZ_SD_DIR) + "/" + n;
        s_files.push_back(it);
      }
    }
    e.close();
  }
  dir.close();

  std::sort(s_files.begin(), s_files.end(),
            [](const Entry& a, const Entry& b) { return a.name < b.name; });

  if (s_files.empty()) {
    subghzFileStatus = "No .sub files";
    subghzFileStatusWarn = false;
  } else {
    subghzFileStatus = String(s_files.size()) + " file(s)";
    subghzFileStatusWarn = false;
  }
}

/* Read the chosen file, parse it, and store what comes out.
 *
 * Every failure says which one it was. "Import failed" on its own would
 * leave somebody guessing between a card that is not there, a RAW capture
 * that cannot become a profile, and a frequency this radio cannot reach,
 * which are three different things to do next. */
static void importSelected() {
  if (s_files.empty()) return;
  const Entry& it = s_files[s_sel];

  File fh = SD.open(it.path.c_str(), FILE_READ);
  if (!fh) {
    subghzFileStatus = "Cannot open file";
    subghzFileStatusWarn = true;
    return;
  }
  const size_t len = fh.size();
  if (len == 0 || len > 8192) {
    fh.close();
    subghzFileStatus = (len == 0) ? "File is empty" : "File too large";
    subghzFileStatusWarn = true;
    return;
  }

  std::vector<char> buf(len + 1);
  const size_t got = fh.read((uint8_t*)buf.data(), len);
  fh.close();
  buf[got] = '\0';

  SubFile::Parsed parsed{};
  const SubFile::Result r = SubFile::parse(buf.data(), got, &parsed);
  if (r != SubFile::Result::Ok) {
    subghzFileStatus = SubFile::resultText(r);
    subghzFileStatusWarn = true;
    return;
  }

  String err;
  bool rotated = false;
  if (!replayat::makeRoomForProfile(&err, &rotated)) {
    subghzFileStatus = "Full, export failed";
    subghzFileStatusWarn = true;
    return;
  }

  const SubGhzProfile p = replayat::makeProfile(
      parsed.frequency, parsed.value, parsed.bitLength, parsed.protocol,
      nameFromFile(it.name).c_str());
  replayat::storeProfile(p);

  /* protocol 0 is the parser saying it will not guess at this file's
   * protocol name. The record is still worth storing, because the
   * frequency, key and bit count are real, but a plain "Imported" would
   * leave the first sign of trouble to be a Send that quietly does
   * nothing. Name the protocol: which decoder is missing is the actionable
   * part. */
  if (parsed.protocol == 0) {
    subghzFileStatus = String("Stored, ") +
               (parsed.protocolName[0] != '\0' ? parsed.protocolName
                                                : "that protocol") +
               " cannot send";
    subghzFileStatusWarn = true;
    return;
  }

  subghzFileStatus = rotated ? "Imported, 5 sent to SD"
                     : String("Imported to slot ")
                           + String(replayat::profileCount);
  subghzFileStatusWarn = false;
}

static void draw() {
  subghzClearBody(TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  const int top = 30 + replayat::yshift;
  const int bottom = subghzContentBottom();
  const int rows = (bottom - top - 14) / kRowH;

  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(6, top - 12);
  tft.print("Import .sub");

  if (s_files.empty()) {
    tft.setTextColor(subghzFileStatusWarn ? UI_WARN : UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(6, top + 6);
    tft.print(subghzFileStatus);
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(6, top + 20);
    tft.print("put files in ");
    tft.print(SUBGHZ_SD_DIR);
    return;
  }

  if (s_sel < s_top) s_top = s_sel;
  if (rows > 0 && s_sel >= s_top + rows) s_top = s_sel - rows + 1;

  for (int i = 0; i < rows && (s_top + i) < (int)s_files.size(); i++) {
    const int idx = s_top + i;
    const int y = top + i * kRowH;
    const bool on = (idx == s_sel);
    tft.setTextColor(on ? TFT_BLACK : UI_TEXT, on ? UI_ICON : TFT_BLACK);
    tft.setCursor(6, y);
    String n = s_files[idx].name;
    if (n.length() > 30) n = n.substring(0, 29) + "~";
    tft.print(on ? ">" : " ");
    tft.print(n);
  }

  tft.setTextColor(subghzFileStatusWarn ? UI_WARN : UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(6, bottom - 12);
  tft.print(subghzFileStatus);
}

/* The touch nav bar, which on this board is the only input there is.
 *
 * The first version read isPhysicalButtonPressed only, and the PCF8574
 * buttons are disabled here, so Next, Prev and Import did nothing while
 * Exit still worked: Exit goes through featureExitButtonPressed, which
 * falls through to the touch bar, and nothing else did.
 *
 * Edge-triggered, with a release wait, the way Saved Profile does it. A
 * level read would repeat for as long as a finger rests on the bar. */
static void handleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  if (s_files.empty()) {
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    s_sel = (s_sel - 1 + (int)s_files.size()) % (int)s_files.size();
    s_needRedraw = true;
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    s_sel = (s_sel + 1) % (int)s_files.size();
    s_needRedraw = true;
    subghzWaitNavRelease(BTN_DOWN);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    importSelected();
    s_needRedraw = true;
    subghzWaitNavRelease(BTN_RIGHT);
  }
}

void setup() {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("", "Next", "Exit", "Prev", "Import");

  EEPROM.begin(EEPROM_SIZE);
  replayat::loadProfileCount();

  scan();
  s_needRedraw = true;

  subghzClearBody(TFT_BLACK);
  setupTouchscreen();
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  replayat::uiDrawn = false;
  replayat::runUI();
  subghzRedrawNavChrome();
}

void loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  replayat::runUI();
  handleNavButtons();

  static unsigned long lastMs = 0;
  const unsigned long debounce = 200;

  static bool pUp = false, pDown = false, pRight = false;
  const bool up    = isPhysicalButtonPressed(BTN_UP);
  const bool down  = isPhysicalButtonPressed(BTN_DOWN);
  const bool go    = isPhysicalButtonPressed(BTN_RIGHT);

  if (!s_files.empty() && millis() - lastMs > debounce) {
    if (down && !pDown) {
      s_sel = (s_sel + 1) % (int)s_files.size();
      s_needRedraw = true;
      lastMs = millis();
    } else if (up && !pUp) {
      s_sel = (s_sel - 1 + (int)s_files.size()) % (int)s_files.size();
      s_needRedraw = true;
      lastMs = millis();
    } else if (go && !pRight) {
      importSelected();
      s_needRedraw = true;
      lastMs = millis();
    }
  }
  pUp = up; pDown = down; pRight = go;

  if (s_needRedraw) {
    draw();
    s_needRedraw = false;
    subghzRedrawNavChrome();
  }

  delay(10);
}

}  // namespace SubImport

namespace SubExport {

/* Writing a saved profile out as a Flipper `.sub`.
 *
 * The inverse of SubImport, and the easier half: SubFile::write has nothing
 * to guess at, because every field in the record has exactly one spelling
 * in the format. What makes it worth having is that the alternative is a
 * packed binary only this firmware reads, and the five-slot EEPROM pushes
 * older captures into that format as new ones arrive.
 *
 * No radio, so no Stealth gate. This reads EEPROM and writes one file.
 */

constexpr int kRowH = 12;

int    s_sel = 0;
int    s_top = 0;
bool   s_needRedraw = true;
bool   s_mounted = false;

/* Strip anything that has no business in a filename. A profile name comes
 * from a keyboard or from another file's stem, so it is not trusted to be
 * one. */
static String safeFileName(const char* name) {
  String out;
  for (const char* p = name; *p != '\0' && out.length() < MAX_NAME_LENGTH; p++) {
    const char c = *p;
    const bool ok = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                    (c >= '0' && c <= '9') || c == '-' || c == '_';
    out += ok ? c : '_';
  }
  if (out.length() == 0) out = "profile";
  return out;
}

static bool mountCard() {
  /* The CC1101 and the card share the SPI bus on this board, so a plain
   * SD.open fails after any sub-GHz feature has run. */
  if (s_mounted && SD.exists("/")) return true;
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif
  restoreSdAfterSharedSpi();
  s_mounted = isSDCardAvailable();
  return s_mounted;
}

static void exportSelected() {
  replayat::readProfileCount();
  if (replayat::profileCount == 0) {
    subghzFileStatus = "No profiles";
    subghzFileStatusWarn = true;
    return;
  }
  if (s_sel < 0 || s_sel >= (int)replayat::profileCount) {
    subghzFileStatus = "No such slot";
    subghzFileStatusWarn = true;
    return;
  }

  SubGhzProfile p{};
  EEPROM.get(ADDR_PROFILE_START + (s_sel * PROFILE_SIZE), p);
  p.name[MAX_NAME_LENGTH - 1] = '\0';

  /* TE is 0 because the record has no room for it: the EEPROM budget is
   * exactly full at five 28-byte profiles, so there is nowhere to put one
   * without moving the format. write() omits the line rather than
   * inventing a value, and a reader that needs TE is better off applying
   * its own default than trusting a guess from here. */
  char buf[256];
  const size_t n = SubFile::write(buf, sizeof(buf), p.frequency, p.value,
                                  p.bitLength, p.protocol, 0);
  if (n == 0) {
    /* The only way this fails on a stored record is protocol 0, which is a
     * .sub imported from a protocol this firmware will not name. It cannot
     * be written back without inventing the name it never stored. */
    subghzFileStatus = (p.protocol == 0) ? "No protocol name to write"
                                 : "Record will not render";
    subghzFileStatusWarn = true;
    return;
  }

  if (!mountCard()) {
    subghzFileStatus = "No SD card";
    subghzFileStatusWarn = true;
    return;
  }
  if (!SD.exists(SUBGHZ_SD_DIR)) {
    sdEnsureDir(SUBGHZ_SD_DIR);
  }

  const String path =
      String(SUBGHZ_SD_DIR) + "/" + safeFileName(p.name) + ".sub";
  if (SD.exists(path.c_str())) SD.remove(path.c_str());

  File fh = SD.open(path.c_str(), FILE_WRITE);
  if (!fh) {
    subghzFileStatus = "Cannot open for write";
    subghzFileStatusWarn = true;
    return;
  }
  const size_t put = fh.write((const uint8_t*)buf, n);
  fh.close();

  if (put != n) {
    subghzFileStatus = "Short write";
    subghzFileStatusWarn = true;
    return;
  }

  subghzFileStatus = String("Wrote ") + safeFileName(p.name) + ".sub";
  subghzFileStatusWarn = false;
}

static void draw() {
  subghzClearBody(TFT_BLACK);
  tft.setTextFont(1);
  tft.setTextSize(1);

  const int top = 30 + replayat::yshift;
  const int bottom = subghzContentBottom();
  const int rows = (bottom - top - 14) / kRowH;

  tft.setTextColor(UI_TEXT, TFT_BLACK);
  tft.setCursor(6, top - 12);
  tft.print("Export .sub");

  replayat::readProfileCount();
  const int count = (int)replayat::profileCount;

  if (count == 0) {
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(6, top + 6);
    tft.print("No saved profiles");
    tft.setCursor(6, top + 20);
    tft.print("capture one, or import a .sub");
    return;
  }

  if (s_sel >= count) s_sel = count - 1;
  if (s_sel < 0) s_sel = 0;
  if (s_sel < s_top) s_top = s_sel;
  if (rows > 0 && s_sel >= s_top + rows) s_top = s_sel - rows + 1;

  for (int i = 0; i < rows && (s_top + i) < count; i++) {
    const int idx = s_top + i;
    const int y = top + i * kRowH;
    const bool on = (idx == s_sel);

    SubGhzProfile p{};
    EEPROM.get(ADDR_PROFILE_START + (idx * PROFILE_SIZE), p);
    p.name[MAX_NAME_LENGTH - 1] = '\0';

    tft.setTextColor(on ? TFT_BLACK : UI_TEXT, on ? UI_ICON : TFT_BLACK);
    tft.setCursor(6, y);
    tft.print(on ? ">" : " ");
    tft.print(p.name);

    /* Say up front which rows cannot be written, rather than on the press.
     * A profile with no protocol name is the one case write() refuses. */
    if (p.protocol == 0) {
      tft.setTextColor(on ? TFT_BLACK : UI_WARN, on ? UI_ICON : TFT_BLACK);
      tft.print("  (no protocol)");
    }
  }

  tft.setTextColor(subghzFileStatusWarn ? UI_WARN : UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(6, bottom - 12);
  tft.print(subghzFileStatus);
}

/* The touch nav bar, which on this board is the only input there is. */
static void handleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }

  replayat::readProfileCount();
  const int count = (int)replayat::profileCount;
  if (count == 0) {
    return;
  }

  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    s_sel = (s_sel - 1 + count) % count;
    s_needRedraw = true;
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_DOWN)) {
    s_sel = (s_sel + 1) % count;
    s_needRedraw = true;
    subghzWaitNavRelease(BTN_DOWN);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    exportSelected();
    s_needRedraw = true;
    subghzWaitNavRelease(BTN_RIGHT);
  }
}

void setup() {
  setTouchButtonInputEnabled(true);
  setTouchNavLabels("", "Next", "Exit", "Prev", "Export");

  EEPROM.begin(EEPROM_SIZE);
  replayat::loadProfileCount();

  s_sel = 0;
  s_top = 0;
  subghzFileStatus = "";
  subghzFileStatusWarn = false;
  s_needRedraw = true;

  subghzClearBody(TFT_BLACK);
  setupTouchscreen();
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  replayat::uiDrawn = false;
  replayat::runUI();
  subghzRedrawNavChrome();
}

void loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  replayat::runUI();
  handleNavButtons();

  if (s_needRedraw) {
    s_needRedraw = false;
    draw();
    subghzRedrawNavChrome();
  }
  delay(20);
}

}  // namespace SubExport

namespace jammingdetector {

static constexpr uint16_t JD_SAMPLES = ESP32DIV_JD_RSSI_SAMPLES;
static constexpr double JD_SAMPLE_HZ = 5000.0;
static constexpr double JD_RXBW = 650.0;
static constexpr int JD_MARGIN_DB = 18;
static constexpr int JD_ABS_THRESH_DBM = -75;
static constexpr float JD_BUSY_WIN_DUTY = 0.50f;
static constexpr uint32_t JD_JAM_STREAK_MS = 400;
static constexpr float JD_JAM_AVG_DUTY = 0.80f;
static constexpr float JD_ACTIVITY_DUTY = 0.10f;
static constexpr uint8_t JD_RING = 20;
static constexpr int JD_FLOOR_INIT_DBM = -95;

static const uint32_t kFreqHz[] = {433920000UL, 434420000UL, 315000000UL, 868350000UL};
static const char* kFreqLabel[] = {"433.92", "434.42", "315.00", "868.35"};
static constexpr uint8_t kFreqCount = sizeof(kFreqHz) / sizeof(kFreqHz[0]);
static uint8_t freqIdx = 1;

static unsigned int samplingPeriod = 0;

static float noiseFloor = JD_FLOOR_INIT_DBM;
static uint32_t busyStreakMs = 0;
static float dutyRing[JD_RING];
static uint8_t dutyRingPos = 0;
static bool jamActive = false;
static uint32_t jamStartMs = 0;
static int jamPeakDbm = -127;
static uint32_t eventCount = 0;

static bool logEnabled = false;
static bool logMounted = false;
static bool prevLeft = false, prevRight = false, prevUp = false, prevDown = false;

static constexpr int kJdBarBottom = 36;
static constexpr int kJdSectionGap = 6;
static constexpr int kJdPad = 4;
static constexpr int kJdPanelW = 240 - (kJdPad * 2);
static constexpr int kJdRadius = 3;
static constexpr int kJdLabelX = 10;
static constexpr int kJdCol2LabelX = 128;
static constexpr int kJdValueX = 44;
static constexpr int kJdCol2ValueX = 162;
static constexpr int kJdCol1ValueW = 78;
static constexpr int kJdCol2ValueW = 70;
static constexpr int kJdLineH = 12;
static constexpr int kJdPanelHeader = 14;
static constexpr int kJdInfoY = kJdBarBottom + 4;
static constexpr int kJdInfoH = 46;
static constexpr int kJdInfoRow1Y = kJdInfoY + kJdPanelHeader + 2;
static constexpr int kJdInfoRow2Y = kJdInfoRow1Y + kJdLineH + 2;
static constexpr int kJdStatusY = kJdInfoY + kJdInfoH + kJdSectionGap;
static constexpr int kJdStatusH = 30;
static constexpr int kJdWaveY = kJdStatusY + kJdStatusH + kJdSectionGap;
static constexpr int kJdWaveHeader = 14;

static constexpr int kWaveW = ESP32DIV_JD_WAVE_WIDTH;
static constexpr int kWaveRssiMin = -100;
static constexpr int kWaveRssiMax = -35;
static int8_t waveBuf[kWaveW];
static uint16_t waveWrite = 0;
static float waveSmooth = -95.0f;

static bool s_chromeDrawn = false;
static bool s_jdUiDrawn = false;
static bool s_waveHasPrev = false;
static int16_t s_wavePrevY[kWaveW];
static uint8_t subghzFileStatusState = 255;

struct JdDisp {
  bool valid = false;
  uint8_t freqIdx = 255;
  int rssi = -999;
  int floor = -999;
  int dutyPct = -1;
  uint32_t events = 0xFFFFFFFFu;
  bool logOn = false;
};
static JdDisp s_disp;

static void cc1101BeginRx() {
  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setRxBW(JD_RXBW);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.setMHZ(kFreqHz[freqIdx] / 1000000.0);
  ELECHOUSE_cc1101.SetRx();
}

static void tuneTo(uint8_t idx) {
  freqIdx = idx % kFreqCount;
  ELECHOUSE_cc1101.setSidle();
  ELECHOUSE_cc1101.setMHZ(kFreqHz[freqIdx] / 1000000.0);
  ELECHOUSE_cc1101.SetRx();
  s_disp.freqIdx = 255;
}

static bool jdMountSD() {
  if (logMounted && SD.exists("/")) return true;

#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  restoreSdAfterSharedSpi();
  logMounted = isSDCardAvailable();
  return logMounted;
}

static void logEvent(uint32_t whenMs, uint32_t durMs, int peakDbm, int dutyPct) {
  /* Both, and not just the toggle. The toggle cannot be turned on while
   * Settings says no, so this is redundant -- and it is the line that
   * actually keeps the promise, so it is the line worth being sure of. */
  if (!logEnabled || !sdLoggingAllowed(LogApp::JamDetector)) return;

  restoreSdAfterSharedSpi();
  if (jdMountSD()) {
    if (!sdEnsureDir(LOG_DIR)) return;
    /* Two filenames rather than one, because this appends across sessions:
     * a single name would end up holding both formats. */
    const bool json = settings().logJson;
    File f = SD.open(json ? LOG_DIR "/jamdet.jsonl" : LOG_DIR "/jamdet.csv",
                     FILE_APPEND);
    if (f) {
      if (json) {
        /* The CSV here never had a header, so these names are new rather than
         * a translation of existing ones. JAM is the event type: the column
         * was a literal in every row and is a field now. */
        f.printf("{\"ms\":%lu,\"freq\":\"%s\",\"event\":\"JAM\","
                 "\"peak_dbm\":%d,\"dur_ms\":%lu,\"duty_pct\":%d}\n",
                 (unsigned long)whenMs, kFreqLabel[freqIdx], peakDbm,
                 (unsigned long)durMs, dutyPct);
      } else {
        f.printf("%lu,%s,JAM,%d,%lu,%d\n",
                 (unsigned long)whenMs, kFreqLabel[freqIdx], peakDbm,
                 (unsigned long)durMs, dutyPct);
      }
      f.close();
    }
  }
  cc1101BeginRx();
}

static int jdWaveBottom() {
  return subghzContentBottom() - 2;
}

static int jdPlotTop() {
  return kJdWaveY + kJdWaveHeader + 4;
}

static int jdPlotHeight() {
  const int h = jdWaveBottom() - jdPlotTop() - 2;
  return h > 8 ? h : 8;
}

static int jdRssiToY(int dbm) {
  dbm = constrain(dbm, kWaveRssiMin, kWaveRssiMax);
  const int plotH = jdPlotHeight();
  return jdPlotTop() + plotH - 2 -
         ((dbm - kWaveRssiMin) * (plotH - 4) / (kWaveRssiMax - kWaveRssiMin));
}

static void jdDrawValueCell(int x, int y, int w, const char* text, uint16_t color) {
  tft.fillRect(x, y, w, kJdLineH, TFT_BLACK);
  tft.setTextSize(1);
  tft.setTextColor(color, TFT_BLACK);
  tft.setCursor(x, y);
  tft.print(text);
}

static void jdInvalidateContent() {
  s_chromeDrawn = false;
  subghzFileStatusState = 255;
  s_disp.valid = false;
  s_waveHasPrev = false;
  waveWrite = 0;
  waveSmooth = -95.0f;
  memset(waveBuf, kWaveRssiMin, sizeof(waveBuf));
}

static void jdInvalidateAll() {
  jdInvalidateContent();
  s_jdUiDrawn = false;
}

static void jdResetStats() {
  eventCount = 0;
  busyStreakMs = 0;
  jamActive = false;
  noiseFloor = JD_FLOOR_INIT_DBM;
  for (uint8_t i = 0; i < JD_RING; i++) dutyRing[i] = 0;
  jdInvalidateContent();
}

static void jdRunUI() {
  static constexpr int kBarY = 20;
  static constexpr int kBarH = 16;
  static constexpr int kIconSz = 16;
  static constexpr int kIconN = 5;
  static constexpr int kBackIdx = 4;

  static int iconX[kIconN] = {PUEO_SCREEN_W - 150, PUEO_SCREEN_W - 110, PUEO_SCREEN_W - 70, PUEO_SCREEN_W - 30, 10};
  static int iconY = kBarY;

  static const unsigned char* icons[kIconN] = {
      bitmap_icon_sort_down_minus,
      bitmap_icon_floppy,
      bitmap_icon_undo,
      bitmap_icon_sort_up_plus,
      bitmap_icon_go_back,
  };

  if (!s_jdUiDrawn) {
    tft.fillRect(0, kBarY, PUEO_SCREEN_W, kBarH, DARK_GRAY);
    for (int i = 0; i < kIconN; i++) {
      tft.drawBitmap(iconX[i], iconY, icons[i], kIconSz, kIconSz, UI_ICON);
    }
    tft.drawFastHLine(0, 19, PUEO_SCREEN_W, UI_LINE);
    tft.drawFastHLine(0, kBarY + kBarH, PUEO_SCREEN_W, UI_LINE);
    s_jdUiDrawn = true;
  }

  static unsigned long lastAnimationTime = 0;
  static int animationState = 0;
  static int activeIcon = -1;

  if (animationState > 0 && millis() - lastAnimationTime >= 150) {
    if (animationState == 1) {
      if (activeIcon >= 0 && activeIcon != kBackIdx) {
        tft.drawBitmap(iconX[activeIcon], iconY, icons[activeIcon], kIconSz, kIconSz, UI_ICON);
      }
      animationState = 2;
      switch (activeIcon) {
        case 0:
          tuneTo(freqIdx + kFreqCount - 1);
          s_disp.freqIdx = 255;
          break;
        case 1:
          /* Settings holds the master switch; this one only chooses within
           * it. Pressing it while logging is off leaves the cell reading
           * "n/a", which is the answer to why nothing happened. */
          if (sdLoggingAllowed(LogApp::JamDetector)) {
            logEnabled = !logEnabled;
            s_disp.logOn = !logEnabled;
          }
          break;
        case 2:
          jdResetStats();
          break;
        case 3:
          tuneTo(freqIdx + 1);
          s_disp.freqIdx = 255;
          break;
        case kBackIdx:
          feature_exit_requested = true;
          break;
        default:
          break;
      }
    } else if (animationState == 2) {
      animationState = 0;
      activeIcon = -1;
    }
    lastAnimationTime = millis();
  }

  static unsigned long lastTouchCheck = 0;
  if (millis() - lastTouchCheck >= 50) {
    int x, y;
    if (feature_active && readTouchXY(x, y)) {
      if (y > kBarY && y < kBarY + kBarH) {
        for (int i = 0; i < kIconN; i++) {
          if (x > iconX[i] && x < iconX[i] + kIconSz) {
            if (animationState == 0) {
              if (i == kBackIdx) {
                feature_exit_requested = true;
              } else {
                tft.fillRect(iconX[i], iconY, kIconSz, kIconSz, DARK_GRAY);
                animationState = 1;
                activeIcon = i;
                lastAnimationTime = millis();
              }
            }
            break;
          }
        }
      }
    }
    lastTouchCheck = millis();
  }
}

static void jdDrawPanelFrame(int y, int h, const char* title) {
  tft.drawRoundRect(kJdPad, y, kJdPanelW, h, kJdRadius, UI_LINE);
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kJdLabelX, y + 3);
  tft.print(title);
}

static void jdDrawPlotGridLines() {
  const int plotX = kJdPad + 4;
  const int plotW = kJdPanelW - 8;
  const int plotTop = jdPlotTop();
  const int plotH = jdPlotHeight();
  if (plotH < 8) {
    return;
  }

  for (int g = 1; g < 4; g++) {
    const int gy = plotTop + (plotH * g) / 4;
    tft.drawFastHLine(plotX + 1, gy, plotW - 2, DARK_GRAY);
  }
  for (int g = 1; g < 4; g++) {
    const int gx = plotX + (plotW * g) / 4;
    tft.drawFastVLine(gx, plotTop + 1, plotH - 2, DARK_GRAY);
  }
}

static void jdDrawPlotBackground() {
  const int plotX = kJdPad + 4;
  const int plotW = kJdPanelW - 8;
  const int plotTop = jdPlotTop();
  const int plotH = jdPlotHeight();
  if (plotH < 8) {
    return;
  }

  tft.fillRect(plotX, plotTop, plotW, plotH, TFT_BLACK);
  jdDrawPlotGridLines();
}

static void jdDrawStaticChrome() {
  if (s_chromeDrawn) {
    return;
  }

  const int bodyBottom = jdWaveBottom();
  tft.fillRect(0, kJdBarBottom + 1, PUEO_SCREEN_W, bodyBottom - kJdBarBottom - 1, TFT_BLACK);

  jdDrawPanelFrame(kJdInfoY, kJdInfoH, "Monitor");
  tft.setTextSize(1);
  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(kJdLabelX, kJdInfoRow1Y);
  tft.print("Freq");
  tft.setCursor(kJdLabelX, kJdInfoRow2Y);
  tft.print("RSSI");
  tft.setCursor(kJdCol2LabelX, kJdInfoRow1Y);
  tft.print("Duty");
  tft.setCursor(kJdCol2LabelX, kJdInfoRow2Y);
  tft.print("Log");

  const int waveH = bodyBottom - kJdWaveY;
  if (waveH > kJdWaveHeader + 12) {
    jdDrawPanelFrame(kJdWaveY, waveH, "Signal");
    jdDrawPlotBackground();
  }

  s_chromeDrawn = true;
  s_disp.valid = false;
  subghzFileStatusState = 255;
}

static void jdDrawStatusBox(bool jam, bool activity) {
  const uint8_t st = jam ? 2 : (activity ? 1 : 0);
  if (st == subghzFileStatusState && s_disp.valid) {
    return;
  }
  subghzFileStatusState = st;

  uint16_t bg = jam ? ORANGE : (activity ? UI_WARN : UI_OK);
  uint16_t fg = (jam || activity) ? TFT_BLACK : UI_FG;
  const char* s = jam ? "JAMMING DETECTED" : (activity ? "ACTIVITY" : "CLEAR");

  tft.fillRoundRect(kJdPad, kJdStatusY, kJdPanelW, kJdStatusH, kJdRadius, bg);
  tft.drawRoundRect(kJdPad, kJdStatusY, kJdPanelW, kJdStatusH, kJdRadius, UI_LINE);
  tft.setTextColor(fg, bg);
  uint8_t sz = 2;
  tft.setTextSize(sz);
  if (tft.textWidth(s) > kJdPanelW - 8) { sz = 1; tft.setTextSize(sz); }
  const int16_t tw = tft.textWidth(s);
  const int16_t th = 8 * sz;
  tft.setCursor(kJdPad + (kJdPanelW - tw) / 2, kJdStatusY + (kJdStatusH - th) / 2);
  tft.print(s);
  tft.setTextSize(1);
}

static void jdUpdateInfo(int rssiNow, int dutyPct) {
  jdDrawStaticChrome();

  const bool full = !s_disp.valid;
  char buf[28];

  if (full || s_disp.freqIdx != freqIdx) {
    snprintf(buf, sizeof(buf), "%s MHz", kFreqLabel[freqIdx]);
    jdDrawValueCell(kJdValueX, kJdInfoRow1Y, kJdCol1ValueW, buf, UI_TEXT);
    s_disp.freqIdx = freqIdx;
  }

  if (full || abs(s_disp.rssi - rssiNow) >= 2 ||
      abs(s_disp.floor - (int)noiseFloor) >= 2) {
    snprintf(buf, sizeof(buf), "%d/%d dBm", rssiNow, (int)noiseFloor);
    jdDrawValueCell(kJdValueX, kJdInfoRow2Y, kJdCol1ValueW, buf, UI_TEXT);
    s_disp.rssi = rssiNow;
    s_disp.floor = (int)noiseFloor;
  }

  if (full || abs(s_disp.dutyPct - dutyPct) >= 5 || s_disp.events != eventCount) {
    snprintf(buf, sizeof(buf), "%d%% E:%lu", dutyPct, (unsigned long)eventCount);
    jdDrawValueCell(kJdCol2ValueX, kJdInfoRow1Y, kJdCol2ValueW, buf, UI_TEXT);
    s_disp.dutyPct = dutyPct;
    s_disp.events = eventCount;
  }

  if (full || s_disp.logOn != logEnabled) {
    const bool allowed = sdLoggingAllowed(LogApp::JamDetector);
    jdDrawValueCell(kJdCol2ValueX, kJdInfoRow2Y, kJdCol2ValueW,
                    !allowed ? "n/a" : (logEnabled ? "on" : "off"),
                    (allowed && logEnabled) ? UI_OK : UI_DIM_TEXT);
    s_disp.logOn = logEnabled;
  }

  s_disp.valid = true;
}

static void jdDrawWaveform(bool jam, bool activity) {
  const int plotX = kJdPad + 4;
  const int plotW = kJdPanelW - 8;
  if (jdPlotHeight() < 8 || plotW < 2 || kWaveW < 2) {
    return;
  }

  const uint16_t waveColor = jam ? ORANGE : (activity ? UI_WARN : UI_OK);

  if (s_waveHasPrev) {
    for (int x = 0; x < plotW - 1; x++) {
      const int px0 = (x * (kWaveW - 1)) / (plotW - 1);
      const int px1 = ((x + 1) * (kWaveW - 1)) / (plotW - 1);
      tft.drawLine(plotX + x, s_wavePrevY[px0], plotX + x + 1, s_wavePrevY[px1], TFT_BLACK);
    }
  }

  for (int i = 0; i < kWaveW; i++) {
    s_wavePrevY[i] = jdRssiToY(waveBuf[(waveWrite + i) % kWaveW]);
  }

  for (int x = 0; x < plotW - 1; x++) {
    const int i0 = (x * (kWaveW - 1)) / (plotW - 1);
    const int i1 = ((x + 1) * (kWaveW - 1)) / (plotW - 1);
    tft.drawLine(plotX + x, s_wavePrevY[i0], plotX + x + 1, s_wavePrevY[i1], waveColor);
  }

  s_waveHasPrev = true;
  jdDrawPlotGridLines();
}

struct WindowStat { int peakDbm; int minDbm; float duty; uint32_t elapsedMs; };

static WindowStat sampleWindow() {
  const int busyThresh = max(JD_ABS_THRESH_DBM, (int)(noiseFloor + JD_MARGIN_DB));
  int peak = -127, lo = 0;
  uint16_t busy = 0;
  const float kEwmaAlpha = 0.35f;

  const uint32_t t0 = millis();
  uint32_t micro_s = micros();
  for (int i = 0; i < JD_SAMPLES; i++) {
    const int dbm = ELECHOUSE_cc1101.getRssi();
    if (dbm > peak) peak = dbm;
    if (dbm < lo) lo = dbm;
    if (dbm > busyThresh) busy++;

    waveSmooth = (kEwmaAlpha * dbm) + ((1.0f - kEwmaAlpha) * waveSmooth);
    if ((i & 1) == 0) {
      waveBuf[waveWrite] = (int8_t)constrain((int)lroundf(waveSmooth), kWaveRssiMin, kWaveRssiMax);
      waveWrite = (waveWrite + 1) % kWaveW;
    }

    while (micros() < micro_s + samplingPeriod) {}
    micro_s += samplingPeriod;
  }

  WindowStat st;
  st.peakDbm = peak;
  st.minDbm = lo;
  st.duty = (float)busy / JD_SAMPLES;
  st.elapsedMs = millis() - t0;
  return st;
}

static void evaluate(const WindowStat& st) {
  if (st.duty < 0.2f) noiseFloor = 0.95f * noiseFloor + 0.05f * st.minDbm;

  dutyRing[dutyRingPos] = st.duty;
  dutyRingPos = (dutyRingPos + 1) % JD_RING;
  float avgDuty = 0;
  for (uint8_t i = 0; i < JD_RING; i++) avgDuty += dutyRing[i];
  avgDuty /= JD_RING;

  if (st.duty >= JD_BUSY_WIN_DUTY) busyStreakMs += st.elapsedMs;
  else busyStreakMs = 0;

  const bool jam = (busyStreakMs >= JD_JAM_STREAK_MS) || (avgDuty >= JD_JAM_AVG_DUTY);

  if (jam && !jamActive) {
    jamActive = true;
    jamStartMs = millis();
    jamPeakDbm = st.peakDbm;
    eventCount++;
    s_disp.events = 0xFFFFFFFFu;
  } else if (jam && jamActive) {
    if (st.peakDbm > jamPeakDbm) jamPeakDbm = st.peakDbm;
  } else if (!jam && jamActive) {
    jamActive = false;
    logEvent(jamStartMs, millis() - jamStartMs, jamPeakDbm, (int)(avgDuty * 100));
  }
}

static bool edge(int pin, bool& prev) {
  const bool now = isPhysicalButtonPressed(pin);
  const bool e = now && !prev;
  prev = now;
  return e;
}

static void handleInput() {
  const bool navFreqDown = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_LEFT);
  const bool navFreqUp = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_RIGHT);
  const bool navReset = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_UP);
  const bool navLog = featureHasTouchNavBar() && isTouchNavButtonPressedEdge(BTN_DOWN);

  if (edge(BTN_LEFT, prevLeft) || navFreqDown) tuneTo(freqIdx + kFreqCount - 1);
  if (edge(BTN_RIGHT, prevRight) || navFreqUp) tuneTo(freqIdx + 1);
  if (edge(BTN_UP, prevUp) || navReset) {
    jdResetStats();
  }
  if (edge(BTN_DOWN, prevDown) || navLog) {
    if (sdLoggingAllowed(LogApp::JamDetector)) {
      logEnabled = !logEnabled;
      s_disp.logOn = !logEnabled;
    }
  }
}

static void exitCleanup() {
  ELECHOUSE_cc1101.setSidle();
  restoreSdAfterSharedSpi();
}

void Setup() {
  if (!cc1101Ready("Jamming Detector")) return;

  setTouchButtonInputEnabled(true);
  setTouchNavLabels("Freq-", "Log", "Exit", "Reset", "Freq+");

  holdSdInactiveOnSharedSpi();
  reclaimSharedSpiBus();

#if defined(SD_CS)
  pinMode(SD_CS, OUTPUT);
  digitalWrite(SD_CS, HIGH);
#endif
#if defined(CC1101_CS)
  pinMode(CC1101_CS, OUTPUT);
  digitalWrite(CC1101_CS, HIGH);
#endif

  cc1101BeginRx();
  tuneTo(freqIdx);

  samplingPeriod = round(1000000.0 * (1.0 / JD_SAMPLE_HZ));

  noiseFloor = JD_FLOOR_INIT_DBM;
  busyStreakMs = 0;
  jamActive = false;
  logMounted = false;
  for (uint8_t i = 0; i < JD_RING; i++) dutyRing[i] = 0;
  prevLeft = prevRight = prevUp = prevDown = false;
  jdInvalidateAll();

#if HAS_PCF8574_BUTTONS
  pcf.pinMode(BTN_LEFT, INPUT_PULLUP);
  pcf.pinMode(BTN_RIGHT, INPUT_PULLUP);
  pcf.pinMode(BTN_UP, INPUT_PULLUP);
  pcf.pinMode(BTN_DOWN, INPUT_PULLUP);
  pcf.pinMode(BTN_SELECT, INPUT_PULLUP);
#endif

  tft.setRotation(TFT_ROTATION);
  subghzClearBody(TFT_BLACK);
  drawStatusBar(readBatteryVoltage(), true);
  subghzRedrawNavChrome();
  setupTouchscreen();
  jdRunUI();
  jdDrawStaticChrome();
  jdDrawStatusBox(false, false);
}

void Loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    exitCleanup();
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  jdRunUI();
  handleInput();

  const WindowStat st = sampleWindow();
  evaluate(st);

  const bool activity = !jamActive && (st.duty >= JD_ACTIVITY_DUTY);
  const int dutyPct = (int)(dutyRing[(dutyRingPos + JD_RING - 1) % JD_RING] * 100.0f);

  jdUpdateInfo(st.peakDbm, dutyPct);
  jdDrawStatusBox(jamActive, activity);
  jdDrawWaveform(jamActive, activity);
}

}  // namespace jammingdetector

namespace FreqScan {

/* Sweeping the frequency list and showing where the energy is.
 *
 * The problem this answers: Replay Attack wants a frequency chosen before
 * it will listen, and a remote on the wrong one is indistinguishable from
 * a remote that is not transmitting. Twelve guesses, each ambiguous.
 *
 * Peak hold is the part that makes it usable. A key fob transmits for
 * perhaps 200 ms; an instantaneous bar chart shows nothing by the time you
 * have looked from the remote to the screen. The peak decays slowly so a
 * burst stays readable and a steady carrier still stands out from it.
 *
 * Receive only: tune, read RSSI, repeat. Nothing to refuse under Stealth.
 */

constexpr int kRowH       = 13;
constexpr int kSettleUs   = 1200;   // after a retune, before the first read
constexpr int kSamples    = 6;      // per frequency per sweep
constexpr int kRssiFloor  = -110;   // bar empty at or below
constexpr int kRssiCeil   = -20;    // bar full at or above
constexpr int kDecayDbPerSweep = 1;

int8_t  s_peak[kSubghzFreqCount];
bool    s_held = false;
bool    s_needFull = true;
uint8_t s_strongest = 0;

static void resetPeaks() {
  for (size_t i = 0; i < kSubghzFreqCount; i++) {
    s_peak[i] = (int8_t)kRssiFloor;
  }
  s_strongest = 0;
}

static void sweep() {
  int best = kRssiFloor;
  uint8_t bestIdx = 0;

  for (size_t i = 0; i < kSubghzFreqCount; i++) {
    ELECHOUSE_cc1101.setSidle();
    ELECHOUSE_cc1101.setMHZ(subghz_frequency_list[i] / 1000000.0);
    ELECHOUSE_cc1101.SetRx();
    delayMicroseconds(kSettleUs);

    int peak = -127;
    for (int n = 0; n < kSamples; n++) {
      const int dbm = ELECHOUSE_cc1101.getRssi();
      if (dbm > peak) peak = dbm;
      delayMicroseconds(150);
    }
    if (peak < kRssiFloor) peak = kRssiFloor;
    if (peak > kRssiCeil)  peak = kRssiCeil;

    /* Hold the peak, and let it fall slowly rather than snapping back, so
     * a burst that has already ended is still visible. The decay is what
     * makes one array enough: a signal that stops is shown going away
     * rather than needing a separate live reading beside it. */
    if (peak > s_peak[i]) {
      s_peak[i] = (int8_t)peak;
    } else if (s_peak[i] > kRssiFloor) {
      s_peak[i] = (int8_t)(s_peak[i] - kDecayDbPerSweep);
    }

    if (s_peak[i] > best) {
      best = s_peak[i];
      bestIdx = (uint8_t)i;
    }
  }
  s_strongest = bestIdx;
}

static int barWidthFor(int dbm, int fullW) {
  if (dbm <= kRssiFloor) return 0;
  if (dbm >= kRssiCeil)  return fullW;
  return ((dbm - kRssiFloor) * fullW) / (kRssiCeil - kRssiFloor);
}

static void draw() {
  const int top    = 30 + replayat::yshift;
  const int bottom = subghzContentBottom();

  if (s_needFull) {
    subghzClearBody(TFT_BLACK);
    s_needFull = false;
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(UI_TEXT, TFT_BLACK);
    tft.setCursor(6, top - 12);
    tft.print("Freq Analyser");
    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(150, top - 12);
    tft.print("peak hold, dBm");
  }

  tft.setTextFont(1);
  tft.setTextSize(1);

  const int labelX = 6;
  const int barX   = 62;
  const int dbmX   = PUEO_SCREEN_W - 40;
  const int fullW  = dbmX - barX - 8;

  for (size_t i = 0; i < kSubghzFreqCount; i++) {
    const int y = top + (int)i * kRowH;
    if (y + kRowH > bottom) break;

    const bool best = ((uint8_t)i == s_strongest) &&
                      (s_peak[i] > kRssiFloor);

    tft.setTextColor(best ? UI_ICON : UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(labelX, y + 2);
    tft.printf("%7.2f", subghz_frequency_list[i] / 1000000.0);

    const int wPeak = barWidthFor(s_peak[i], fullW);

    /* Paint the bar, then black only the part beyond it. Clearing the
     * whole row first and filling it after blinks every bar on every
     * sweep, which at this rate reads as the display being broken. */
    if (wPeak > 0) {
      tft.fillRect(barX, y + 2, wPeak, 8, best ? UI_ICON : UI_DIM_TEXT);
      tft.drawFastVLine(barX + wPeak - 1, y + 1, 10, UI_WARN);
    }
    if (wPeak < fullW) {
      tft.fillRect(barX + wPeak, y + 1, fullW - wPeak, 10, TFT_BLACK);
    }

    tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
    tft.setCursor(dbmX, y + 2);
    tft.printf("%4d", (int)s_peak[i]);
  }

  tft.setTextColor(s_held ? UI_WARN : UI_DIM_TEXT, TFT_BLACK);
  tft.setCursor(6, bottom - 11);
  if (s_held) {
    tft.print("HELD                          ");
  } else {
    tft.printf("strongest %.2f MHz            ",
               subghz_frequency_list[s_strongest] / 1000000.0);
  }
}

static void handleNavButtons() {
  if (!featureHasTouchNavBar()) {
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_SELECT)) {
    feature_exit_requested = true;
    return;
  }
  if (isTouchNavButtonPressedEdge(BTN_UP)) {
    resetPeaks();
    s_needFull = true;
    subghzWaitNavRelease(BTN_UP);
  }
  if (isTouchNavButtonPressedEdge(BTN_RIGHT)) {
    s_held = !s_held;
    subghzWaitNavRelease(BTN_RIGHT);
  }
}

void setup() {
  if (!cc1101Ready("Freq Analyser")) return;

  setTouchButtonInputEnabled(true);
  setTouchNavLabels("", "Reset", "Exit", "", "Hold");

  reclaimSharedSpiBus();
  SpiBus::claim(SpiBus::Dev::Cc1101);

  ELECHOUSE_cc1101.setSpiPin(CC1101_SCK, CC1101_MISO, CC1101_MOSI, CC1101_CS);
  ELECHOUSE_cc1101.Init();
  ELECHOUSE_cc1101.setModulation(2);
  ELECHOUSE_cc1101.setGDO(CC1101_GDO0, CC1101_GDO2);
  ELECHOUSE_cc1101.SetRx();

  resetPeaks();
  s_held = false;
  s_needFull = true;

  subghzClearBody(TFT_BLACK);
  setupTouchscreen();
  float v = readBatteryVoltage();
  drawStatusBar(v, true);
  replayat::uiDrawn = false;
  replayat::runUI();
  subghzRedrawNavChrome();
}

void loop() {
  if (feature_active && (feature_exit_requested || featureExitButtonPressed())) {
    feature_exit_requested = true;
    return;
  }

  maintainTouchNavBar();
  replayat::runUI();
  handleNavButtons();

  if (!s_held) {
    sweep();
  }
  draw();
  subghzRedrawNavChrome();
  delay(10);
}

}  // namespace FreqScan
