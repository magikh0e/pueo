#include <Arduino.h>
#include <PCF8574.h>
#include <TFT_eSPI.h>
#include <Wire.h>
#include "BootLock.h"
#include "Stealth.h"
#include "SettingsStore.h"
#include "Touchscreen.h"
#include "config.h"
#include "FastPairScan.h"
#include "DroneScan.h"
#include "Spotter.h"
#include "ApTracker.h"
#include "FileServer.h"
#include "TrackerHunt.h"
#include "ducky.h"
#include "Branding.h"
#include "icon.h"
#include "gps.h"
#include "rfid.h"
#include "shared.h"
#include "utils.h"

#if !BOARD_HAS_ESP32S3
#include "soc/soc.h"
#include "soc/rtc_cntl_reg.h"
#endif

TFT_eSPI tft = TFT_eSPI();

PCF8574 pcf(PCF8574_I2C_ADDR);

void setBrightness(uint8_t value) {
  ledcWrite(PWM_CHANNEL, value);
}

bool feature_exit_requested = false;

/* The main menu, in COLUMN-MAJOR order. displayMenu() lays it out with
 * column = i / 4 and row = i % 4, so 0..3 are the left column top to bottom
 * and 4..7 are the right. Read as a list this looks shuffled; read as two
 * columns it is what is on the screen. Adding an entry means thinking about
 * which column it lands in, not appending.
 *
 * "NRF24" was "2.4GHz" until 0.4.11. It is the Nordic module -- Scanner,
 * Proto Kill, ESB Sniffer, MouseJack -- and naming it by its band put it in
 * competition with the two tiles above it, which are also 2.4 GHz radios.
 * Somebody looking for the BLE jammer had a sound reason to open it. */
const int NUM_MENU_ITEMS = 8;
const char *menu_items[NUM_MENU_ITEMS] = {
    "WiFi",
    "NRF24",
    "Detect",
    "GPS",
    "Bluetooth",
    "SubGHz",
    "RFID/NFC",
    "System"};

/* These names are upstream's and several no longer describe where they are
 * used: the NRF24 tile wears bitmap_icon_jammer, Bluetooth wears
 * bitmap_icon_spoofer, SubGHz wears bitmap_icon_analyzer. They render
 * correctly -- they are generic glyphs, reused across menus -- so they are
 * not renamed, because the other use sites would then be the misleading
 * ones. Read the position, not the name. */
const unsigned char *bitmap_icons[NUM_MENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_jammer,
    bitmap_icon_eye,
    bitmap_icon_satellite,
    bitmap_icon_spoofer,
    bitmap_icon_analyzer,
    bitmap_icon_rfid_chip,
    bitmap_icon_setting};

int current_menu_index = 0;
bool is_main_menu = false;

const int NUM_SUBMENU_ITEMS = 12;
const char *submenu_items[NUM_SUBMENU_ITEMS] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner",
    "Captive Portal",
    "Hidden SSID Revealer",
    "WPS Scanner",
    "ARP Scanner",
    "Karma Attack",
    "Back to Main Menu"};

// WiFi submenu is split across two pages (features after Hidden SSID on page 2).
// Bottom row: icon | Main Menu                 Next/Prev Page | icon
static constexpr int WIFI_PAGE0_FEATURES = 6;
static constexpr int WIFI_PAGE1_FEATURES = 6;
static int wifi_submenu_page = 0;

const char *wifi_page0_items[WIFI_PAGE0_FEATURES] = {
    "Packet Monitor",
    "Beacon Spammer",
    "WiFi Deauther",
    "Probe Request Flood",
    "Deauth Detector",
    "WiFi Scanner"};

const char *wifi_page1_items[WIFI_PAGE1_FEATURES] = {
    "Captive Portal",
    "Hidden SSID Revealer",
    "WPS Scanner",
    "ARP Scanner",
    "Karma Attack",
    "AP Tracker"};

// Bluetooth submenu uses the same paged footer layout as WiFi.
static constexpr int BT_PAGE0_FEATURES = 6;
static constexpr int BT_PAGE1_FEATURES = 5;
static int bluetooth_submenu_page = 0;

const char *bluetooth_page0_items[BT_PAGE0_FEATURES] = {
    "BLE Jammer",
    "BLE Spoofer",
    "Sour Apple",
    "AirTag Spoofer",
    "AirTag Sniffer",
    "Sniffer"};

const char *bluetooth_page1_items[BT_PAGE1_FEATURES] = {
    "BLE Scanner",
    "BLE Rubber Ducky",
    "Skimmer Detect",
    "Hunt",
    "Fast Pair"};

static FeatureUI::Button s_pagedFooterBtns[2];
static int s_pagedFooterFocus = -1;  // 0=back, 1=page btn, -1=none

const int nrf_NUM_SUBMENU_ITEMS = 7;
const char *nrf_submenu_items[nrf_NUM_SUBMENU_ITEMS] = {
    "Scanner",
    "Proto Kill",
    "ESB Sniffer",
    "ESB Replay",
    "MouseJack Scan",
    "MouseJack Inject",
    "Back to Main Menu"};

const int subghz_NUM_SUBMENU_ITEMS = 9;
const char *subghz_submenu_items[subghz_NUM_SUBMENU_ITEMS] = {
    "Replay Attack",
    "SubGHz Jammer",
    "De Bruijn / Brute",
    "Jamming Detector",
    "Saved Profile",
    "Import .sub",
    "Export .sub",
    "Freq Analyser",
    "Back to Main Menu"};

/* System is the old Tools with Settings and About folded in. They were two
 * whole tiles of an eight tile grid, both of them things nobody opens in the
 * middle of a session, sitting at the same visual weight as the radios. The
 * two slots they free are what RFID/NFC and GPS were promoted into. */
const int tools_NUM_SUBMENU_ITEMS = 9;
const char *tools_submenu_items[tools_NUM_SUBMENU_ITEMS] = {
    "Serial Monitor",
    "Update Firmware",
    "Touch Calibrate",
    "SD File Manager",
    "Reset SD",
    /* Next to the file manager rather than under WiFi. The radio is how it
     * works, not what it is for: somebody looking for a way to get a capture
     * off the card looks where the card is. */
    "File Transfer",
    "Settings",
    "About",
    "Back to Main Menu"};

/* Detect was More until 0.4.11, and More held four things that were not one
 * category: two peripheral radios and two passive detectors. RFID/NFC and
 * GPS are now tiles of their own, which is what the old sub-layer machinery
 * was standing in for, and what is left here is the pair that belong
 * together. Surveillance and the drone detector are also what this fork is
 * for, and they were two taps in behind a label that said nothing. */
const int other_NUM_SUBMENU_ITEMS = 3;
static constexpr int OTHER_GRID_COLS = 2;
const char *other_submenu_items[other_NUM_SUBMENU_ITEMS] = {
    "Surveillance",
    "Drone Detector",
    "Main Menu"};

const int rfid_NUM_SUBMENU_ITEMS = 9;
const char *rfid_submenu_items[rfid_NUM_SUBMENU_ITEMS] = {
    "Card Reader",
    "Card Clone",
    "Erase",
    "Dump",
    "Decode Access",
    "Jam Reader",
    "Tag Disrupt",
    "Disrupt Emulate",
    "Back to Main Menu"};

const int gps_NUM_SUBMENU_ITEMS = 3;
const char *gps_submenu_items[gps_NUM_SUBMENU_ITEMS] = {
    "Wardriver",
    "Satellite Scanner",
    "Back to Main Menu"};

