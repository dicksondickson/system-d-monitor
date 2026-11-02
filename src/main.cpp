// System-D Monitor (spec.md): live TensorFold/vLLM dashboard on the Guition
// ESP32-S3-4848S040.
//
//   tap left third / right third   previous / next server (with > 1 server)
//   tap centre                     overview ↔ graphs (anywhere with 1 server)
//   tap bottom-left corner (≡)     settings: refresh rate, brightness, theme
//   hold 2.2 s anywhere            screen off (backlight); a tap turns it back on
//
// One poller task per server (core 0) fetches the data; this loop (core 1)
// handles touch and redraws after every poll and every clock second. Theme,
// page and server are saved in NVS. Configuration: include/config.h (see
// config.example.h).

#include <Arduino.h>
#include <Preferences.h>

#include "app_config.h"
#include "board_pins.h"
#include "gfx/grid.h"
#include "gfx/theme.h"
#include "hw/backlight.h"
#include "hw/panel.h"
#include "hw/screen.h"
#include "hw/touch_gt911.h"
#include "net/poller.h"
#include "net/wifi.h"
#include "ui/pages.h"

namespace {

constexpr uint32_t TOUCH_POLL_MS = 33;  // ~30 Hz
constexpr uint32_t TAP_MAX_MS = 500;    // spec.md §9
constexpr uint32_t HOLD_MS = 700;
// Hold this long: screen off. 2.2 s on the board; the README says "3 s" so a
// user who counts to three is sure to get there (user request).
constexpr uint32_t SLEEP_HOLD_MS = 2200;
constexpr int TAP_MAX_MOVE = 24;
constexpr uint32_t ROTATE_PAUSE_MS = 60000;  // a touch pauses auto-rotate (spec.md §9)

Arduino_RGB_Display* gfx = nullptr;
Preferences prefs;
Grid grid;
View view;
pages::Page page = pages::Page::OVERVIEW;
pages::Page back_to = pages::Page::OVERVIEW;  // where "done" in Settings returns
int theme_index = 0;
int list_page = 0;  // settings: page of the theme list
uint8_t brightness_pct = BRIGHTNESS_PCT;
int server = 0;  // index into kServers
uint32_t last_rotate = 0, last_touch_ms = 0;
bool touch_ok = false;
// Screen sleep: only the backlight goes off. The poller tasks keep running so
// history and averages are current on wake; redraws pause meanwhile.
bool asleep = false;

// Gesture state
bool pressed = false;
bool hold_fired = false;
bool swallow = false;  // this press woke or slept the screen: it does nothing else
uint32_t press_ms = 0;
int press_x = 0, press_y = 0, last_x = 0, last_y = 0;

// Header label: the configured name, or the host up to the first dot.
void serverName(const ServerCfg& s, char* out, size_t len) {
  if (s.name) {
    snprintf(out, len, "%s", s.name);
    return;
  }
  const char* dot = strchr(s.host, '.');
  snprintf(out, len, "%.*s", dot ? (int)(dot - s.host) : (int)strlen(s.host), s.host);
}

void refreshView() {
  net::clock(view.clock, sizeof view.clock);
  serverName(kServers[server], view.server, sizeof view.server);
  net::info(view.ip, sizeof view.ip, &view.wifi_bars);
  view.server_index = (uint8_t)server;
  view.server_count = (uint8_t)kServerCount;
  poller::snapshot(server, view, net::up());
  if (!CONFIG_PRESENT) snprintf(view.status, sizeof view.status, "no include/config.h");
}

pages::Settings settingsState() { return {poller::pollMs(), brightness_pct, theme_index, list_page}; }

void redraw() {
  if (page == pages::Page::SETTINGS) {
    pages::settings(grid, settingsState());
  } else {
    refreshView();
    pages::draw(page, grid, view);
  }
  screen::present(grid, theme::get(theme_index));
}

void openSettings() {
  back_to = page;
  page = pages::Page::SETTINGS;
  list_page = theme_index / pages::THEMES_PER_PAGE;
  redraw();
}

void setTheme(int index) {
  theme_index = index;
  prefs.putString("theme", theme::get(theme_index).name);
  Serial.printf("theme -> %s\n", theme::get(theme_index).name);
  screen::invalidate();
  redraw();
}

void stepPoll(int dir) {
  int i = 0;
  while (i < pages::POLL_OPTION_COUNT - 1 && pages::POLL_OPTIONS_MS[i] < poller::pollMs()) i++;
  i += dir;
  if (i < 0 || i >= pages::POLL_OPTION_COUNT) return;
  poller::setPollMs(pages::POLL_OPTIONS_MS[i]);
  prefs.putUInt("poll_ms", pages::POLL_OPTIONS_MS[i]);
  Serial.printf("poll -> %u ms\n", (unsigned)pages::POLL_OPTIONS_MS[i]);
  redraw();
}

void stepBrightness(int dir) {
  int i = 0;
  while (i < pages::BRIGHTNESS_OPTION_COUNT - 1 && pages::BRIGHTNESS_OPTIONS_PCT[i] < brightness_pct) i++;
  i += dir;
  if (i < 0 || i >= pages::BRIGHTNESS_OPTION_COUNT) return;
  brightness_pct = pages::BRIGHTNESS_OPTIONS_PCT[i];
  backlight::set(pages::brightnessDuty(brightness_pct));
  prefs.putUChar("bright_pct", brightness_pct);
  Serial.printf("brightness -> %u%% (duty %u)\n", brightness_pct, pages::brightnessDuty(brightness_pct));
  redraw();
}

void onSettingsTap(int x, int y) {
  pages::SettingsHit h = pages::settingsHit(x, y, settingsState());
  static const char* const KIND[] = {"none",  "refresh -", "refresh +", "brightness -", "brightness +",
                                     "theme", "page -",    "page +",    "done"};
  Serial.printf("  -> %s%s\n", KIND[h.kind], h.kind == pages::SettingsHit::THEME ? theme::get(h.theme).name : "");
  switch (h.kind) {
    case pages::SettingsHit::POLL_PREV: return stepPoll(-1);
    case pages::SettingsHit::POLL_NEXT: return stepPoll(+1);
    case pages::SettingsHit::BRIGHT_PREV: return stepBrightness(-1);
    case pages::SettingsHit::BRIGHT_NEXT: return stepBrightness(+1);
    case pages::SettingsHit::THEME: return setTheme(h.theme);
    case pages::SettingsHit::PAGE_PREV:
      if (list_page > 0) { list_page--; redraw(); }
      return;
    case pages::SettingsHit::PAGE_NEXT:
      if (list_page + 1 < pages::themePages()) { list_page++; redraw(); }
      return;
    case pages::SettingsHit::DONE:
      page = back_to;
      return redraw();
    case pages::SettingsHit::NONE: return;
  }
}

void showServer(int index) {
  server = (index + kServerCount) % kServerCount;
  prefs.putUChar("server", (uint8_t)server);
  Serial.printf("server -> %d (%s)\n", server, kServers[server].host);
  redraw();
}

void onTap(int x, int y) {
  Serial.printf("tap x=%d y=%d (col %d, row %d) on %s\n", x, y, x / 10, y / 24,
                page == pages::Page::SETTINGS ? "settings" : "dashboard");
  if (page == pages::Page::SETTINGS) return onSettingsTap(x, y);
  if (pages::isSettingsCorner(x, y)) return openSettings();
  if (kServerCount > 1 && x < SCREEN_W / 3) return showServer(server - 1);
  if (kServerCount > 1 && x >= SCREEN_W * 2 / 3) return showServer(server + 1);
  page = page == pages::Page::OVERVIEW ? pages::Page::GRAPHS : pages::Page::OVERVIEW;
  prefs.putUChar("page", (uint8_t)page);
  redraw();
}

void sleepScreen() {
  asleep = true;
  backlight::set(0);
  Serial.println("screen -> off");
}

void wakeScreen() {
  asleep = false;
  redraw();  // catch up first, so the backlight comes on over a fresh frame
  backlight::set(pages::brightnessDuty(brightness_pct));
  Serial.println("screen -> on");
}

void pollTouch(uint32_t now) {
  touch::Point p = touch::read();
  if (p.fresh && p.down) {
    last_touch_ms = now;
    if (!pressed) {
      pressed = true;
      hold_fired = false;
      press_ms = now;
      press_x = p.x;
      press_y = p.y;
      swallow = asleep;
      if (asleep) wakeScreen();  // on touch-down, so it feels immediate
    }
    last_x = p.x;
    last_y = p.y;
  } else if (p.fresh && pressed) {  // released
    pressed = false;
    const int moved = abs(last_x - press_x) + abs(last_y - press_y);
    if (!swallow && !hold_fired && now - press_ms <= TAP_MAX_MS && moved < TAP_MAX_MOVE)
      onTap(press_x, press_y);
  }
  if (pressed && !hold_fired && now - press_ms >= HOLD_MS) hold_fired = true;  // a hold isn't a tap
  // Sleep fires while the finger is still down, so it's clear it worked;
  // lifting that finger doesn't wake it. A drag doesn't count.
  if (pressed && !swallow && now - press_ms >= SLEEP_HOLD_MS &&
      abs(last_x - press_x) + abs(last_y - press_y) < TAP_MAX_MOVE) {
    swallow = true;
    sleepScreen();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n\nsystem-d monitor — %d server(s), poll %u ms%s\n", kServerCount,
                (unsigned)POLL_MS, CONFIG_PRESENT ? "" : " (no config.h: Wi-Fi off)");
  for (int i = 0; i < kServerCount; i++)
    Serial.printf("  [%d] %s:%u\n", i, kServers[i].host, (unsigned)kServers[i].port);

  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed (psram / RGB bus)");
    return;
  }
  screen::begin(gfx);

