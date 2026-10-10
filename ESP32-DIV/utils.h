#ifndef UTILS_H
#define UTILS_H

#include "Branding.h"

#include <SD.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
#include <XPT2046_Touchscreen.h>
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "shared.h"

#ifdef TFT_GREEN
#undef TFT_GREEN
#endif
#define TFT_GREEN GREEN

extern TFT_eSPI tft;

// Obfuscated-string helpers (XOR decode, then print).
void tftPrintObf(const uint8_t* data, size_t len, uint8_t key = k0());
void tftPrintlnObf(const uint8_t* data, size_t len, uint8_t key = k0());
void serialPrintObf(const uint8_t* data, size_t len, bool newline = false, uint8_t key = k0());

void updateStatusBar();
/** Stop GPS wardriver background task before WiFi/BLE features use the radio. */
void pauseBackgroundRadioTasks();
float readBatteryVoltage();
float readInternalTemperature();
bool isSDCardAvailable();
/** After SPI is used for another device (nRF24 / CC1101 / PN532), restore pins,
 *  deselect other chip-selects, and remount SD. No GPIO pin remapping. */
void restoreSdAfterSharedSpi();
/** Reclaim shared SPI pins for CC1101/nRF without mounting SD (SD.begin raises
 *  the SPI clock and can leave the bus unusable for SubGHz until reboot). */
void reclaimSharedSpiBus();
/** Hold SD CS high and set a CC1101-safe SPI rate on the shared bus. */
void holdSdInactiveOnSharedSpi();
void drawStatusBar(float batteryVoltage, bool forceUpdate = false, bool bottomSeparator = false);

/* Set by whichever function paints the screen, immediately before it forces
 * the bar to redraw. Shared state rather than an argument because the bar is
 * also repainted asynchronously by statusBarTask, which has no idea what is
 * on screen and would otherwise repaint a tall bar at the short height and
 * leave a 14 px orphan stripe behind it.
 *
 * On a panel with only one height this is a no-op that the compiler deletes,
 * so the bar's geometry stays the compile-time constant it has always been
 * there and that image rebuilds to the same bytes. */
#include "UiLine.h"   // uiShowLine, shared with the beacon sketch

/* A short tone, on boards that can make one.
 *
 * Blocking, because every caller is an alert: something worth a noise is
 * worth 150 ms. On a board with no amplifier this compiles to nothing.
 *
 * The enable is raised, given a moment to settle, and dropped again after.
 * Leaving it high idles the amplifier on a rail the display is already
 * using; keying it fast clicks. Both are in board_pueo.h's notes. */
void uiBeep(uint16_t hz = 2200, uint16_t ms = 150);

/* A feature's own mark, held for a beat as it opens.
 *
 * Upstream's features open on the skull animation. Pueo's own open on their
 * own artwork, and for less time: 800 ms against the skull's three seconds,
 * because this is a signature rather than a loading bar -- nothing is being
 * waited on while it shows. */
void showFeatureMark(const unsigned char* bitmap, const char* caption,
                     int holdMs = PUEO_MARK_HOLD_MS);

/* drawBitmap() with an integer scale. scale 1 is drawBitmap itself.
 *
 * TFT_eSPI cannot scale a bitmap, and the icons are 1-bit 16x16 blobs in
 * icon.h. Rather than redraw 143 of them at 32x32, this doubles the bits
 * into a scratch bitmap and makes one drawBitmap call -- a fillRect per lit
 * pixel would be up to 256 SPI transactions per icon, eight icons a grid. */
void drawBitmapScaled(int16_t x, int16_t y, const uint8_t* bmp,
                      int16_t w, int16_t h, uint16_t colour, uint8_t scale);

#if PUEO_STATUS_TALL == PUEO_STATUS_SHORT
inline void setStatusBarHeight(int) {}
#else
void setStatusBarHeight(int height);
#endif
void startStatusBarTask();
/** Request a status bar pass on the next update (e.g. after SD or ward state changes). */
void requestStatusBarRedraw();

/** Init NimBLE once (releases unused Classic BT RAM first). Safe to call repeatedly. */
bool ensureBleStackReady();

/** Allow another SD mount attempt (clears v1 boot "gave up" latch). */
void sdRetryMount();
/** Create a directory and every parent it needs. SD.mkdir does one level,
 *  which stopped being enough when every path moved under PUEO_DIR. */
bool sdEnsureDir(const char* path);

extern bool feature_exit_requested;

extern void setBrightness(uint8_t value);
bool isButtonPressed(int buttonPin);
/* Spin-free wait for release. Gives up after timeoutMs rather than
 * hanging the device on a button that never clears. */