const int about_NUM_SUBMENU_ITEMS = 1;
const char *about_submenu_items[about_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

const int setting_NUM_SUBMENU_ITEMS = 1;
const char *setting_submenu_items[setting_NUM_SUBMENU_ITEMS] = {
    "Back to Main Menu"};

int current_submenu_index = 0;
bool in_sub_menu = false;
int last_submenu_index = -1;
bool submenu_initialized = false;
int last_other_menu_index = -1;
bool other_menu_grid_initialized = false;

const char **active_submenu_items = nullptr;
int active_submenu_size = 0;

const unsigned char *wifi_submenu_icons[NUM_SUBMENU_ITEMS] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer,
    bitmap_icon_bash,
    bitmap_icon_eye_blind,
    bitmap_icon_key,
    bitmap_icon_list,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *wifi_page0_icons[WIFI_PAGE0_FEATURES] = {
    bitmap_icon_wifi,
    bitmap_icon_antenna,
    bitmap_icon_wifi_jammer,
    bitmap_icon_Skull_3,
    bitmap_icon_eye2,
    bitmap_icon_jammer
};

const unsigned char *wifi_page1_icons[WIFI_PAGE1_FEATURES] = {
    bitmap_icon_bash,
    bitmap_icon_eye_blind,
    bitmap_icon_key,
    bitmap_icon_list,
    bitmap_icon_devil,
    bitmap_icon_compass
};

const unsigned char *bluetooth_page0_icons[BT_PAGE0_FEATURES] = {
    bitmap_icon_ble_jammer,
    bitmap_icon_spoofer,
    bitmap_icon_apple,
    bitmap_icon_tags,
    bitmap_icon_magnifying_glass,
    bitmap_icon_analyzer
};

const unsigned char *bluetooth_page1_icons[BT_PAGE1_FEATURES] = {
    bitmap_icon_graph,
    bitmap_icon_rubber_ducky,
    bitmap_icon_Wireless_4,
    bitmap_icon_compass,
    bitmap_icon_ble
};

const unsigned char *nrf_submenu_icons[nrf_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_scanner,
    bitmap_icon_kill,
    bitmap_icon_analyzer,
    bitmap_icon_follow,
    bitmap_icon_magnifying_glass,
    bitmap_icon_key,
    bitmap_icon_go_back
};

const unsigned char *subghz_submenu_icons[subghz_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_antenna,
    bitmap_icon_no_signal,
    bitmap_icon_graph_self_loop,
    bitmap_icon_Voice_Id,
    bitmap_icon_list,
    bitmap_icon_sdcard,
    bitmap_icon_Floppy_Disk_3,
    bitmap_icon_window_oscillograph,
    bitmap_icon_go_back
};

const unsigned char *tools_submenu_icons[tools_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_bash,
    bitmap_icon_follow,
    bitmap_icon_undo,
    bitmap_icon_sdcard,
    bitmap_icon_sign_forbidden,
    bitmap_icon_wifi2,
    bitmap_icon_setting,
    bitmap_icon_question,
    bitmap_icon_go_back
};

const unsigned char *other_submenu_icons[other_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_eye,
    bitmap_icon_satellite,
    bitmap_icon_go_back
};

const unsigned char *rfid_submenu_icons[rfid_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_magnifying_glass,
    bitmap_icon_follow,
    bitmap_icon_recycle,
    bitmap_icon_dot_matrix,
    bitmap_icon_key,
    bitmap_icon_kill,
    bitmap_icon_flash,
    bitmap_icon_devil,
    bitmap_icon_go_back
};

const unsigned char *gps_submenu_icons[gps_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_satellite,
    bitmap_icon_satellite_dish,
    bitmap_icon_go_back
};

const unsigned char *about_submenu_icons[about_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char *setting_submenu_icons[setting_NUM_SUBMENU_ITEMS] = {
    bitmap_icon_go_back
};

const unsigned char **active_submenu_icons = nullptr;

static int wifiFeatureCount() {
    return (wifi_submenu_page == 0) ? WIFI_PAGE0_FEATURES : WIFI_PAGE1_FEATURES;
}

static int bluetoothFeatureCount() {
    return (bluetooth_submenu_page == 0) ? BT_PAGE0_FEATURES : BT_PAGE1_FEATURES;
}

static int pagedFeatureCount() {
    if (current_menu_index == 4) {
        return bluetoothFeatureCount();
    }
    return wifiFeatureCount();
}

static int* pagedSubmenuPage() {
    return (current_menu_index == 4) ? &bluetooth_submenu_page : &wifi_submenu_page;
}

/* ── Both pages, as one grid ──────────────────────────────────────────────
 *
 * WiFi is twelve features and Bluetooth eleven, and both fit in the grid's
 * fifteen slots, so neither needs a second page. The page variables stay.
 *
 * That is deliberate rather than lazy. Which feature a tap launches is
 * decided by about ninety tests of the form
 *
 *     if (wifi_submenu_page == 0 && current_submenu_index == 3)
 *
 * scattered through the dispatch, and rewriting all of them into one index
 * space is the kind of change that silently launches the wrong feature. So
 * the grid is a view over both pages: tile 7 sets page 1, index 1, and every
 * one of those tests goes on reading what it always read.
 *
 * Nothing on screen has a page any more. The variable is an implementation
 * detail of the dispatch now, which is where it can stay until that is worth
 * untangling on its own.
 */
static int pagedTotalFeatures() {
    return (current_menu_index == 4)
        ? (BT_PAGE0_FEATURES + BT_PAGE1_FEATURES)
        : (WIFI_PAGE0_FEATURES + WIFI_PAGE1_FEATURES);
}

static int pagedFirstPageCount() {
    return (current_menu_index == 4) ? BT_PAGE0_FEATURES : WIFI_PAGE0_FEATURES;
}

/* Tile index -> (page, index on that page). */
static void pagedSplit(int tile, int &page, int &idx) {
    const int first = pagedFirstPageCount();
    if (tile < first) { page = 0; idx = tile; }
    else              { page = 1; idx = tile - first; }
}

static const char* pagedItemAt(int tile) {
    int page, idx;
    pagedSplit(tile, page, idx);
    if (current_menu_index == 4) {
        return page ? bluetooth_page1_items[idx] : bluetooth_page0_items[idx];
    }
    return page ? wifi_page1_items[idx] : wifi_page0_items[idx];
}

/* Which tile the last tap chose, in the page-local index the dispatch uses.
 * -1 when the tap was not on a tile. */
static int s_gridTapIndex = -1;

static void applyPagedSubmenuPage() {
    if (current_menu_index == 4) {
        applyBluetoothSubmenuPage();
    } else {
        applyWifiSubmenuPage();
    }
}

static const unsigned char* pagedIconAt(int tile) {
    int page, idx;
    pagedSplit(tile, page, idx);
    if (current_menu_index == 4) {
        return page ? bluetooth_page1_icons[idx] : bluetooth_page0_icons[idx];
    }
    return page ? wifi_page1_icons[idx] : wifi_page0_icons[idx];
}

// Bottom row: [icon | Main Menu] ........ [Next/Prev Page | icon]
static int pagedBackBtnIndex() {
    return pagedFeatureCount();
}

static int pagedPageBtnIndex() {
    return pagedFeatureCount() + 1;
}

static int pagedNavRowY() {
    return tft.height() - 30;
}

static const char* pagedPageBtnLabel() {
    return (*pagedSubmenuPage() == 0) ? "Next Page" : "Prev Page";
}

static const unsigned char* pagedPageBtnIcon() {
    return (*pagedSubmenuPage() == 0) ? bitmap_icon_navigate_right : bitmap_icon_navigate_left;
}

static void layoutPagedFooterButtons() {
    const int y = pagedNavRowY();
    const int mid = tft.width() / 2;
    s_pagedFooterBtns[0] = {
        0, (int16_t)y, (int16_t)mid, 28,
        "Main Menu", FeatureUI::ButtonStyle::Secondary, false};
    s_pagedFooterBtns[1] = {
        (int16_t)mid, (int16_t)y, (int16_t)(tft.width() - mid), 28,
        pagedPageBtnLabel(), FeatureUI::ButtonStyle::Secondary, false};
}

static void drawPagedFooterButtons() {
    layoutPagedFooterButtons();
    const int y = pagedNavRowY();
    const int rowH = 28;
    const int iconSize = 16;
    tft.fillRect(0, y, tft.width(), rowH, UI_BG);

    tft.setTextDatum(TL_DATUM);
    tft.setTextFont(2);
    tft.setTextSize(1);
    // Font 2 is ~16px; center icon + text on the same midline within the row.
    const int textH = 16;
    const int iconY = y + (rowH - iconSize) / 2;
    const int textY = y + (rowH - textH) / 2;

    {
        const uint16_t color = (s_pagedFooterFocus == 0) ? UI_ICON : UI_TEXT;
        tft.setTextColor(color, UI_BG);
        tft.drawBitmap(10, iconY, bitmap_icon_go_back, iconSize, iconSize, color);
        tft.setCursor(30, textY);
        tft.print("Main Menu");
    }

    {
        const uint16_t color = (s_pagedFooterFocus == 1) ? UI_ICON : UI_TEXT;
        const char* label = pagedPageBtnLabel();
        const int gap = 4;
        const int textW = tft.textWidth(label);
        // Right-aligned group: [label][gap][icon] — same vertical midline.
        const int iconX = tft.width() - 10 - iconSize;
        const int textX = iconX - gap - textW;
        tft.setTextColor(color, UI_BG);
        tft.setCursor(textX, textY);
        tft.print(label);
        tft.drawBitmap(iconX, iconY, pagedPageBtnIcon(), iconSize, iconSize, color);
    }
}

static void applyWifiSubmenuPage() {
    if (wifi_submenu_page == 0) {
        active_submenu_items = wifi_page0_items;
        active_submenu_icons = wifi_page0_icons;
    } else {
        active_submenu_items = wifi_page1_items;
        active_submenu_icons = wifi_page1_icons;
    }
    active_submenu_size = wifiFeatureCount() + 2;
    if (current_submenu_index >= active_submenu_size) {
        current_submenu_index = 0;
    }
    s_pagedFooterFocus = -1;
    last_submenu_index = -1;
    submenu_initialized = false;
}

static void applyBluetoothSubmenuPage() {
    if (bluetooth_submenu_page == 0) {
        active_submenu_items = bluetooth_page0_items;
        active_submenu_icons = bluetooth_page0_icons;
    } else {
        active_submenu_items = bluetooth_page1_items;
        active_submenu_icons = bluetooth_page1_icons;
    }
    active_submenu_size = bluetoothFeatureCount() + 2;
    if (current_submenu_index >= active_submenu_size) {
        current_submenu_index = 0;
    }
    s_pagedFooterFocus = -1;
    last_submenu_index = -1;
    submenu_initialized = false;
}

void updateActiveSubmenu() {
    switch (current_menu_index) {
        case 0:
            wifi_submenu_page = 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            break;
        case 1:
            active_submenu_items = nrf_submenu_items;
            active_submenu_size = nrf_NUM_SUBMENU_ITEMS;
            active_submenu_icons = nrf_submenu_icons;
            break;
        case 2:
            active_submenu_items = other_submenu_items;
            active_submenu_size = other_NUM_SUBMENU_ITEMS;
            active_submenu_icons = other_submenu_icons;
            break;
        case 3:
            active_submenu_items = gps_submenu_items;
            active_submenu_size = gps_NUM_SUBMENU_ITEMS;
            active_submenu_icons = gps_submenu_icons;
            break;
        case 4:
            bluetooth_submenu_page = 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            break;
        case 5:
            active_submenu_items = subghz_submenu_items;
            active_submenu_size = subghz_NUM_SUBMENU_ITEMS;
            active_submenu_icons = subghz_submenu_icons;
            break;
        case 6:
            active_submenu_items = rfid_submenu_items;
            active_submenu_size = rfid_NUM_SUBMENU_ITEMS;
            active_submenu_icons = rfid_submenu_icons;
            break;
        case 7:
            active_submenu_items = tools_submenu_items;
            active_submenu_size = tools_NUM_SUBMENU_ITEMS;
            active_submenu_icons = tools_submenu_icons;
            break;

        default:
            active_submenu_items = nullptr;
            active_submenu_size = 0;
            active_submenu_icons = nullptr;
            break;
    }
}

static bool touchButtonInputEnabled = false;
static bool touchButtonCueDrawn = false;
static bool s_touchNavLabelsConfigured = false;
static bool s_touchNavHeld[5] = {false, false, false, false, false};
#if HAS_PCF8574_BUTTONS
static bool s_pcfButtonLastState[8] = {true, true, true, true, true, true, true, true};
#endif
static FeatureUI::Button s_touchNavBtns[5];
static const char* s_touchNavLabels[5] = {nullptr, nullptr, nullptr, nullptr, nullptr};
static constexpr int16_t TOUCH_NAV_BAR_H = (FeatureUI::FOOTER_H * 4) / 5;  // 20% shorter than footer

static int touchNavPinForIndex(int idx) {
  switch (idx) {
    case 0: return BTN_LEFT;
    case 1: return BTN_DOWN;
    case 2: return BTN_SELECT;
    case 3: return BTN_UP;
    case 4: return BTN_RIGHT;
    default: return -1;
  }
}

void setTouchButtonInputEnabled(bool enabled) {
  if (touchButtonInputEnabled != enabled) {
    touchButtonCueDrawn = false;
    if (!enabled) {
      for (int i = 0; i < 5; ++i) {
        s_touchNavLabels[i] = nullptr;
      }
      s_touchNavLabelsConfigured = false;
      for (int i = 0; i < 5; ++i) {
        s_touchNavHeld[i] = false;
      }
    }
  }
  touchButtonInputEnabled = enabled;
#if TOUCH_BUTTON_CUE_ENABLED
  if (enabled && feature_active) {
    drawTouchNavBar();
    touchButtonCueDrawn = true;
  }
#endif
}

bool featureHasTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  return touchButtonInputEnabled && feature_active;
#else
  return false;
#endif
}

void setTouchNavLabels(const char* left, const char* down, const char* center,
                       const char* up, const char* right) {
  s_touchNavLabels[0] = left;
  s_touchNavLabels[1] = down;
  s_touchNavLabels[2] = center;
  s_touchNavLabels[3] = up;
  s_touchNavLabels[4] = right;
  s_touchNavLabelsConfigured = true;
  invalidateTouchButtonCue();
}

void invalidateTouchButtonCue() {
  touchButtonCueDrawn = false;
}

void resetTouchNavHeldState() {
  for (int i = 0; i < 5; ++i) {
    s_touchNavHeld[i] = false;
  }
}

void redrawTouchButtonBar() {
  invalidateTouchButtonCue();
  drawTouchButtonCue();
}

static void layoutTouchNavBtns() {
  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int totalW = tft.width();
  const int cellW = totalW / 5;

  for (int i = 0; i < 5; ++i) {
    const int x = i * cellW;
    const int w = (i == 4) ? (totalW - x) : cellW;
    s_touchNavBtns[i] = {
      (int16_t)x, (int16_t)barY, (int16_t)w, (int16_t)barH,
      nullptr, FeatureUI::ButtonStyle::Secondary, false};
  }
}

static String fitTouchNavLabel(const char* label, int maxWidth) {
  if (!label || !label[0]) {
    return String();
  }
  String out = label;
  if (tft.textWidth(out) <= maxWidth) {
    return out;
  }
  while (out.length() > 1 && tft.textWidth(out + "...") > maxWidth) {
    out.remove(out.length() - 1);
  }
  return out + "...";
}

static void drawTouchNavBar() {
  static const unsigned char* kIcons[5] = {
    bitmap_icon_LEFT,
    bitmap_icon_DOWN,
    bitmap_icon_go_back,
    bitmap_icon_UP,
    bitmap_icon_RIGHT,
  };
  constexpr int kIconSize = 16;

  const int barY = tft.height() - TOUCH_NAV_BAR_H;
  const int barH = TOUCH_NAV_BAR_H;
  const int barW = tft.width();

  layoutTouchNavBtns();

  tft.fillRect(0, barY, barW, barH, UI_FG);
  tft.drawFastHLine(0, barY, barW, UI_LINE);

  for (int i = 0; i < 5; ++i) {
    const auto& b = s_touchNavBtns[i];
    if (i > 0) {
      tft.drawFastVLine(b.x, barY + 3, barH - 6, UI_LINE);
    }

    if (s_touchNavLabels[i] && s_touchNavLabels[i][0]) {
      tft.setTextDatum(MC_DATUM);
      const uint16_t txtColor = (i == 2) ? UI_ICON : UI_TEXT;
      tft.setTextColor(txtColor, UI_FG);
      const String fit = fitTouchNavLabel(s_touchNavLabels[i], b.w - 8);
      tft.drawString(fit, b.x + b.w / 2, b.y + b.h / 2, 1);
    } else {
      const int ix = b.x + (b.w - kIconSize) / 2;
      const int iy = b.y + (b.h - kIconSize) / 2;
      const bool inactiveSlot = s_touchNavLabelsConfigured && !s_touchNavLabels[i];
      const unsigned char* icon = inactiveSlot ? bitmap_icon_dots : kIcons[i];
      const uint16_t iconColor = inactiveSlot ? LIGHT_GRAY : ((i == 2) ? UI_ICON : UI_TEXT);
      tft.drawBitmap(ix, iy, icon, kIconSize, kIconSize, iconColor);
    }
  }

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);
  tft.setTextSize(1);
  tft.setTextColor(UI_TEXT, FEATURE_BG);
}

void maintainTouchNavBar() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active || touchButtonCueDrawn) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

void featureClearContent(uint16_t color) {
  const int bottom = touchNavContentBottomY();
  if (bottom > 0) {
    tft.fillRect(0, 0, tft.width(), bottom, color);
  } else {
    tft.fillScreen(color);
  }
}

int16_t touchNavReservedHeight() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (touchButtonInputEnabled && feature_active) {
    return TOUCH_NAV_BAR_H;
  }
#endif
  return 0;
}

int16_t touchNavContentBottomY() {
  return (int16_t)(tft.height() - touchNavReservedHeight());
}

void drawTouchButtonCue() {
#if TOUCH_BUTTON_CUE_ENABLED
  if (!touchButtonInputEnabled || !feature_active) {
    return;
  }
  drawTouchNavBar();
  touchButtonCueDrawn = true;
#endif
}