  prefs.begin("sysd", false);
  theme_index = theme::find(prefs.getString("theme", theme::DEFAULT_NAME).c_str());
  const uint32_t poll_ms = prefs.getUInt("poll_ms", POLL_MS);
  brightness_pct = prefs.getUChar("bright_pct", BRIGHTNESS_PCT);
  page = prefs.getUChar("page", 0) ? pages::Page::GRAPHS : pages::Page::OVERVIEW;
  server = prefs.getUChar("server", 0) % kServerCount;

  touch::Info tinfo;
  touch_ok = touch::begin(&tinfo);
  Serial.printf("touch %s\n", touch_ok ? "ok" : "not found");

  net::begin(WIFI_SSID, WIFI_PASS, TZ_POSIX);
  poller::begin(kServers, kServerCount, poll_ms, CONNECT_MS, READ_MS);

  redraw();
  backlight::begin();  // after panel::begin(), see backlight.h
  backlight::set(pages::brightnessDuty(brightness_pct));
}

void loop() {
  if (!gfx) return;
  static uint32_t last_touch = 0, last_tick = 0, seen_gen = 0;
  const uint32_t now = millis();

  if (touch_ok && now - last_touch >= TOUCH_POLL_MS) {
    last_touch = now;
    pollTouch(now);
  }
  if (asleep) {  // nothing visible: skip auto-rotate and redraws
    delay(1);
    return;
  }
  if (ROTATE_S > 0 && kServerCount > 1 && now - last_rotate >= ROTATE_S * 1000u &&
      now - last_touch_ms >= ROTATE_PAUSE_MS) {
    last_rotate = now;
    showServer(server + 1);
  }
  const uint32_t gen = poller::generation(server);
  if (page == pages::Page::SETTINGS) {
    // static screen: only redraws on touch
  } else if (gen != seen_gen || now - last_tick >= 1000) {
    seen_gen = gen;
    last_tick = now;
    redraw();
  }
  delay(1);
}
