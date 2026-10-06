#include "Stealth.h"

#include "SettingsStore.h"
#include "shared.h"
#include "utils.h"

namespace Stealth {

bool on() { return settings().stealthMode; }

bool refuseAction(const char* feature) {
  if (!on()) return false;

  tft.fillScreen(TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  const int cx = PUEO_SCREEN_W / 2;
  int y = PUEO_SCREEN_H / 2 - 52;

  tft.setTextFont(2);
  tft.setTextColor(UI_WARN, TFT_BLACK);
  tft.drawString("STEALTH MODE", cx, y);
  y += 26;

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.drawString(feature != nullptr ? feature : "This tool", cx, y);
  y += 22;

  tft.setTextColor(UI_DIM_TEXT, TFT_BLACK);
  tft.drawString("transmits, so it is switched off", cx, y);
  y += 18;
  /* Says where to change it. A refusal that does not is a device that
   * appears broken to whoever did not set the switch. */
  tft.drawString("Settings > Stealth Mode", cx, y);

  tft.setTextDatum(TL_DATUM);
  tft.setTextFont(1);

  /* Held long enough to read, and skippable. Not a prompt: there is no
   * choice being offered here, so there is nothing to press OK on. */
  const uint32_t until = millis() + 2200;
  while ((int32_t)(millis() - until) < 0) {
    int tx, ty;
    if (readTouchXY(tx, ty)) break;
    if (isButtonPressed(BTN_SELECT)) break;
    delay(20);
  }

  return true;
}

/* Refusing a whole feature is refusing its one action and then leaving. */
bool refuse(const char* feature) {
  if (!refuseAction(feature)) return false;
  feature_exit_requested = true;
  return true;
}

}  // namespace Stealth
