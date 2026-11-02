// System-D Monitor — M2 (spec.md §11): the renderer and both pages on the
// panel, fed with static demo data (no networking yet).
//
//   tap        overview ↔ graphs
//   hold 0.7 s cycle theme (ansi → btop)
//
// Theme and page are saved in NVS. The header clock ticks from millis() so
// the diff-blit has something to do; serial logs repaint timings.

#include <Arduino.h>
#include <Preferences.h>

#include "board_pins.h"
#include "gfx/grid.h"
#include "gfx/theme.h"
#include "hw/backlight.h"
#include "hw/panel.h"
#include "hw/screen.h"
#include "hw/touch_gt911.h"
#include "ui/demo_view.h"
#include "ui/pages.h"

namespace {

constexpr uint8_t BRIGHTNESS = 200;
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

void redraw(const char* why) {
  uint32_t t0 = micros();
  pages::draw(page, grid, view);
  uint32_t t1 = micros();
  int cells = screen::present(grid, theme::get(theme_index));
  uint32_t t2 = micros();
  Serial.printf("%-6s layout %4u us, blit %5u us, %4d cells\n", why, (unsigned)(t1 - t0),
                (unsigned)(t2 - t1), cells);
}

void tickClock() {
  // Demo clock: 12:47:33 + uptime.
  uint32_t s = 12 * 3600 + 47 * 60 + 33 + millis() / 1000;
  snprintf(view.clock, sizeof view.clock, "%02u:%02u:%02u", (unsigned)(s / 3600 % 24),
           (unsigned)(s / 60 % 60), (unsigned)(s % 60));
}

void onTap() {
  page = page == pages::Page::OVERVIEW ? pages::Page::GRAPHS : pages::Page::OVERVIEW;
  prefs.putUChar("page", (uint8_t)page);
  redraw("tap");
}

void onHold() {
  theme_index = (theme_index + 1) % theme::COUNT;
  prefs.putUChar("theme", (uint8_t)theme_index);
  Serial.printf("theme -> %s\n", theme::get(theme_index).name);
  screen::invalidate();
  redraw("theme");
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
  Serial.println("\n\nsystem-d monitor — M2 renderer (demo data)");

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

  view = demoView();
  tickClock();
  redraw("boot");

  backlight::begin();  // after panel::begin(), see backlight.h
  backlight::set(BRIGHTNESS);
  Serial.printf("theme %s, page %s\n", theme::get(theme_index).name,
                page == pages::Page::OVERVIEW ? "overview" : "graphs");
}

void loop() {
  if (!gfx) return;
  static uint32_t last_touch = 0, last_clock = 0;
  uint32_t now = millis();

  if (touch_ok && now - last_touch >= TOUCH_POLL_MS) {
    last_touch = now;
    pollTouch(now);
  }
  if (now - last_clock >= 1000) {
    last_clock = now;
    tickClock();
    redraw("clock");
  }
}