bool waitForButtonRelease(int buttonPin, uint32_t timeoutMs = 5000);
/** True while the PCF8574 button for this pin is held (no touch nav). */
bool isPhysicalButtonPressed(int buttonPin);
/** True while a touch nav slot for this pin is held (ignores physical buttons). */
bool isTouchNavButtonPressed(int buttonPin);
/** True once per touch nav press (ignores physical buttons). */
bool isTouchNavButtonPressedEdge(int buttonPin);
/** True once per press (touch nav edge or PCF falling edge). */
bool isButtonPressedEdge(int buttonPin);
/** Exit/back: level-sensitive so slow feature loops still catch PCF + touch nav. */
bool featureExitButtonPressed();
void setTouchButtonInputEnabled(bool enabled);
void drawTouchButtonCue();
void invalidateTouchButtonCue();
void redrawTouchButtonBar();
/** Clear touch-nav edge state after a full-screen overlay (keyboard, etc.). */
void resetTouchNavHeldState();
/** Draw the touch nav bar once (no-op if already drawn). */
void maintainTouchNavBar();
/** Pixels reserved at the bottom for the touch nav bar (0 when hidden). */
int16_t touchNavReservedHeight();
/** Lowest Y coordinate features should draw into (exclusive of nav bar). */
int16_t touchNavContentBottomY();
/** Clear feature body only; leaves the touch nav bar intact. */
void featureClearContent(uint16_t color = BLACK);
/** True when the on-screen touch nav bar is active for the current feature. */
bool featureHasTouchNavBar();
/** Override nav slots with text labels (nullptr = default icon for that slot). */
void setTouchNavLabels(const char* left, const char* down, const char* center,
                       const char* up, const char* right);
bool initPcf8574Buttons();
uint8_t getPcf8574Address();
void sdSpiInit();
bool sdMountChipSelect(uint8_t cs);

extern float currentBatteryVoltage;

void initDisplay();
void showNotification(const char* title, const char* message);
void hideNotification();

enum class NotificationAction : uint8_t { None, Close, Ok, Save };
void showNotificationActions(const char* title, const char* message, bool showSave);
bool isNotificationVisible();
/** Repaint the modal after a full-screen or body redraw covered it (same cached title/message). */
void notificationRedrawIfVisible();
NotificationAction notificationHandleTouch(int x, int y);
void printWrappedText(int x, int y, int maxWidth, const char* text);
void loading(int frameDelay, uint16_t color, int16_t x, int16_t y, int repeats, bool center);
void displayLogo(uint16_t color, int displayTime);
/* The boot splash: grid, spectrum, mark and the [boot] log. Replaces the
 * skull animation and displayLogo() at startup. */
void bootSplash();
void initSDCard();

namespace AppSettingsUI{ void setup(); void loop(); }
namespace TouchCalib{ void setup(); void loop(); }

namespace Terminal {
  void terminalSetup();
  void terminalLoop();
}

/* Wiping Pueo's own directories on the card. Never the card itself: the
 * guard that enforces that lives in utils.cpp beside the recursive
 * remove. */
namespace SdReset {
  void setup();
  void loop();
}

namespace SdFileManager {
  void setup();
  void loop();
}

namespace FeatureUI {
  enum class ButtonStyle : uint8_t { Primary, Secondary, Danger };

  struct Button {
    int16_t x, y, w, h;
    const char* label;
    ButtonStyle style;
    bool disabled;
  };

  constexpr int16_t FOOTER_H = 34;
  constexpr int16_t BTN_H    = 26;
  constexpr int16_t PAD_X    = 8;
  constexpr int16_t GAP_X    = 8;

  void drawFooterBg();

  void drawButtonRect(int x, int y, int w, int h,
                      const char* label,
                      ButtonStyle style,
                      bool pressed = false,
                      bool disabled = false,
                      uint8_t font = 2);

  inline void drawButton(const Button& b, bool pressed = false) {
    drawButtonRect(b.x, b.y, b.w, b.h, b.label, b.style, pressed, b.disabled);
  }

  void layoutFooter3(Button (&btns)[3],
                     const char* l0, ButtonStyle s0,
                     const char* l1, ButtonStyle s1,
                     const char* l2, ButtonStyle s2,
                     bool d0=false, bool d1=false, bool d2=false);
  void layoutFooter2(Button (&btns)[2],
                     const char* l0, ButtonStyle s0,
                     const char* l1, ButtonStyle s1,
                     bool d0=false, bool d1=false);
  void layoutFooter4(Button (&btns)[4],
                     const char* l0, ButtonStyle s0,
                     const char* l1, ButtonStyle s1,
                     const char* l2, ButtonStyle s2,
                     const char* l3, ButtonStyle s3,
                     bool d0=false, bool d1=false, bool d2=false, bool d3=false);
  void layoutFooter1(Button& btn, const char* label, ButtonStyle style, bool disabled=false);
  void layoutFooter5(Button (&btns)[5],
                     const char* l0, ButtonStyle s0,
                     const char* l1, ButtonStyle s1,
                     const char* l2, ButtonStyle s2,
                     const char* l3, ButtonStyle s3,
                     const char* l4, ButtonStyle s4,
                     bool d0=false, bool d1=false, bool d2=false, bool d3=false, bool d4=false);

  int hit(const Button* btns, int n, int x, int y);
}

#endif
 