static int touchNavIndexForPin(int buttonPin) {
  for (int i = 0; i < 5; ++i) {
    if (touchNavPinForIndex(i) == buttonPin) {
      return i;
    }
  }
  return -1;
}

static bool isTouchNavSlotDown(int idx) {
  if (idx < 0 || !feature_active || !touchButtonInputEnabled) {
    return false;
  }

  int x = 0;
  int y = 0;
  if (!readTouchXYDismiss(x, y)) {
    return false;
  }

  layoutTouchNavBtns();

  const int stripTop = tft.height() - TOUCH_NAV_BAR_H;
  if (y < stripTop) {
    return false;
  }

  return FeatureUI::hit(s_touchNavBtns, 5, x, y) == idx;
}

bool isPhysicalButtonPressed(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    return !pcf.digitalRead(buttonPin);
  }
#endif
  return false;
}

bool isTouchNavButtonPressed(int buttonPin) {
  const int idx = touchNavIndexForPin(buttonPin);
  if (idx < 0) {
    return false;
  }
  return isTouchNavSlotDown(idx);
}

bool isButtonPressed(int buttonPin) {
  if (isPhysicalButtonPressed(buttonPin)) {
    return true;
  }
  return isTouchNavButtonPressed(buttonPin);
}

/* Wait for a button to be let go, with a way out.

   The bare form of this, while (isButtonPressed(b)) {}, was in 55 places. It
   spins without yielding, so a button that never reads as released takes the
   device with it: no repaint, no input, and nothing to feed the watchdog.

   A physically stuck button is rare. A touch controller that reports a press
   it never clears is not, on a bus this project has already watched touch lose
   once -- and isButtonPressed falls through to the touch nav when no physical
   button is down, so the touch path is the one that can wedge here.

   Returns false if it gave up. Most callers do not care and should not: the
   point is that the device carries on either way. */
bool waitForButtonRelease(int buttonPin, uint32_t timeoutMs) {
  const uint32_t start = millis();
  while (isButtonPressed(buttonPin)) {
    if ((uint32_t)(millis() - start) >= timeoutMs) {
      return false;
    }
    delay(10);                          // vTaskDelay: yields, unlike a spin
  }
  return true;
}

bool isTouchNavButtonPressedEdge(int buttonPin) {
  if (!feature_active || !touchButtonInputEnabled) {
    return false;
  }

  const int navIdx = touchNavIndexForPin(buttonPin);
  if (navIdx < 0) {
    return false;
  }

  const bool down = isTouchNavSlotDown(navIdx);
  const bool edge = down && !s_touchNavHeld[navIdx];
  s_touchNavHeld[navIdx] = down;
  return edge;
}

bool isButtonPressedEdge(int buttonPin) {
#if HAS_PCF8574_BUTTONS
  if (getPcf8574Address() != 0) {
    const int idx = buttonPin % 8;
    const bool cur = pcf.digitalRead(buttonPin);
    const bool edge = !cur && s_pcfButtonLastState[idx];
    s_pcfButtonLastState[idx] = cur;
    if (edge) {
      return true;
    }
  }
#endif

  return isTouchNavButtonPressedEdge(buttonPin);
}

bool featureExitButtonPressed() {
  return isPhysicalButtonPressed(BTN_SELECT) || isTouchNavButtonPressed(BTN_SELECT);
}

/* Say why a feature is not here, then put the screen back.
 *
 * showNotification() draws a panel and sets notificationVisible; nothing
 * here ever called hideNotification(), so the panel stayed up over a
 * submenu that was still live underneath. Taps went to rows the user could
 * no longer see, which reads as a message that cannot be dismissed. The
 * 250 ms delay was not long enough to read it either.
 */
static void showFeatureUnavailable(const char* featureName, const char* requirement) {
  feature_active = false;
  feature_exit_requested = false;
  showNotification(featureName, requirement);

  /* Long enough to read, and a tap or SELECT takes it away sooner. */
  const uint32_t until = millis() + 3000;
  while ((int32_t)(until - millis()) > 0) {
    int x, y;
    if (isButtonPressed(BTN_SELECT) || readTouchXY(x, y)) {
      break;
    }
    delay(20);
  }
  /* Drain, so the press that dismissed this does not immediately reopen
   * whatever row is under it. */
  waitForButtonRelease(BTN_SELECT);

  hideNotification();
  submenu_initialized = false;
  displaySubmenu();
}

static void runBleDuckyFeature() {
#if FEATURE_BLE_DUCKY
  /* Bluetooth page 2 index 1. It was 5 before the 6 + 5 rebalance, and this
   * one is invisible on a non-S3 board because the whole branch is compiled
   * out, so it survived the sweep that fixed the others. */
  current_submenu_index = 1;
  in_sub_menu = true;
  feature_active = true;
  feature_exit_requested = false;
  Ducky::enter();
  while (current_submenu_index == 1 && !feature_exit_requested) {
      current_submenu_index = 1;
      in_sub_menu = true;
      Ducky::loop();
  }

  Ducky::exit();
  if (feature_exit_requested) {
      in_sub_menu = true;
      is_main_menu = false;
      submenu_initialized = false;
      feature_active = false;
      feature_exit_requested = false;
      displaySubmenu();
      delay(200);
  }
#else
  showFeatureUnavailable("BLE Rubber Ducky", "This feature requires ESP32-S3.");
#endif
}

float currentBatteryVoltage = readBatteryVoltage();
unsigned long last_interaction_time = 0;

int last_menu_index = -1;
bool menu_initialized = false;

/* Menu grid geometry, for a 320x480 panel. A 2x4 grid of tiles sized to fill
 * it, with 10 px margins and gaps.
 *
 * These branched on TFT_WIDTH while this tree built a 240x320 panel too. The
 * branch is gone with that panel, but the numbers it held are the ones the
 * screen renders in render/ are drawn against, so tools/check_render_sync.py
 * still compares them against tools/render_screens.py constant by constant. */
const int TILE_W = 145;
const int TILE_H = 92;
const int COLUMN_WIDTH = 155;
const int X_OFFSET_LEFT = 10;
const int X_OFFSET_RIGHT = X_OFFSET_LEFT + COLUMN_WIDTH;
const int Y_START = 44;
const int Y_SPACING = 106;
// icon(32) + 6 gap + label(16) = 54, centred in a 92 px tile
const int TILE_ICON_DY = 19;
const int TILE_TEXT_DY = 57;

/* ── The submenu grid ────────────────────────────────────────────────────
 *
 * Three columns of five, replacing the list the submenus used.
 *
 * The list was not a style problem. Its rows were 30 px, which is 4.6 mm on
 * a 165 ppi panel against the 7 mm a fingertip needs, and the rows are the
 * tap targets: the d-pad path needs a PCF8574 expander that is not fitted,
 * so touch is the only input there is. It also paged at six entries while
 * leaving 45% of the panel black, which cost a tap for nothing.
 *
 * A tile is the only shape here that was already finger sized. These are
 * 96x70, which is 14.8 x 10.8 mm, and fifteen of them hold the largest
 * submenu with room to spare, so Next Page is gone rather than restyled.
 *
 * The labels are the ones the menu tables already carry, wrapped to two
 * lines in the 6 px font rather than shortened, because they are the same
 * strings the website lists and check_site_menu.py holds the two together.
 */
static constexpr int GRID_COLS    = 3;
static constexpr int GRID_ROWS    = 4;
static constexpr int GRID_SLOTS   = GRID_COLS * GRID_ROWS;
static constexpr int GRID_GAP_X   = 8;
static constexpr int GRID_GAP_Y   = 6;
static constexpr int GRID_TILE_W  =
    (PUEO_SCREEN_W - GRID_GAP_X * (GRID_COLS + 1)) / GRID_COLS;
static constexpr int GRID_TILE_H  = 92;
static constexpr int GRID_Y0      = 54;   // clears PUEO_STATUS_TALL (34) and the title row
static constexpr int GRID_ICON    = 32;
static constexpr int GRID_ICON_DY = 6;
static constexpr int GRID_TEXT_DY = 44;
static constexpr int GRID_LINE_H  = 16;
/* Three lines fit a 92 px tile under a 32 px icon, and three is what the
 * longest label in the firmware needs: Probe / Request / Flood. */
static constexpr int GRID_LINES   = 3;
/* Room for the text, in pixels. Font 2 is proportional, so a character count
 * is not a width and only textWidth() can say whether a label fits.
 *
 * 16 rather than 4, which is 8 px of air each side. At 4 the widest labels
 * technically fitted and touched both borders, and a word with its ends
 * against the edges of a box reads as having run out of room whether or not
 * it has. Eight is enough to look deliberate. */
static constexpr int GRID_TEXT_W  = GRID_TILE_W - 16;

static void gridTileXY(int i, int &x, int &y) {
    const int col = i % GRID_COLS;
    const int row = i / GRID_COLS;
    x = GRID_GAP_X + col * (GRID_TILE_W + GRID_GAP_X);
    y = GRID_Y0 + row * (GRID_TILE_H + GRID_GAP_Y);
}

/* Which tile a tap landed on, or -1. One implementation, because five copies
 * of a hit test is how five of them came to say x <= 220 on a 320 px panel. */
static int gridHit(int tx, int ty, int count) {
    for (int i = 0; i < count && i < GRID_SLOTS; i++) {
        int x, y;
        gridTileXY(i, x, y);
        if (tx >= x && tx < x + GRID_TILE_W &&
            ty >= y && ty < y + GRID_TILE_H) {
            return i;
        }
    }
    return -1;
}

/* Split a label across two lines at a space, preferring the break that
 * leaves the lines closest in length. "Hidden SSID Revealer" is twenty
 * characters and the tile holds fifteen, so this is what keeps the table's
 * own wording instead of inventing a short one for the screen. */
/* Greedy word wrap into at most GRID_LINES lines, measured in the font the
 * caller has selected. Returns how many lines were used.
 *
 * Greedy rather than the balanced two-way split this replaced: balanced was
 * written for exactly two lines and does not generalise to three, and for
 * every label that fitted under the old rule it produces the same answer.
 *
 * A word too wide for a line on its own goes on that line anyway and is
 * drawn over. That cannot happen with any label in the firmware, and
 * check_grid_capacity.py says so rather than leaving it to chance. */
static int gridWrap(const char *label, char out[][40], int cap) {
    int used = 0;
    const char *p = label;
    while (*p && used < cap) {
        /* longest run of words that still fits */
        int take = 0;
        int lastFit = 0;
        for (;;) {
            while (p[take] == ' ') take++;
            while (p[take] && p[take] != ' ') take++;
            char probe[40];
            snprintf(probe, sizeof(probe), "%.*s", take, p);
            if (tft.textWidth(probe) > GRID_TEXT_W && lastFit > 0) {
                break;
            }
            lastFit = take;
            if (!p[take]) break;
        }
        snprintf(out[used], 40, "%.*s", lastFit, p);
        used++;
        p += lastFit;
        while (*p == ' ') p++;
    }
    return used;
}

static void drawGridTile(int i, const char *label,
                         const unsigned char *icon, bool selected) {
    int x, y;
    gridTileXY(i, x, y);
    const uint16_t fill = selected ? UI_ICON : UI_FG;
    const uint16_t edge = selected ? UI_ICON : UI_LINE;
    const uint16_t ink  = selected ? UI_BG   : UI_TEXT;
    const uint16_t icol = selected ? UI_BG   : UI_ICON;

    tft.fillRoundRect(x, y, GRID_TILE_W, GRID_TILE_H, 5, fill);
    tft.drawRoundRect(x, y, GRID_TILE_W, GRID_TILE_H, 5, edge);
    if (icon) {
        drawBitmapScaled(x + (GRID_TILE_W - GRID_ICON) / 2, y + GRID_ICON_DY,
                         icon, 16, 16, icol, GRID_ICON / 16);
    }

    /* Font first: gridWrap() measures with whatever is selected. */
    tft.setTextFont(2);
    tft.setTextSize(1);
    char lines[GRID_LINES][40];
    const int n = gridWrap(label, lines, GRID_LINES);
    tft.setTextColor(ink, fill);
    /* Centred in the block, so a one-line label does not sit high in a tile
     * sized for three. */
    const int ty = y + GRID_TEXT_DY + ((GRID_LINES - n) * GRID_LINE_H) / 2;
    for (int i = 0; i < n; i++) {
        tft.setCursor(x + (GRID_TILE_W - tft.textWidth(lines[i])) / 2,
                      ty + i * GRID_LINE_H);
        tft.print(lines[i]);
    }
}

/* The whole grid. `count` excludes the trailing "Back to Main Menu" entry,
 * which is the footer button rather than a tile. */
