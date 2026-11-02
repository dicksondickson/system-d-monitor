// System-D Monitor (spec.md): live TensorFold/vLLM dashboard on the Guition
// ESP32-S3-4848S040.
//
//   tap        overview ↔ graphs
//   hold 0.7 s cycle theme (ansi → btop)
//
// The poller task (core 0) fetches the server; this loop (core 1) handles
// touch and redraws after every poll and every clock second. Theme and page
// are saved in NVS. Configuration: include/config.h (see config.example.h).

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
constexpr int TAP_MAX_MOVE = 24;

Arduino_RGB_Display* gfx = nullptr;
Preferences prefs;
Grid grid;
View view;
pages::Page page = pages::Page::OVERVIEW;
int theme_index = 0;
bool touch_ok = false;

// Gesture state
bool pressed = false;
bool hold_fired = false;
uint32_t press_ms = 0;
int press_x = 0, press_y = 0, last_x = 0, last_y = 0;

// Header label: SERVER_NAME, or SERVER_HOST up to the first dot.
void serverName(char* out, size_t len) {
#ifdef SERVER_NAME
  snprintf(out, len, "%s", SERVER_NAME);
#else
  const char* dot = strchr(SERVER_HOST, '.');
  int n = dot ? (int)(dot - SERVER_HOST) : (int)strlen(SERVER_HOST);
  snprintf(out, len, "%.*s", n, SERVER_HOST);
#endif
}

void refreshView() {
  net::clock(view.clock, sizeof view.clock);
  serverName(view.server, sizeof view.server);
  net::info(view.ip, sizeof view.ip, &view.wifi_bars);
  poller::snapshot(view, net::up());
  if (!CONFIG_PRESENT) snprintf(view.status, sizeof view.status, "no include/config.h");
}

void redraw() {
  refreshView();
  pages::draw(page, grid, view);
  screen::present(grid, theme::get(theme_index));
}

void onTap() {
  page = page == pages::Page::OVERVIEW ? pages::Page::GRAPHS : pages::Page::OVERVIEW;
  prefs.putUChar("page", (uint8_t)page);
  redraw();
}

void onHold() {
  theme_index = (theme_index + 1) % theme::COUNT;
  prefs.putUChar("theme", (uint8_t)theme_index);
  Serial.printf("theme -> %s\n", theme::get(theme_index).name);
  screen::invalidate();
  redraw();
}

void pollTouch(uint32_t now) {
  touch::Point p = touch::read();
  if (p.fresh && p.down) {
    if (!pressed) {
      pressed = true;
      hold_fired = false;
      press_ms = now;
      press_x = p.x;
      press_y = p.y;
    }
    last_x = p.x;
    last_y = p.y;
  } else if (p.fresh && pressed) {  // released
    pressed = false;
    const int moved = abs(last_x - press_x) + abs(last_y - press_y);
    if (!hold_fired && now - press_ms <= TAP_MAX_MS && moved < TAP_MAX_MOVE) onTap();
  }
  if (pressed && !hold_fired && now - press_ms >= HOLD_MS) {
    hold_fired = true;
    onHold();
  }
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.printf("\n\nsystem-d monitor — server %s:%u, poll %u ms%s\n", SERVER_HOST,
                (unsigned)SERVER_PORT, (unsigned)POLL_MS,
                CONFIG_PRESENT ? "" : " (no config.h: Wi-Fi off)");

  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed (psram / RGB bus)");
    return;
  }
  screen::begin(gfx);

  prefs.begin("sysd", false);
  theme_index = prefs.getUChar("theme", 0) % theme::COUNT;
  page = prefs.getUChar("page", 0) ? pages::Page::GRAPHS : pages::Page::OVERVIEW;

  touch::Info tinfo;
  touch_ok = touch::begin(&tinfo);
  Serial.printf("touch %s\n", touch_ok ? "ok" : "not found");

  net::begin(WIFI_SSID, WIFI_PASS, TZ_POSIX);
  poller::begin(SERVER_HOST, SERVER_PORT, POLL_MS, CONNECT_MS, READ_MS);

  redraw();
  backlight::begin();  // after panel::begin(), see backlight.h
  backlight::set(BRIGHTNESS);
}

void loop() {
  if (!gfx) return;
  static uint32_t last_touch = 0, last_tick = 0, seen_gen = 0;
  const uint32_t now = millis();

  if (touch_ok && now - last_touch >= TOUCH_POLL_MS) {
    last_touch = now;
    pollTouch(now);
  }
  const uint32_t gen = poller::generation();
  if (gen != seen_gen || now - last_tick >= 1000) {
    seen_gen = gen;
    last_tick = now;
    redraw();
  }
  delay(1);
}
