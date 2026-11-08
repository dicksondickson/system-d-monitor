// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// SYSTEM-D Monitor
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
#include <esp_heap_caps.h>

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

constexpr char FW_VERSION[] = "v0.1.4";  // shown top-right in Settings; keep in step with CHANGELOG.md
constexpr uint32_t TOUCH_POLL_MS = 33;  // ~30 Hz
constexpr uint32_t TAP_MAX_MS = 500;    // SPEC.md §9
constexpr uint32_t HOLD_MS = 700;
// Hold this long: screen off. 2.2 s on the board; the README says "3 s" so a
// user who counts to three is sure to get there (user request).
constexpr uint32_t SLEEP_HOLD_MS = 2200;
constexpr int TAP_MAX_MOVE = 24;
constexpr uint32_t ROTATE_PAUSE_MS = 60000;  // a touch pauses auto-rotate (SPEC.md §9)

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

// Server and page are saved to NVS only once they've stayed put for
// SAVE_DELAY_MS, and only if they changed. A flash write holds off the LCD
// interrupts and can glitch the image (bring-up §2.10); this keeps it away
// from the redraw and poll a switch causes, and rapid switching writes
// nothing (user request, 2026-11-08: 9 s).
constexpr uint32_t SAVE_DELAY_MS = 9000;
uint8_t saved_server = 0, saved_page = 0;  // what NVS holds (no read-back)
bool save_pending = false;
uint32_t changed_ms = 0;
void markChanged() {
  save_pending = true;
  changed_ms = millis();
}
void saveIfSettled(uint32_t now) {
  if (!save_pending || now - changed_ms < SAVE_DELAY_MS) return;
  save_pending = false;
  const uint8_t p = page == pages::Page::GRAPHS ? 1 : 0;
  if (server != saved_server) {
    prefs.putUChar("server", (uint8_t)server);
    saved_server = (uint8_t)server;
    Serial.printf("saved server %d\n", server);
  }
  if (p != saved_page) {
    prefs.putUChar("page", p);
    saved_page = p;
    Serial.printf("saved page %u\n", p);
  }
}
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
  poller::snapshot(server, view, net::link());
  if (!CONFIG_PRESENT) snprintf(view.status, sizeof view.status, "no include/config.h");
}

pages::Settings settingsState() { return {poller::pollMs(), brightness_pct, theme_index, list_page, FW_VERSION}; }

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
  poller::setActive(server);
  markChanged();
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
  markChanged();
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

// Free memory on the serial log. Internal RAM is shared by Wi-Fi, the LCD
// bounce buffers and the task stacks; "min" is the lowest it has been since
// boot (bring-up §2.10).
constexpr uint32_t MEM_LOG_MS = 10000;
constexpr uint32_t WIFI_START_WAIT_MS = 2000;  // cap on the wait before the display starts
void logMemory(const char* when) {
  const auto kb = [](size_t b) { return (unsigned)(b / 1024); };
  Serial.printf("mem %s: internal %u KB free (min %u, largest block %u), dma %u KB, psram %u KB free\n",
                when, kb(heap_caps_get_free_size(MALLOC_CAP_INTERNAL)),
                kb(heap_caps_get_minimum_free_size(MALLOC_CAP_INTERNAL)),
                kb(heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL)),
                kb(heap_caps_get_free_size(MALLOC_CAP_DMA)), kb(heap_caps_get_free_size(MALLOC_CAP_SPIRAM)));
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

  logMemory("at boot");
  prefs.begin("sysd", false);
  theme_index = theme::find(prefs.getString("theme", theme::DEFAULT_NAME).c_str());
  const uint32_t poll_ms = prefs.getUInt("poll_ms", POLL_MS);
  brightness_pct = prefs.getUChar("bright_pct", BRIGHTNESS_PCT);
  saved_page = prefs.getUChar("page", 0) ? 1 : 0;
  page = saved_page ? pages::Page::GRAPHS : pages::Page::OVERVIEW;
  saved_server = prefs.getUChar("server", 0);
  server = saved_server % kServerCount;

  touch::Info tinfo;
  touch_ok = touch::begin(&tinfo);
  Serial.printf("touch %s\n", touch_ok ? "ok" : "not found");

  // Wi-Fi before the display: its start-up reads and writes flash (PHY
  // calibration, saved config), and a long flash operation while the RGB
  // stream runs can leave the image shifted until a reset (bring-up §2.10).
  // The internal RAM the bounce buffers need is still free (§2.8).
  net::begin(WIFI_SSID, WIFI_PASS, TZ_POSIX, HOSTNAME);
  const uint32_t wait_from = millis();
  while (!net::started() && millis() - wait_from < WIFI_START_WAIT_MS) delay(10);
  Serial.printf("wifi: started after %u ms\n", (unsigned)(millis() - wait_from));

  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed (psram / RGB bus)");
    return;
  }
  screen::begin(gfx);
  logMemory("after panel");

  poller::begin(kServers, kServerCount, server, poll_ms, CONNECT_MS, READ_MS);

  redraw();
  backlight::begin();  // after panel::begin(), see backlight.h
  backlight::set(pages::brightnessDuty(brightness_pct));
  logMemory("after setup");
}

void loop() {
  if (!gfx) return;
  static uint32_t last_touch = 0, last_tick = 0, seen_gen = 0, last_mem = 0;
  const uint32_t now = millis();
  if (now - last_mem >= MEM_LOG_MS) {
    last_mem = now;
    logMemory("now");
  }
  saveIfSettled(now);

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