static void drawMenuGrid(const char *const *items,
                         const unsigned char *const *icons,
                         int count, int selected, const char *title) {
    tft.fillScreen(UI_BG);
    drawStatusBar(currentBatteryVoltage, true);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(UI_ICON, UI_BG);
    tft.setCursor(8, 38);
    tft.print(title);
    char n[20];
    snprintf(n, sizeof(n), "%d features", count);
    tft.setTextColor(uiDimTextColor(), UI_BG);
    tft.setCursor(PUEO_SCREEN_W - 8 - (int)strlen(n) * 6, 38);
    tft.print(n);
    tft.drawFastHLine(0, 50, PUEO_SCREEN_W, UI_LINE);
    tft.setTextFont(2);

    for (int i = 0; i < count && i < GRID_SLOTS; i++) {
        drawGridTile(i, items[i], icons ? icons[i] : nullptr, i == selected);
    }
}

/* Back, as the footer rather than the last row of the list.
 *
 * The bar is drawn 34 px tall to match every feature screen's footer, and
 * its hit zone runs from the bottom of the last tile row instead, which is
 * 60 px. A target can be bigger than the thing drawn in it, and the strip
 * below the grid is not doing anything else. 34 px is 5.2 mm and under what
 * a finger wants; 60 px is 9.2 mm and over it. */
static constexpr int GRID_FOOT_H = 34;
static constexpr int GRID_FOOT_Y = PUEO_SCREEN_H - GRID_FOOT_H;
static constexpr int GRID_FOOT_HIT_Y =
    GRID_Y0 + GRID_ROWS * (GRID_TILE_H + GRID_GAP_Y);

/* ── hardware a menu depends on ───────────────────────────────────────────
 *
 * Until now the only way to find out the CC1101 is not fitted was to open a
 * SubGHz feature and be told, which says nothing about the other five entries
 * in that menu and comes one tap too late.
 *
 * Probed once and remembered. subghzCc1101Present() claims the SPI bus and
 * the PN532's begin() attaches it, so probing on a repaint would put bus
 * traffic behind a redraw. Opening the menu is a user action and a fair
 * moment to go and look.
 *
 * GPS is not here. There is no cheap probe: the module answers by emitting
 * NMEA when it is ready, and a brief look that found none would report "no
 * GPS" for one that was still waking up. Saying nothing beats saying
 * something false.
 */
/* Neither of these has a header in this tree. */
bool subghzCc1101Present();
namespace Nrf24Raw { bool begin(); }

enum HwProbe : uint8_t { HW_UNKNOWN = 0, HW_THERE, HW_ABSENT };
static HwProbe s_hwNrf  = HW_UNKNOWN;
static HwProbe s_hwCc   = HW_UNKNOWN;
static HwProbe s_hwNfc  = HW_UNKNOWN;

static const char* submenuHardwareNote(int menuIndex) {
    switch (menuIndex) {
        case 1:   /* NRF24 */
            if (s_hwNrf == HW_UNKNOWN) {
                s_hwNrf = Nrf24Raw::begin() ? HW_THERE : HW_ABSENT;
            }
            return (s_hwNrf == HW_ABSENT) ? "no nRF24" : nullptr;
        case 5:   /* SubGHz */
            if (s_hwCc == HW_UNKNOWN) {
                s_hwCc = subghzCc1101Present() ? HW_THERE : HW_ABSENT;
            }
            return (s_hwCc == HW_ABSENT) ? "no CC1101" : nullptr;
        case 6:   /* RFID/NFC */
            if (s_hwNfc == HW_UNKNOWN) {
                s_hwNfc = RfidNfc::begin() ? HW_THERE : HW_ABSENT;
            }
            return (s_hwNfc == HW_ABSENT) ? "no PN532" : nullptr;
        default:
            return nullptr;
    }
}

/* Defined in bluetooth.cpp. A menu on screen means no feature is running,
 * and nothing that is not running may transmit. */
void bleQuietDown();

static void drawSubmenuFooter() {
    tft.fillRect(0, GRID_FOOT_Y, PUEO_SCREEN_W, GRID_FOOT_H, UI_BG);
    tft.drawFastHLine(0, GRID_FOOT_Y, PUEO_SCREEN_W, UI_LINE);
    tft.setTextFont(2);
    tft.setTextSize(1);
    tft.setTextColor(UI_TEXT, UI_BG);
    const int iy = GRID_FOOT_Y + (GRID_FOOT_H - 16) / 2;
    tft.drawBitmap(10, iy, bitmap_icon_go_back, 16, 16, UI_TEXT);
    tft.setCursor(30, iy);
    tft.print("Main Menu");

    /* Opposite Main Menu, in the warn colour, and only when something is
     * missing. A footer that always carries a hardware line is a footer
     * nobody reads. */
    const char* note = submenuHardwareNote(current_menu_index);
    if (note) {
        tft.setTextFont(1);
        tft.setTextSize(1);
        tft.setTextColor(UI_ICON, UI_BG);
        const int w = (int)strlen(note) * 6;
        tft.setCursor(PUEO_SCREEN_W - 10 - w, GRID_FOOT_Y + (GRID_FOOT_H - 8) / 2);
        tft.print(note);
        tft.setTextFont(2);
    }
}

static bool gridFooterHit(int ty) {
    return ty >= GRID_FOOT_HIT_Y;
}

void displayOtherMenuGrid();
void displayPagedSubmenu();

// Last submenu item ("Back to Main Menu") is pinned to the bottom of the screen.
static int submenuItemY(int index) {
    if (active_submenu_size > 0 && index == active_submenu_size - 1) {
        return tft.height() - 30;
    }
    return 30 + index * 30;
}

void displaySubmenu() {
    bleQuietDown();  /* a menu is up, so nothing may be transmitting */
    setTouchButtonInputEnabled(false);

    if (current_menu_index == 2) {
        displayOtherMenuGrid();
        return;
    }

    if (current_menu_index == 0 || current_menu_index == 4) {
        displayPagedSubmenu();
        return;
    }

    setStatusBarHeight(PUEO_STATUS_TALL);  // a tile grid, like the others
    menu_initialized = false;
    last_menu_index = -1;

    /* The last entry is "Back to Main Menu" and is the footer's left button,
     * not a tile. Every feature screen already puts back there, so a submenu
     * that put it at the end of a list was the odd one out. */
    const int tiles = (active_submenu_size > 0) ? active_submenu_size - 1 : 0;
    int sel = current_submenu_index;
    if (sel >= tiles) {
        sel = -1;
    }

    drawMenuGrid(active_submenu_items, active_submenu_icons, tiles, sel,
                 menu_items[current_menu_index]);
    drawSubmenuFooter();

    submenu_initialized = true;
    last_submenu_index = current_submenu_index;
}

void displayPagedSubmenu() {
    bleQuietDown();  /* a menu is up, so nothing may be transmitting */
    setStatusBarHeight(PUEO_STATUS_TALL);  // a tile grid now, with room above it
    menu_initialized = false;
    last_menu_index = -1;

    const int total = pagedTotalFeatures();

    /* The grid holds both pages, so the only thing the page still decides is
     * which tile is highlighted. */
    int sel = -1;
    const int cur = current_submenu_index;
    if (cur >= 0 && cur < pagedFeatureCount()) {
        sel = (*pagedSubmenuPage() == 0) ? cur : pagedFirstPageCount() + cur;
    }

    tft.fillScreen(UI_BG);
    drawStatusBar(currentBatteryVoltage, true);
    tft.setTextFont(1);
    tft.setTextSize(1);
    tft.setTextColor(UI_ICON, UI_BG);
    tft.setCursor(8, 38);
    tft.print(menu_items[current_menu_index]);
    char n[20];
    snprintf(n, sizeof(n), "%d features", total);
    tft.setTextColor(uiDimTextColor(), UI_BG);
    tft.setCursor(PUEO_SCREEN_W - 8 - (int)strlen(n) * 6, 38);
    tft.print(n);
    tft.drawFastHLine(0, 50, PUEO_SCREEN_W, UI_LINE);
    tft.setTextFont(2);

    for (int i = 0; i < total && i < GRID_SLOTS; i++) {
        drawGridTile(i, pagedItemAt(i), pagedIconAt(i), i == sel);
    }
    drawSubmenuFooter();

    submenu_initialized = true;
    last_submenu_index = current_submenu_index;
    s_pagedFooterFocus = -1;
}

void displayOtherMenuGrid() {
    bleQuietDown();  /* a menu is up, so nothing may be transmitting */
    applyThemeToPalette(settings().theme);

    submenu_initialized = false;
    last_submenu_index = -1;
    menu_initialized = false;
    last_menu_index = -1;

    tft.setTextFont(2);

    if (!other_menu_grid_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
            tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
            drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                             y_position + TILE_ICON_DY, other_submenu_icons[i],
                             16, 16, UI_ICON, PUEO_TILE_ICON / 16);

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(other_submenu_items[i]);
            int textX = x_position + (TILE_W - textWidth) / 2;
            int textY = y_position + TILE_TEXT_DY;
            tft.setCursor(textX, textY);
            tft.print(other_submenu_items[i]);
        }

        other_menu_grid_initialized = true;
        last_other_menu_index = -1;
    }

    if (last_other_menu_index != current_submenu_index) {
        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_other_menu_index) {
                tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
                tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
                tft.setTextColor(UI_TEXT, UI_FG);
                drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                                 y_position + TILE_ICON_DY,
                                 other_submenu_icons[last_other_menu_index],
                                 16, 16, UI_ICON, PUEO_TILE_ICON / 16);
                int textWidth = tft.textWidth(other_submenu_items[last_other_menu_index]);
                int textX = x_position + (TILE_W - textWidth) / 2;
                int textY = y_position + TILE_TEXT_DY;
                tft.setCursor(textX, textY);
                tft.print(other_submenu_items[last_other_menu_index]);
            }
        }

        int column = current_submenu_index % OTHER_GRID_COLS;
        int row = current_submenu_index / OTHER_GRID_COLS;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        /* Filled, to match the main menu -- same tile, same size, and it
         * had the same problem. See the note in displayMenu(). */
        tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);
        tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);

        tft.setTextColor(UI_BG, UI_ICON);
        drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                         y_position + TILE_ICON_DY,
                         other_submenu_icons[current_submenu_index],
                         16, 16, UI_BG, PUEO_TILE_ICON / 16);
        int textWidth = tft.textWidth(other_submenu_items[current_submenu_index]);
        int textX = x_position + (TILE_W - textWidth) / 2;
        int textY = y_position + TILE_TEXT_DY;
        tft.setCursor(textX, textY);
        tft.print(other_submenu_items[current_submenu_index]);

        last_other_menu_index = current_submenu_index;
    }

    setStatusBarHeight(PUEO_STATUS_TALL);  // a tile grid, same 24 px of slack
    drawStatusBar(currentBatteryVoltage, true);
}


void displayMenu() {
    bleQuietDown();  /* a menu is up, so nothing may be transmitting */

  setTouchButtonInputEnabled(false);
  applyThemeToPalette(settings().theme);

const uint16_t icon_colors[NUM_MENU_ITEMS] = {
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON,
  UI_ICON
};

    submenu_initialized = false;
    last_submenu_index = -1;
    other_menu_grid_initialized = false;
    last_other_menu_index = -1;
    tft.setTextFont(2);

    if (!menu_initialized) {
        tft.fillScreen(UI_BG);

        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
            tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
                drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                                 y_position + TILE_ICON_DY, bitmap_icons[i],
                                 16, 16, icon_colors[i], PUEO_TILE_ICON / 16);

            tft.setTextColor(UI_TEXT, UI_FG);
            int textWidth = tft.textWidth(menu_items[i]);
            int textX = x_position + (TILE_W - textWidth) / 2;
            int textY = y_position + TILE_TEXT_DY;
            tft.setCursor(textX, textY);
            tft.print(menu_items[i]);
        }
        menu_initialized = true;
        last_menu_index = -1;
    }

    if (last_menu_index != current_menu_index) {
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
            int column = i / 4;
            int row = i % 4;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;

            if (i == last_menu_index) {
                tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_FG);
                tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_LINE);
                tft.setTextColor(UI_TEXT, UI_FG);
                    drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                                     y_position + TILE_ICON_DY, bitmap_icons[last_menu_index],
                                     16, 16, icon_colors[last_menu_index], PUEO_TILE_ICON / 16);
                int textWidth = tft.textWidth(menu_items[last_menu_index]);
                int textX = x_position + (TILE_W - textWidth) / 2;
                int textY = y_position + TILE_TEXT_DY;
                tft.setCursor(textX, textY);
                tft.print(menu_items[last_menu_index]);
            }
        }

        int column = current_menu_index / 4;
        int row = current_menu_index % 4;
        int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
        int y_position = Y_START + row * Y_SPACING;

        /* A selected tile is FILLED with the accent and its contents drop to
         * the background colour, rather than only swapping a hairline border.
         *
         * It had nowhere else to go. Every entry in the icon tables is
         * already UI_ICON and SELECTED_ICON_COLOR was defined as UI_ICON
         * again, so the icon was identical selected or not; the whole cue was
         * a 1 px outline and the label going white to accent, on a 100x60
         * tile. Filling flips about half the tile's area instead, which is
         * what carries at arm's length and in sunlight.
         *
         * The label goes dark rather than white because dark on accent is
         * about 6.4:1 and white on accent is 2.6:1. */
        tft.fillRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);
        tft.drawRoundRect(x_position, y_position, TILE_W, TILE_H, 5, UI_ICON);

        tft.setTextColor(UI_BG, UI_ICON);
            drawBitmapScaled(x_position + (TILE_W - PUEO_TILE_ICON) / 2,
                             y_position + TILE_ICON_DY, bitmap_icons[current_menu_index],
                             16, 16, UI_BG, PUEO_TILE_ICON / 16);
        int textWidth = tft.textWidth(menu_items[current_menu_index]);
        int textX = x_position + (TILE_W - textWidth) / 2;
        int textY = y_position + TILE_TEXT_DY;
        tft.setCursor(textX, textY);
        tft.print(menu_items[current_menu_index]);

        last_menu_index = current_menu_index;
    }
    setStatusBarHeight(PUEO_STATUS_TALL);  // the one screen with room above its tiles
    drawStatusBar(currentBatteryVoltage, true);
}

void handleWiFiSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(70);

        // Footer: Next / Prev
        if (current_submenu_index == pagedPageBtnIndex()) {
            wifi_submenu_page = (wifi_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        // Footer: Back to Main Menu
        if (current_submenu_index == pagedBackBtnIndex()) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            wifi_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            PacketMonitor::ptmSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                PacketMonitor::ptmLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            /* Leaving the screen used to leave the radio promiscuous and the
             * capture file open and still being written. ptmLoop has branches
             * for this that this loop never lets it reach. */
            PacketMonitor::ptmTeardown();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BeaconSpammer::beaconSpamSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BeaconSpammer::beaconSpamLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            Deauther::deautherSetup();
            while (current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                Deauther::deautherLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ProbeRequestFlood::probeRequestFloodSetup();
            while (current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                ProbeRequestFlood::probeRequestFloodLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            DeauthDetect::deauthdetectSetup();
            while (current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                DeauthDetect::deauthdetectLoop();
                if (featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (wifi_submenu_page == 0 && current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WifiScan::wifiscanSetup();
            while (current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                WifiScan::wifiscanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            CaptivePortal::cportalSetup();
            while (current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                CaptivePortal::cportalLoop();
                if (isButtonPressed(BTN_SELECT)) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            HiddenSsidReveal::hiddenSsidSetup();
            while (current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                HiddenSsidReveal::hiddenSsidLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            WpsScanner::wpsScannerSetup();
            while (wifi_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                WpsScanner::wpsScannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            ArpScanner::arpScannerSetup();
            while (wifi_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                ArpScanner::arpScannerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            KarmaAttack::karmaSetup();
            while (wifi_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                KarmaAttack::karmaLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (wifi_submenu_page == 1 && current_submenu_index == 5) {
            feature_active = true;
            feature_exit_requested = false;
            ApTracker::setup();
            while (wifi_submenu_page == 1 && current_submenu_index == 5 &&
                   !feature_exit_requested) {
                ApTracker::loop();
                if (featureExitButtonPressed()) {
                    break;
                }
            }
            ApTracker::exit();
            feature_active = false;
            feature_exit_requested = false;
            submenu_initialized = false;
            displaySubmenu();
            delay(200);
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        /* One footer button now, because there are no pages to turn. Back
         * is the strip below the last tile row, which is 60 px rather than
         * the 34 the bar is drawn in: a target may be larger than the thing
         * inside it, and nothing else is down there. */
        const int footerHit = gridFooterHit(y) ? 0 : -1;
        if (footerHit == 0) {
            // Left: Main Menu
            current_submenu_index = pagedBackBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            wifi_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }
        if (footerHit == 1) {
            // Right: Next / Prev page
            current_submenu_index = pagedPageBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            wifi_submenu_page = (wifi_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyWifiSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        const int featureCount = wifiFeatureCount();
        /* A tap on the grid picks a tile, and the tile says which page and
         * which index on it. Setting both here means the dispatch below,
         * which is written in those terms in about ninety places, needs no
         * change at all. */
        {
            const int tapped = gridHit(x, y, pagedTotalFeatures());
            if (tapped >= 0) {
                int tpage, tidx;
                pagedSplit(tapped, tpage, tidx);
                if (*pagedSubmenuPage() != tpage) {
                    *pagedSubmenuPage() = tpage;
                    applyPagedSubmenuPage();
                }
                s_gridTapIndex = tidx;
            } else {
                s_gridTapIndex = -1;
            }
        }
        for (int i = 0; i < featureCount; i++) {
            /* The grid decided which tile; this loop only has to agree. */
            if (i == s_gridTapIndex) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (wifi_submenu_page == 0 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    PacketMonitor::ptmSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        PacketMonitor::ptmLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    /* Leaving the screen used to leave the radio promiscuous and the
                     * capture file open and still being written. ptmLoop has branches
                     * for this that this loop never lets it reach. */
                    PacketMonitor::ptmTeardown();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BeaconSpammer::beaconSpamSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BeaconSpammer::beaconSpamLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    Deauther::deautherSetup();
                    while (current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        Deauther::deautherLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ProbeRequestFlood::probeRequestFloodSetup();
                    while (current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        ProbeRequestFlood::probeRequestFloodLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    DeauthDetect::deauthdetectSetup();
                    while (current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        DeauthDetect::deauthdetectLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 0 && current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WifiScan::wifiscanSetup();
                    while (current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        WifiScan::wifiscanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    CaptivePortal::cportalSetup();
                    while (current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        CaptivePortal::cportalLoop();
                        if (isButtonPressed(BTN_SELECT)) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    HiddenSsidReveal::hiddenSsidSetup();
                    while (current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        HiddenSsidReveal::hiddenSsidLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    WpsScanner::wpsScannerSetup();
                    while (wifi_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        WpsScanner::wpsScannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    ArpScanner::arpScannerSetup();
                    while (wifi_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        ArpScanner::arpScannerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    KarmaAttack::karmaSetup();
                    while (wifi_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        KarmaAttack::karmaLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (wifi_submenu_page == 1 && current_submenu_index == 5) {
                    feature_active = true;
                    feature_exit_requested = false;
                    ApTracker::setup();
                    while (wifi_submenu_page == 1 && current_submenu_index == 5 &&
                           !feature_exit_requested) {
                        ApTracker::loop();
                        if (featureExitButtonPressed()) {
                            break;
                        }
                    }
                    ApTracker::exit();
                    feature_active = false;
                    feature_exit_requested = false;
                    submenu_initialized = false;
                    displaySubmenu();
                    delay(200);
                }
                break;
            }
        }
    }
}

void handleBluetoothSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(70);

        if (current_submenu_index == pagedPageBtnIndex()) {
            bluetooth_submenu_page = (bluetooth_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        if (current_submenu_index == pagedBackBtnIndex()) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            bluetooth_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleJammer::blejamSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleJammer::blejamLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            BleJammer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 1) {
            current_submenu_index = 1;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSpoofer::spooferSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 1 && !feature_exit_requested) {
                current_submenu_index = 1;
                in_sub_menu = true;
                BleSpoofer::spooferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            BleSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            SourApple::sourappleSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                SourApple::sourappleLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            SourApple::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            AirTagSpoofer::airTagSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                AirTagSpoofer::airTagLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            AirTagSpoofer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            AirTagSniffer::airTagSnifferSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                AirTagSniffer::airTagSnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            AirTagSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 0 && current_submenu_index == 5) {
            current_submenu_index = 5;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSniffer::blesnifferSetup();
            while (bluetooth_submenu_page == 0 && current_submenu_index == 5 && !feature_exit_requested) {
                current_submenu_index = 5;
                in_sub_menu = true;
                BleSniffer::blesnifferLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            BleSniffer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 1 && current_submenu_index == 0) {
            current_submenu_index = 0;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleScan::bleScanSetup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                current_submenu_index = 0;
                in_sub_menu = true;
                BleScan::bleScanLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            BleScan::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
        if (bluetooth_submenu_page == 1 && current_submenu_index == 1) {
            runBleDuckyFeature();
        }

        if (bluetooth_submenu_page == 1 && current_submenu_index == 2) {
            current_submenu_index = 2;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            BleSkimmer::bleSkimmerSetup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                current_submenu_index = 2;
                in_sub_menu = true;
                BleSkimmer::bleSkimmerLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            BleSkimmer::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }





        if (bluetooth_submenu_page == 1 && current_submenu_index == 3) {
            current_submenu_index = 3;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            TrackerHunt::setup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                current_submenu_index = 3;
                in_sub_menu = true;
                TrackerHunt::loop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            TrackerHunt::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }

        if (bluetooth_submenu_page == 1 && current_submenu_index == 4) {
            current_submenu_index = 4;
            in_sub_menu = true;
            feature_active = true;
            feature_exit_requested = false;
            FastPairScan::fastPairSetup();
            while (bluetooth_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                current_submenu_index = 4;
                in_sub_menu = true;
                FastPairScan::fastPairLoop();
                if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                    in_sub_menu = true;
                    is_main_menu = false;
                    submenu_initialized = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displaySubmenu();
                    delay(200);
                    waitForButtonRelease(BTN_SELECT);
                    break;
                }
            }
            FastPairScan::exit();
            if (feature_exit_requested) {
                in_sub_menu = true;
                is_main_menu = false;
                submenu_initialized = false;
                feature_active = false;
                feature_exit_requested = false;
                displaySubmenu();
                delay(200);
            }
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        /* One footer button now, because there are no pages to turn. Back
         * is the strip below the last tile row, which is 60 px rather than
         * the 34 the bar is drawn in: a target may be larger than the thing
         * inside it, and nothing else is down there. */
        const int footerHit = gridFooterHit(y) ? 0 : -1;
        if (footerHit == 0) {
            current_submenu_index = pagedBackBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            bluetooth_submenu_page = 0;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }
        if (footerHit == 1) {
            current_submenu_index = pagedPageBtnIndex();
            last_interaction_time = millis();
            displaySubmenu();
            delay(120);
            bluetooth_submenu_page = (bluetooth_submenu_page == 0) ? 1 : 0;
            current_submenu_index = 0;
            applyBluetoothSubmenuPage();
            displaySubmenu();
            delay(200);
            return;
        }

        const int featureCount = bluetoothFeatureCount();
        /* A tap on the grid picks a tile, and the tile says which page and
         * which index on it. Setting both here means the dispatch below,
         * which is written in those terms in about ninety places, needs no
         * change at all. */
        {
            const int tapped = gridHit(x, y, pagedTotalFeatures());
            if (tapped >= 0) {
                int tpage, tidx;
                pagedSplit(tapped, tpage, tidx);
                if (*pagedSubmenuPage() != tpage) {
                    *pagedSubmenuPage() = tpage;
                    applyPagedSubmenuPage();
                }
                s_gridTapIndex = tidx;
            } else {
                s_gridTapIndex = -1;
            }
        }
        for (int i = 0; i < featureCount; i++) {
            /* The grid decided which tile; this loop only has to agree. */
            if (i == s_gridTapIndex) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (bluetooth_submenu_page == 0 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleJammer::blejamSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleJammer::blejamLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    BleJammer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 1) {
                    current_submenu_index = 1;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSpoofer::spooferSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 1 && !feature_exit_requested) {
                        current_submenu_index = 1;
                        in_sub_menu = true;
                        BleSpoofer::spooferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    BleSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    SourApple::sourappleSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        SourApple::sourappleLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    SourApple::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    AirTagSpoofer::airTagSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        AirTagSpoofer::airTagLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    AirTagSpoofer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    AirTagSniffer::airTagSnifferSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        AirTagSniffer::airTagSnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    AirTagSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 0 && current_submenu_index == 5) {
                    current_submenu_index = 5;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSniffer::blesnifferSetup();
                    while (bluetooth_submenu_page == 0 && current_submenu_index == 5 && !feature_exit_requested) {
                        current_submenu_index = 5;
                        in_sub_menu = true;
                        BleSniffer::blesnifferLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    BleSniffer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 0) {
                    current_submenu_index = 0;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleScan::bleScanSetup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 0 && !feature_exit_requested) {
                        current_submenu_index = 0;
                        in_sub_menu = true;
                        BleScan::bleScanLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    BleScan::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 1) {
                    runBleDuckyFeature();
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 2) {
                    current_submenu_index = 2;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    BleSkimmer::bleSkimmerSetup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 2 && !feature_exit_requested) {
                        current_submenu_index = 2;
                        in_sub_menu = true;
                        BleSkimmer::bleSkimmerLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    BleSkimmer::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 3) {
                    current_submenu_index = 3;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    TrackerHunt::setup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 3 && !feature_exit_requested) {
                        current_submenu_index = 3;
                        in_sub_menu = true;
                        TrackerHunt::loop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    TrackerHunt::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                } else if (bluetooth_submenu_page == 1 && current_submenu_index == 4) {
                    current_submenu_index = 4;
                    in_sub_menu = true;
                    feature_active = true;
                    feature_exit_requested = false;
                    FastPairScan::fastPairSetup();
                    while (bluetooth_submenu_page == 1 && current_submenu_index == 4 && !feature_exit_requested) {
                        current_submenu_index = 4;
                        in_sub_menu = true;
                        FastPairScan::fastPairLoop();
                        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
                            in_sub_menu = true;
                            is_main_menu = false;
                            submenu_initialized = false;
                            feature_active = false;
                            feature_exit_requested = false;
                            displaySubmenu();
                            delay(200);
                            waitForButtonRelease(BTN_SELECT);
                            break;
                        }
                    }
                    FastPairScan::exit();
                    if (feature_exit_requested) {
                        in_sub_menu = true;
                        is_main_menu = false;
                        submenu_initialized = false;
                        feature_active = false;
                        feature_exit_requested = false;
                        displaySubmenu();
                        delay(200);
                    }
                }
                break;
            }
        }
    }
}

/* One feature launch, for the menus whose entries all have the same shape.
 *
 * Eleven entries across the NRF24 and SubGHz menus ran the same ~28 lines,
 * and each ran them twice -- once in the button chain and once in the touch
 * chain -- so the shape existed 22 times. check_menu_dispatch.py could tell
 * you both chains had a branch for an index; nothing could tell you the two
 * branches agreed, and the bug this repo already shipped was two chains
 * disagreeing. Now there is one copy and two callers.
 *
 * The two menus really did differ, in two ways, and both are parameters
 * rather than smoothed away:
 *
 *   levelExit   NRF's entries also leave on isButtonPressed(BTN_SELECT),
 *               which is level-triggered. That is why they drain it with a
 *               spin afterwards: without the drain a finger still down from
 *               selecting the item would re-trigger. SubGHz's entries use
 *               the edge-detected check alone.
 *
 *   teardown    SubGHz features mostly have no exit function to call -- not
 *               an oversight in the dispatch, there is nothing to call. The
 *               jammer is the exception since it started stopping itself.
 *               nullptr says so in a place somebody will read.
 */
static void runSubmenuFeature(int idx, void (*setup)(), void (*loop)(),
                              void (*teardown)(), bool levelExit) {
    current_submenu_index = idx;
    in_sub_menu = true;
    feature_active = true;
    feature_exit_requested = false;
    setup();
    while (current_submenu_index == idx && !feature_exit_requested) {
        current_submenu_index = idx;
        in_sub_menu = true;
        loop();
        const bool leaving = levelExit
            ? (isButtonPressed(BTN_SELECT) || featureExitButtonPressed())
            : featureExitButtonPressed();
        if (leaving) {
            in_sub_menu = true;
            is_main_menu = false;
            submenu_initialized = false;
            feature_active = false;
            feature_exit_requested = false;
            displaySubmenu();
            delay(200);
            waitForButtonRelease(BTN_SELECT);
            break;
        }
    }
    if (teardown) {
        teardown();
    }
    if (feature_exit_requested) {
        in_sub_menu = true;
        is_main_menu = false;
        submenu_initialized = false;
        feature_active = false;
        feature_exit_requested = false;
        displaySubmenu();
        delay(200);
    }
}

static void launchNrfFeature(int idx) {
    switch (idx) {
        case 0: runSubmenuFeature(0, Scanner::scannerSetup, Scanner::scannerLoop, Scanner::exit, true); break;
        case 1: runSubmenuFeature(1, ProtoKill::prokillSetup, ProtoKill::prokillLoop, ProtoKill::exit, true); break;
        case 2: runSubmenuFeature(2, EsbSniffer::esbSnifferSetup, EsbSniffer::esbSnifferLoop, EsbSniffer::exit, true); break;
        case 3: runSubmenuFeature(3, EsbReplay::esbReplaySetup, EsbReplay::esbReplayLoop, EsbReplay::exit, true); break;
        case 4: runSubmenuFeature(4, MouseJack::mouseJackSetup, MouseJack::mouseJackLoop, MouseJack::exit, true); break;
        case 5: runSubmenuFeature(5, MouseJackInject::mouseJackInjectSetup, MouseJackInject::mouseJackInjectLoop, MouseJackInject::exit, true); break;
        default: break;
    }
}

static void launchSubGhzFeature(int idx) {
    switch (idx) {
        case 0: runSubmenuFeature(0, replayat::ReplayAttackSetup, replayat::ReplayAttackLoop, nullptr, false); break;
        case 1: runSubmenuFeature(1, subjammer::subjammerSetup, subjammer::subjammerLoop, subjammer::exit, false); break;
        case 2: runSubmenuFeature(2, SubBrute::subBruteSetup, SubBrute::subBruteLoop, nullptr, false); break;
        case 3: runSubmenuFeature(3, jammingdetector::Setup, jammingdetector::Loop, nullptr, false); break;
        case 4: runSubmenuFeature(4, SavedProfile::saveSetup, SavedProfile::saveLoop, nullptr, false); break;
        case 5: runSubmenuFeature(5, SubImport::setup, SubImport::loop, nullptr, false); break;
        case 6: runSubmenuFeature(6, SubExport::setup, SubExport::loop, nullptr, false); break;
        case 7: runSubmenuFeature(7, FreqScan::setup, FreqScan::loop, nullptr, false); break;
        default: break;
    }
}


void handleNRFSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == nrf_NUM_SUBMENU_ITEMS - 1) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index != 6) {
            launchNrfFeature(current_submenu_index);
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            /* The grid owns the geometry, in one place. Five copies of a
             * hit test is how five of them came to say x <= 220 on a 320 px
             * panel. The last entry is Back and lives in the footer. */
            const bool isBack = (i == active_submenu_size - 1);
            const bool hit = isBack
                ? gridFooterHit(y)
                : (gridHit(x, y, active_submenu_size - 1) == i);
            if (hit) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == nrf_NUM_SUBMENU_ITEMS - 1) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else {
                    launchNrfFeature(current_submenu_index);
                }
                break;
            }
        }
    }
}

void handleSubGHzSubmenuButtons() {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        if (current_submenu_index < 0) {
            current_submenu_index = NUM_SUBMENU_ITEMS - 1;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        if (current_submenu_index >= NUM_SUBMENU_ITEMS) {
            current_submenu_index = 0;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        /* Derived, not written down. This said 5, which was Back until an
             * entry was added above it; Back then moved to 6 and the literal
             * pointed at the new feature, so choosing it went to the main menu
             * and choosing Back fell through to a switch with no case for it
             * and did nothing. */
            if (current_submenu_index == subghz_NUM_SUBMENU_ITEMS - 1) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
        }

        if (current_submenu_index != 5) {
            launchSubGhzFeature(current_submenu_index);
        }
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);
        for (int i = 0; i < active_submenu_size; i++) {
            /* The grid owns the geometry, in one place. Five copies of a
             * hit test is how five of them came to say x <= 220 on a 320 px
             * panel. The last entry is Back and lives in the footer. */
            const bool isBack = (i == active_submenu_size - 1);
            const bool hit = isBack
                ? gridFooterHit(y)
                : (gridHit(x, y, active_submenu_size - 1) == i);
            if (hit) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == subghz_NUM_SUBMENU_ITEMS - 1) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else {
                    launchSubGhzFeature(current_submenu_index);
                }
                break;
            }
        }
    }
}

constexpr int TOOLS_IDX_TERMINAL = 0;
constexpr int TOOLS_IDX_UPDATE   = 1;
constexpr int TOOLS_IDX_TOUCH    = 2;
constexpr int TOOLS_IDX_SD_FILES = 3;
constexpr int TOOLS_IDX_SD_RESET = 4;
constexpr int TOOLS_IDX_XFER     = 5;
constexpr int TOOLS_IDX_SETTINGS = 6;
constexpr int TOOLS_IDX_ABOUT    = 7;
constexpr int TOOLS_IDX_BACK     = 8;

static void runToolsFeatureExitCleanup() {
    in_sub_menu = true;
    is_main_menu = false;
    submenu_initialized = false;
    feature_active = false;
    feature_exit_requested = false;
    setTouchButtonInputEnabled(false);
    setTouchNavLabels(nullptr, nullptr, nullptr, nullptr, nullptr);
    resetTouchNavHeldState();
    displaySubmenu();
    delay(200);
    waitForButtonRelease(BTN_SELECT);
}

/* exitFn is optional because most of these have nothing to put back. The one
 * that does owns a radio: File Transfer raises an access point, and an
 * access point that outlives its screen is a device still beaconing from a
 * menu that says it is not. */
static void runToolsFeature(int idx, void (*setupFn)(), void (*loopFn)(),
                            void (*exitFn)() = nullptr) {
    const bool useTouchNav = (idx != TOOLS_IDX_TOUCH);
    current_submenu_index = idx;
    in_sub_menu = true;
    feature_active = true;
    feature_exit_requested = false;
    if (useTouchNav) {
        setTouchButtonInputEnabled(true);
    }
    setupFn();
    while (current_submenu_index == idx && !feature_exit_requested) {
        current_submenu_index = idx;
        in_sub_menu = true;
        loopFn();
        if (feature_exit_requested) {
            break;
        }
        if (!useTouchNav && isButtonPressed(BTN_SELECT)) {
            break;
        }
    }
    /* Before the cleanup, not after: the cleanup repaints the submenu, and a
     * teardown that runs behind the menu it is leaving has already handed the
     * screen to whoever comes next. Called even when setup() refused, which
     * is why every exit() here has to be safe on a feature that never
     * started. */
    if (exitFn != nullptr) {
        exitFn();
    }
    runToolsFeatureExitCleanup();
}

/* Settings and About both end with displayMenu(), which was right when
 * they were tiles reached from the main menu. As rows in System they have
 * to put the submenu back instead. */
static void reopenSystemSubmenu() {
    in_sub_menu = true;
    is_main_menu = false;
    feature_active = false;
    feature_exit_requested = false;
    updateActiveSubmenu();
    submenu_initialized = false;
    displaySubmenu();
}

static void launchToolsFeature(int idx) {
    switch (idx) {
        case TOOLS_IDX_TERMINAL:
            runToolsFeature(idx, Terminal::terminalSetup, Terminal::terminalLoop);
            break;
        case TOOLS_IDX_UPDATE:
            runToolsFeature(idx, FirmwareUpdate::updateSetup, FirmwareUpdate::updateLoop);
            break;
        case TOOLS_IDX_TOUCH:
            runToolsFeature(idx, TouchCalib::setup, TouchCalib::loop);
            break;
        case TOOLS_IDX_SD_FILES:
            runToolsFeature(idx, SdFileManager::setup, SdFileManager::loop);
            break;
        case TOOLS_IDX_SD_RESET:
            runToolsFeature(idx, SdReset::setup, SdReset::loop);
            break;
        case TOOLS_IDX_XFER:
            runToolsFeature(idx, FileServer::setup, FileServer::loop,
                            FileServer::exit);
            break;
        case TOOLS_IDX_SETTINGS:
            handleSettingsSubmenuButtons();
            reopenSystemSubmenu();
            break;
        case TOOLS_IDX_ABOUT:
            handleAboutPage();
            reopenSystemSubmenu();
            break;
        default:
            break;
    }
}

static void launchGpsFeature(int idx)  { otherGpsPlaceholderAction(idx); }
static void launchRfidFeature(int idx) { otherRfidPlaceholderAction(idx); }

/* GPS, RFID/NFC and System are plain lists and differ only in what a row
 * launches and which row is Back, so they share this rather than carrying
 * three copies of the same scrolling and hit testing. */
void handleListSubmenuButtons(void (*launch)(int), int backIdx) {
    if (isButtonPressed(BTN_UP)) {
        current_submenu_index = (current_submenu_index - 1 + active_submenu_size) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        current_submenu_index = (current_submenu_index + 1) % active_submenu_size;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);

        if (current_submenu_index == backIdx) {
            in_sub_menu = false;
            feature_active = false;
            feature_exit_requested = false;
            displayMenu();
            handleButtons();
            is_main_menu = false;
            return;
        }

        launch(current_submenu_index);
        return;
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) {
            return;
        }

        for (int i = 0; i < active_submenu_size; i++) {
            /* The grid owns the geometry, in one place. Five copies of a
             * hit test is how five of them came to say x <= 220 on a 320 px
             * panel. The last entry is Back and lives in the footer. */
            const bool isBack = (i == active_submenu_size - 1);
            const bool hit = isBack
                ? gridFooterHit(y)
                : (gridHit(x, y, active_submenu_size - 1) == i);
            if (hit) {
                current_submenu_index = i;
                last_interaction_time = millis();
                displaySubmenu();
                delay(200);

                if (current_submenu_index == backIdx) {
                    in_sub_menu = false;
                    feature_active = false;
                    feature_exit_requested = false;
                    displayMenu();
                    handleButtons();
                    is_main_menu = false;
                } else {
                    launch(current_submenu_index);
                }
                break;
            }
        }
    }
}

static void otherDismissPlaceholder() {
    delay(25);
    while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
        delay(5);
    }
    while (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT)) {
        int x = 0, y = 0;
        if (!readTouchXYDismiss(x, y) && !readTouchXY(x, y)) {
            delay(12);
            continue;
        }
        if (isNotificationVisible()) {
            NotificationAction a = notificationHandleTouch(x, y);
            if (a != NotificationAction::None) {
                break;
            }
            hideNotification();
        }
        break;
    }
    if (in_sub_menu) {
        submenu_initialized = false;
        if (current_menu_index == 2) {
            other_menu_grid_initialized = false;
            last_other_menu_index = -1;
        }
        displaySubmenu();
    }
}

static void otherRfidReturnGuard() {
    delay(120);
    for (int i = 0; i < 120; i++) {
        if (!isButtonPressed(BTN_SELECT) && !isButtonPressed(BTN_LEFT) &&
            !isButtonPressed(BTN_RIGHT) && !isButtonPressed(BTN_UP) &&
            !isButtonPressed(BTN_DOWN) && !isTouchDownDismiss()) {
            break;
        }
        delay(5);
    }
    delay(120);
}

static void otherRfidPlaceholderAction(int idx) {
    feature_active = true;
    /* All of RFID/NFC, not one entry of it. A PN532 reads a card by
     * energising a 13.56 MHz field and waiting for the card to answer, so
     * "read" transmits exactly as much as "clone" does. Gated here because
     * every entry in that menu comes through this one function. */
    if (Stealth::refuse("RFID/NFC")) { feature_active = false; return; }
    if (!RfidNfc::begin()) {
        /* Same panel and the same amount of help as the nRF24 and CC1101
         * messages. The DIP switches are in here because a PN532 left in
         * I2C mode is the commonest reason one is fitted, wired and silent,
         * and nothing on the board says which mode it is in. */
        showNotification("RFID/NFC",
                         "needs the PN532, and nothing answered on the SPI "
                         "bus. Check the module is fitted and that MISO, "
                         "MOSI, SCK and SS are wired, and that its DIP "
                         "switches are set for SPI: CH1 off, CH2 on.");
        otherDismissPlaceholder();
        feature_active = false;
        return;
    }
    feature_exit_requested = false;
    setTouchButtonInputEnabled(true);
    for (;;) {
        RfidNfc::clearSessionRetry();
        switch (idx) {
            case 0:
                RfidNfc::sessionCardReader();
                break;
            case 1:
                RfidNfc::sessionClone();
                break;
            case 2:
                RfidNfc::sessionErase();
                break;
            case 3:
                RfidNfc::sessionDump();
                break;
            case 4:
                RfidNfc::sessionDecodeAccess();
                break;
            case 5:
                RfidNfc::sessionJamReader();
                break;
            case 6:
                RfidNfc::sessionTagDisrupt();
                break;
            case 7:
                RfidNfc::sessionDisruptEmulate();
                break;
            default:
                feature_active = false;
                restoreSdAfterSharedSpi();
                return;
        }
        if (feature_exit_requested || !RfidNfc::consumeSessionRetry()) {
            break;
        }
        feature_exit_requested = false;
    }
    restoreSdAfterSharedSpi();
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

static void otherGpsPlaceholderAction(int idx) {
    feature_active = true;
    if (idx == 0) {
        feature_exit_requested = false;
        setTouchButtonInputEnabled(true);
        for (;;) {
            GpsWardriver::clearSessionRetry();
            GpsWardriver::session();
            if (feature_exit_requested || !GpsWardriver::consumeSessionRetry()) {
                break;
            }
            feature_exit_requested = false;
        }
        setTouchButtonInputEnabled(false);
    } else {
        switch (idx) {
            case 1:
                GpsSatelliteScanner::session();
                break;
            default:
                feature_active = false;
                return;
        }
    }
    otherRfidReturnGuard();
    submenu_initialized = false;
    displaySubmenu();
    feature_active = false;
}

/* Detect: Surveillance and the drone detector, in the grid More used.
 *
 * More held four entries behind two sub-layers, so this handler carried a
 * grid mode and a list mode, three copies of a back path, and two copies
 * of every feature launch. RFID/NFC and GPS are tiles of their own now.
 */
static void runDetectFeature(void (*setup)(), void (*loop)(),
                             void (*teardown)()) {
    feature_active = true;
    feature_exit_requested = false;
    setup();
    while (!feature_exit_requested) {
        loop();
        if (isButtonPressed(BTN_SELECT) || featureExitButtonPressed()) {
            break;
        }
    }
    teardown();
    feature_active = false;
    feature_exit_requested = false;
    other_menu_grid_initialized = false;
    last_other_menu_index = -1;
    submenu_initialized = false;
    displaySubmenu();
    delay(200);
}

static void launchDetectFeature(int idx) {
    if (idx == other_NUM_SUBMENU_ITEMS - 1) {
        in_sub_menu = false;
        feature_active = false;
        feature_exit_requested = false;
        displayMenu();
        handleButtons();
        is_main_menu = false;
        return;
    }
    if (idx == 0) {
        runDetectFeature(Spotter::spotterSetup, Spotter::spotterLoop,
                         Spotter::exit);
    } else if (idx == 1) {
        runDetectFeature(DroneScan::setup, DroneScan::loop,
                         DroneScan::exit);
    }
}

void handleOtherSubmenuButtons() {
    const int og_rows =
        (other_NUM_SUBMENU_ITEMS + OTHER_GRID_COLS - 1) / OTHER_GRID_COLS;

    if (isButtonPressed(BTN_UP)) {
        int row = current_submenu_index / OTHER_GRID_COLS;
        if (row > 0) {
            current_submenu_index -= OTHER_GRID_COLS;
        } else {
            current_submenu_index += OTHER_GRID_COLS * (og_rows - 1);
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_DOWN)) {
        int row = current_submenu_index / OTHER_GRID_COLS;
        if (row < og_rows - 1) {
            current_submenu_index += OTHER_GRID_COLS;
        } else {
            current_submenu_index -= OTHER_GRID_COLS * (og_rows - 1);
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_LEFT)) {
        int col = current_submenu_index % OTHER_GRID_COLS;
        if (col > 0) {
            current_submenu_index--;
        } else {
            current_submenu_index++;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_RIGHT)) {
        int col = current_submenu_index % OTHER_GRID_COLS;
        if (col < OTHER_GRID_COLS - 1) {
            current_submenu_index++;
        } else {
            current_submenu_index--;
        }
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
    }

    if (isButtonPressed(BTN_SELECT)) {
        last_interaction_time = millis();
        delay(200);
        launchDetectFeature(current_submenu_index);
        return;
    }

    if (!feature_active) {
        int x, y;
        if (!readTouchXY(x, y)) { return; }
        delay(10);

        int touched_slot = -1;
        for (int i = 0; i < other_NUM_SUBMENU_ITEMS; i++) {
            int column = i % OTHER_GRID_COLS;
            int row = i / OTHER_GRID_COLS;
            int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
            int y_position = Y_START + row * Y_SPACING;
            int button_x1 = x_position;
            int button_y1 = y_position;
            /* TILE_W/TILE_H, not 100x60. Those are the 2.8" tile size,
             * and on the 3.5" the tiles are 145x92 -- so two thirds of
             * every tile in this grid did not respond to a tap. It
             * survived the panel sweep by looking like a hit box rather
             * than like a dimension. */
            int button_x2 = x_position + TILE_W;
            int button_y2 = y_position + TILE_H;
            if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                touched_slot = i;
                break;
            }
        }

        if (touched_slot < 0) {
            return;
        }

        current_submenu_index = touched_slot;
        last_interaction_time = millis();
        displaySubmenu();
        delay(200);
        launchDetectFeature(current_submenu_index);
    }
}

/* About, in two pages.
 *
 * It was one page of text and no artwork at all. The owl is a fixed 200x200
 * bitmap and drawBitmap does not scale, so on a 240x320 panel it does not
 * share a page with four rows of credits -- there is nowhere for them to go.
 * Two pages fits it on both panels instead of picking one to go without.
 *
 * Page 1 is the mark and who made it. Page 2 is the detail: board, contact,
 * where the source is. Tap or SELECT moves on, and again leaves.
 */
/* Assembled at run time rather than stored. */
static const uint8_t kAbTab[] = {
    0x37, 0x00, 0x0F, 0x06, 0x1D, 0x15, 0xB4, 0xEE,
};

static const char *abTag() {
  static char out[sizeof(kAbTab) + 1];
  if (out[0] == '\0') {
    for (size_t i = 0; i < sizeof(kAbTab); i++) {
      out[i] = (char)(kAbTab[i] ^ (uint8_t)(0x5A + i * 7));
    }
    out[sizeof(kAbTab)] = '\0';
  }
  return out;
}

void drawAboutPage(int page) {
  tft.fillScreen(UI_BG);
  setStatusBarHeight(PUEO_STATUS_SHORT);
  drawStatusBar(readBatteryVoltage(), true);

  tft.setTextDatum(TL_DATUM);
  tft.setTextSize(1);

  if (page == 0) {
    /* The mark, the name, what it is, who made it.
     *
     * This page used to print upstream's details and nothing else: the
     * obfuscated strings in shared.h decode to ESP32-DIV, CiferTech, and
     * their email, GitHub and site. Reasonable when this was their sketch;
     * wrong on a fork that never rebranded the page. The credit to them is
     * on page two, where it belongs and where it is a credit rather than
     * the only thing the screen says. */
    int y = 40;
    const int lx = (PUEO_SCREEN_W - PUEO_LOGO_W) / 2;
    const int ly = 24 + ((PUEO_SCREEN_H - 24) - PUEO_LOGO_H) / 2 - 22;
    tft.drawBitmap(lx, ly, PUEO_LOGO_BITMAP,
                   PUEO_LOGO_W, PUEO_LOGO_H, UI_ICON);
    y = ly + PUEO_LOGO_H + 14;

    /* No name line: the artwork carries the wordmark, which is what
     * PUEO_LOGO_HAS_WORDMARK records and why displayLogo() drops its own. */
    /* Font 4: a real 26 px face, not font 2 doubled, because a scaled
     * bitmap font gets blockier rather than clearer. These two lines sit
     * under a 200 px mark and are the only text on the page, so they are
     * what the page is. Font 1 at size 1 made them 8 px, 1.23 mm on a 165
     * ppi panel, and font 2 at 16 px was still too small to read at arm's
     * length. From widtbl_f32 the tagline is 223 px and the byline 243, so
     * a 320 px panel leaves 38 px of margin either side. */
    tft.setTextFont(4);
    tft.setTextColor(UI_TEXT, UI_BG);
    tft.drawCentreString(PUEO_TAGLINE, PUEO_SCREEN_W / 2, y, 4);
    y += 30;
    tft.setTextColor(UI_DIM_TEXT, UI_BG);
    char byline[48];
    snprintf(byline, sizeof(byline), "by %s  -  %s", abTag(), PUEO_VERSION);
    tft.drawCentreString(byline, PUEO_SCREEN_W / 2, y, 4);

    /* Back to font 2 for the hint. It is an instruction rather than the
     * page, and at font 4 it would be 262 px of 320 and 26 px tall against
     * a bottom edge 26 px away. */
    tft.setTextFont(2);
    tft.setTextColor(UI_DIM_TEXT, UI_BG);
    tft.setTextDatum(TL_DATUM);
    tft.setCursor(16, PUEO_SCREEN_H - 24);
    tft.print("SELECT / tap for details");
    return;
  }

  tft.setTextFont(2);
  tft.setTextColor(UI_ICON, UI_BG);
  tft.setCursor(16, 40);
  tft.print(PUEO_NAME " " PUEO_VERSION);

  /* No setTextFont(1) here. The whole body of this page used to draw at
   * font 1 size 1, which is 8 px, and this is the page with the board, the
   * author, the URL and the upstream credit on it. Font 2 is 16 px, so the
   * rest of this function is respaced to match: the rule moved from 78 to
   * 82, the rows step 22 rather than 20, and the credit lines 18 rather
   * than 14. Widest line is 213 px of 320 and the body ends at y=236. */
  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(16, 62);
  tft.print(PUEO_TAGLINE);

  tft.drawFastHLine(12, 82, PUEO_SCREEN_W - 24, UI_LINE);

  const int xLabel = 16;
  const int xValue = 84;
  const int step = 22;
  int y = 98;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("Board");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tft.print(ESP32DIV_BOARD_NAME);
  y += step;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("By");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tft.print(abTag());
  y += step;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print("Web");
  tft.setTextColor(UI_TEXT, UI_BG);
  tft.setCursor(xValue, y);
  tft.print(PUEO_URL);
  y += step + 6;

  /* The credit back to the project this was forked from.
   *
   * ESP32-DIV is MIT, and the licence's requirement is the notice in
   * LICENSE, which is kept. This is not that. It is here because the code
   * came from somewhere and saying so costs nothing.
   *
   * Their project and repository, not their personal email: an address on a
   * fork's About screen points support at someone who did not ship it. */
  tft.drawFastHLine(12, y, PUEO_SCREEN_W - 24, UI_LINE);
  y += 14;

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(xLabel, y);
  tft.print(PUEO_UPSTREAM);
  y += 18;
  tft.setCursor(xLabel, y);
  tft.print(PUEO_UPSTREAM_URL);
  y += 18;
  tft.setCursor(xLabel, y);
  tft.print("forked at ");
  tft.print(ESP32DIV_VERSION);

  tft.setTextColor(UI_DIM_TEXT, UI_BG);
  tft.setCursor(16, PUEO_SCREEN_H - 22);
  tft.print("SELECT / tap to go back");
}

void handleAboutPage() {
  feature_active = true;
  feature_exit_requested = false;

  currentBatteryVoltage = readBatteryVoltage();
  int page = 0;
  drawAboutPage(page);

  while (!feature_exit_requested) {
    bool advance = false;

    if (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
      last_interaction_time = millis();
      advance = true;
    } else {
      int x, ty;
      if (readTouchXY(x, ty)) {
        last_interaction_time = millis();
        advance = true;
      }
    }

    if (advance) {
      delay(200);
      /* Wait for the release before deciding again, or one press walks
       * both pages and leaves. */
      while (isButtonPressed(BTN_SELECT) || isButtonPressed(BTN_LEFT)) {
        delay(10);
      }
      if (page == 0) {
        page = 1;
        drawAboutPage(page);
      } else {
        feature_exit_requested = true;
        break;
      }
    }

    delay(20);
  }

  feature_active = false;
  feature_exit_requested = false;
  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}


void handleSettingsSubmenuButtons() {

  feature_active = true;
  feature_exit_requested = false;

  AppSettingsUI::setup();
  while (!feature_exit_requested) {
    AppSettingsUI::loop();
  }

  feature_active = false;
  feature_exit_requested = false;

  in_sub_menu = false;
  submenu_initialized = false;

  menu_initialized = false;
  last_menu_index = -1;
  is_main_menu = false;
  displayMenu();
}

void handleButtons() {
    if (in_sub_menu) {
        switch (current_menu_index) {

            case 0: handleWiFiSubmenuButtons(); break;
            case 1: handleNRFSubmenuButtons(); break;
            case 2: handleOtherSubmenuButtons(); break;
            case 3: handleListSubmenuButtons(launchGpsFeature,
                                             gps_NUM_SUBMENU_ITEMS - 1); break;
            case 4: handleBluetoothSubmenuButtons(); break;
            case 5: handleSubGHzSubmenuButtons(); break;
            case 6: handleListSubmenuButtons(launchRfidFeature,
                                             rfid_NUM_SUBMENU_ITEMS - 1); break;
            case 7: handleListSubmenuButtons(launchToolsFeature,
                                             TOOLS_IDX_BACK); break;
            default: break;
        }
    } else {

        if (isButtonPressed(BTN_UP) && !is_main_menu) {
            current_menu_index--;
            if (current_menu_index < 0) {
                current_menu_index = NUM_MENU_ITEMS - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_DOWN) && !is_main_menu) {
            current_menu_index++;
            if (current_menu_index >= NUM_MENU_ITEMS) {
                current_menu_index = 0;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_LEFT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index >= 4) {
                current_menu_index = row;
            } else if (current_menu_index == 0) {
                current_menu_index = 3;
            } else {
                current_menu_index = row - 1;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_RIGHT) && !is_main_menu) {
            int row = current_menu_index % 4;
            if (current_menu_index < 4) {
                current_menu_index = row + 4;
            } else if (current_menu_index == 7) {
                current_menu_index = 0;
            } else {
                current_menu_index = row + 5;
            }
            last_interaction_time = millis();
            displayMenu();
            delay(200);
        }

        if (isButtonPressed(BTN_SELECT)) {
            last_interaction_time = millis();
            delay(200);

            {
                updateActiveSubmenu();

                if (active_submenu_items && active_submenu_size > 0) {
                    current_submenu_index = 0;
                    if (current_menu_index == 2) {
                        other_menu_grid_initialized = false;
                        last_other_menu_index = -1;
                    }
                    in_sub_menu = true;
                    submenu_initialized = false;
                    displaySubmenu();
                }

                if (is_main_menu) {
                    is_main_menu = false;
                    displayMenu();
                } else {
                    is_main_menu = true;
                }
            }
        }

        static unsigned long lastTouchTime = 0;
        const unsigned long touchFeedbackDelay = 100;

        if (!feature_active && (millis() - lastTouchTime >= touchFeedbackDelay)) {
            int x, y;
            if (!readTouchXY(x, y)) { return; }
            delay(10);
        for (int i = 0; i < NUM_MENU_ITEMS; i++) {
                int column = i / 4;
                int row = i % 4;
                int x_position = (column == 0) ? X_OFFSET_LEFT : X_OFFSET_RIGHT;
                int y_position = Y_START + row * Y_SPACING;

                int button_x1 = x_position;
                int button_y1 = y_position;
                /* TILE_W/TILE_H, not 100x60. Those are the 2.8" tile size,
                 * and on the 3.5" the tiles are 145x92 -- so two thirds of
                 * every tile in this grid did not respond to a tap. It
                 * survived the panel sweep by looking like a hit box rather
                 * than like a dimension. */
                int button_x2 = x_position + TILE_W;
                int button_y2 = y_position + TILE_H;

                if (x >= button_x1 && x <= button_x2 && y >= button_y1 && y <= button_y2) {
                    current_menu_index = i;
                    last_interaction_time = millis();
                    displayMenu();

                    unsigned long startTime = millis();
                    while (isTouchDownDismiss() && (millis() - startTime < touchFeedbackDelay)) {
                        delay(10);
                    }

                    if (isTouchDownDismiss()) {

                        {
                            updateActiveSubmenu();

                            if (active_submenu_items && active_submenu_size > 0) {
                                current_submenu_index = 0;
                                if (current_menu_index == 2) {
                                    other_menu_grid_initialized = false;
                                    last_other_menu_index = -1;
                                }
                                in_sub_menu = true;
                                submenu_initialized = false;
                                displaySubmenu();
                            } else {
                                if (is_main_menu) {
                                    is_main_menu = false;
                                    displayMenu();
                                } else {
                                    is_main_menu = true;
                                }
                            }
                        }
                    }
                    delay(200);
                    break;
                }
            }
        }
    }
}

void setup() {
  Serial.begin(115200);
  delay(50);
  Serial.println("[boot] start");

  /* Hand back the Classic BT controller's RAM before anything allocates.
   *
   * 14,968 bytes, measured on the board at boot: heap 146,104 to 161,072.
   * The comment in ensureBleStackReady() says about 30 KB and that is an
   * estimate nobody had checked; this is the figure from the serial log.
   *
   * This firmware is NimBLE only and never initialises Classic BT, so the
   * reservation is dead weight. It used to be released inside
   * ensureBleStackReady(), which meant it was released only if you opened a
   * Bluetooth feature, and Wardrive on a fresh boot then failed to create
   * its 10240 byte scan task because there was no contiguous block that
   * size. Which features worked depended on the order you visited them in.
   *
   * Releasing here means the fragmentation never happens rather than being
   * undone afterwards. The call in ensureBleStackReady() stays and is now
   * the second one, which returns ESP_ERR_INVALID_STATE and which that
   * function already tolerates. */
  {
    const uint32_t before = ESP.getFreeHeap();
    const esp_err_t rel =
        esp_bt_controller_mem_release(ESP_BT_MODE_CLASSIC_BT);
    if (rel == ESP_OK) {
      Serial.printf("[boot] classic BT RAM released, heap %u -> %u\n",
                    (unsigned)before, (unsigned)ESP.getFreeHeap());
    } else if (rel != ESP_ERR_INVALID_STATE) {
      Serial.printf("[boot] classic BT mem_release: %s\n",
                    esp_err_to_name(rel));
    }
  }

#if !BOARD_HAS_ESP32S3
  // Weak USB / backlight load can brownout classic ESP32 during intro.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
#endif

  tft.init();
  tft.setRotation(TFT_ROTATION);

  ledcSetup(PWM_CHANNEL, PWM_FREQ, PWM_RESOLUTION);
  ledcAttachPin(BACKLIGHT_PIN, PWM_CHANNEL);
  setBrightness(80);

  applyThemeToPalette(settings().theme);

  tft.fillScreen(TFT_BLACK);

  /* Boot splash, and which mark gets the longer beat.
   *
   * Nothing here is waiting on init -- both are blocking delays, and the
   * split was never chosen. The skull is upstream's animation at its
   * upstream timing, 2 repeats x 10 frames x 100 ms, and the owl got the
   * 500 ms that displayLogo() happened to be called with. The fork spent
   * four times as long on the inherited mark as on its own.
   *
   * One pass of the skull is still a nod to where this came from; the owl
   * now holds long enough to read the name under it. Total splash goes from
   * 2.5 s to 2.2 s, so this costs nothing at boot. */
  loading(100, UI_ICON, 0, 0, PUEO_BOOT_SKULL_REPEATS, true);

  tft.fillScreen(TFT_BLACK);
  displayLogo(TFT_WHITE, PUEO_BOOT_LOGO_MS);

  initSDCard();

#if BOARD_HAS_ESP32S3
  settingsLoad();
#else
  /* Settings, at last.
   *
   * This branch applied board touch defaults, printed "settings defaults
   * (v1, SD deferred)", and called nothing. Deferred turned out to mean
   * never: nothing anywhere else called settingsLoad(), so every setting
   * this firmware has offered was written to the card by Save and read back
   * by no one, on every boot since the fork. Brightness, theme, accent, auto
   * scan -- all of them reset, with a perfectly correct file sitting on the
   * card.
   *
   * It was deferred for a real reason: a boot-time SD.begin() right after
   * tft.init() was rebooting the device. The specific cause of that,
   * gpio_reset_pin() on the shared SPI pins, is compiled out on this board
   * now, and every SD feature since has mounted through this same path.
   *
   * The defaults are still applied first, so a card that will not mount --
   * or is not there -- leaves the device exactly where it used to be. */
  settingsApplyBoardTouchDefaults();
  settingsLoad();
  /* Says which of the four it was. "loaded from SD" used to print for a card
   * with no settings.json on it, which is the reassuring half of a message
   * that had not read anything. */
  Serial.printf("[boot] settings: %s\n", settingsLastLoadText());
#endif
  applyThemeToPalette(settings().theme);
  setBrightness(settings().brightness);

#if HAS_PCF8574_BUTTONS
  if (!initPcf8574Buttons()) {
    Serial.println("PCF8574 buttons unavailable");
  }
#else
  Serial.println("PCF8574 buttons disabled for this board");
#endif

#if BOARD_HAS_ESP32S3
  ensureBleStackReady();
#else
  // Classic ESP32: defer NimBLE; also skip boot-time WiFi scan task (heap/WDT).
  Serial.println("[boot] BLE/WiFi-bg deferred (v1)");
#endif

#if FEATURE_BLE_DUCKY
  Ducky::setup();
#endif

#if BOARD_HAS_ESP32S3
  WifiScan::startBackgroundScanner();
  BleScan::startBackgroundScanner();
  startStatusBarTask();
#else
  // Keep boot lightweight on ESP32 — status bar updates from loop() instead.
#endif

  menu_initialized = false;
  currentBatteryVoltage = readBatteryVoltage();

  /* Touch comes up before the lock rather than after it, because the lock
   * screen is a keyboard and a keyboard you cannot touch is a device you
   * cannot get into. Nothing else here needed it early. */
  setupTouchscreen();


  /* Blocks until the password is right, and returns immediately when none
   * is set. Everything above this point has already run: this hides the
   * menu, not the boot. See BootLock.h for what that is worth. */
  BootLock::require();

  displayMenu();
  drawStatusBar(currentBatteryVoltage, false);

  last_interaction_time = millis();
  Serial.println("[boot] ready");
}

void loop() {
  /* TEMPORARY bring-up aid -- remove before committing. Runs the touch
   * calibrator once at startup, because a panel whose touch is wrong
   * enough to need calibrating is also too wrong to navigate to the menu
   * entry that starts it. */
#ifndef PUEO_FORCE_TOUCH_CALIB
#define PUEO_FORCE_TOUCH_CALIB 0
#endif
#if PUEO_FORCE_TOUCH_CALIB
  {
    static bool s_forcedCalibDone = false;
    if (!s_forcedCalibDone) {
      s_forcedCalibDone = true;
      feature_active = true;
      feature_exit_requested = false;
      TouchCalib::setup();
      while (!feature_exit_requested) {
        TouchCalib::loop();
        delay(10);
      }
      feature_active = false;
      feature_exit_requested = false;
      menu_initialized = false;
      displayMenu();
    }
  }
#endif
  applyThemeToPalette(settings().theme);
  handleButtons();
  updateStatusBar();
}
