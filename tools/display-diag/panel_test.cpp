// Panel test (was the M1 bring-up app, spec.md §11): panel + backlight +
// touch. Run with: tools/display-diag/run.sh panel upload
//
// What you should see:
//   top    — four bars, each labelled with its own colour name: RED GREEN BLUE WHITE
//   middle — three smooth left→right ramps (red, green, blue); steps or
//            jumps mean a mis-wired/mis-ordered data line
//   edges  — a 1 px white border on all four sides; a missing or doubled
//            edge means the porch timings need tuning
//   bottom — touch area: a dot follows your finger, raw x/y printed

#include <Arduino.h>
#include <esp_chip_info.h>
#include <esp_flash.h>

#include "board_pins.h"
#include "hw/backlight.h"
#include "hw/panel.h"
#include "hw/touch_gt911.h"

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
constexpr uint16_t BLACK = rgb565(0, 0, 0);
constexpr uint16_t WHITE = rgb565(255, 255, 255);
constexpr uint16_t RED = rgb565(255, 0, 0);
constexpr uint16_t GREEN = rgb565(0, 255, 0);
constexpr uint16_t BLUE = rgb565(0, 0, 255);
constexpr uint16_t GREY = rgb565(128, 128, 128);
constexpr uint16_t CYAN = rgb565(0, 205, 205);

constexpr int BARS_H = 120;
constexpr int RAMP_Y = BARS_H, RAMP_H = 40;
constexpr int TOUCH_Y = RAMP_Y + 3 * RAMP_H;  // 240
constexpr uint32_t TRAIL_RESET_MS = 1500;     // new stroke after this pause

Arduino_RGB_Display* gfx = nullptr;
touch::Info tinfo;
bool touch_ok = false;
uint32_t last_touch_ms = 0;

void centredText(const char* s, int cx, int cy, uint16_t fg, uint8_t size) {
  int16_t x1, y1;
  uint16_t w, h;
  gfx->setTextSize(size);
  gfx->getTextBounds(s, 0, 0, &x1, &y1, &w, &h);
  gfx->setTextColor(fg);
  gfx->setCursor(cx - w / 2, cy - h / 2);
  gfx->print(s);
}

void drawBars() {
  struct Bar { uint16_t colour; const char* name; uint16_t text; };
  const Bar bars[] = {{RED, "RED", WHITE}, {GREEN, "GREEN", BLACK},
                      {BLUE, "BLUE", WHITE}, {WHITE, "WHITE", BLACK}};
  const int w = SCREEN_W / 4;
  for (int i = 0; i < 4; i++) {
    gfx->fillRect(i * w, 0, w, BARS_H, bars[i].colour);
    centredText(bars[i].name, i * w + w / 2, BARS_H / 2, bars[i].text, 2);
  }
}

void drawRamps() {
  for (int x = 0; x < SCREEN_W; x++) {
    uint8_t v = x * 255 / (SCREEN_W - 1);
    gfx->drawFastVLine(x, RAMP_Y, RAMP_H, rgb565(v, 0, 0));
    gfx->drawFastVLine(x, RAMP_Y + RAMP_H, RAMP_H, rgb565(0, v, 0));
    gfx->drawFastVLine(x, RAMP_Y + 2 * RAMP_H, RAMP_H, rgb565(0, 0, v));
  }
}

void drawTouchArea() {
  gfx->fillRect(0, TOUCH_Y, SCREEN_W, SCREEN_H - TOUCH_Y, BLACK);
  gfx->setTextSize(2);
  gfx->setTextColor(CYAN);
  gfx->setCursor(12, TOUCH_Y + 10);
  gfx->print("system-d monitor  M1 bring-up");
  gfx->setTextColor(GREY);
  gfx->setCursor(12, TOUCH_Y + 34);
  if (touch_ok) {
    gfx->printf("GT911 \"%s\" @0x%02X  %ux%u", tinfo.product, tinfo.addr, tinfo.max_x,
                tinfo.max_y);
  } else {
    gfx->setTextColor(RED);
    gfx->print("GT911 not found");
  }
  gfx->setTextColor(GREY);
  gfx->setCursor(12, SCREEN_H - 26);
  gfx->print("touch anywhere below the ramps");
}

void drawBorder() { gfx->drawRect(0, 0, SCREEN_W, SCREEN_H, WHITE); }

void showTouch(const touch::Point& p, uint32_t now) {
  if (now - last_touch_ms > TRAIL_RESET_MS) {
    drawTouchArea();
    drawBorder();
  }
  last_touch_ms = now;

  // Coordinate readout (overwrite in place, background-filled text).
  gfx->setTextSize(2);
  gfx->setTextColor(WHITE, BLACK);
  gfx->setCursor(12, TOUCH_Y + 60);
  gfx->printf("x=%3u y=%3u   ", p.x, p.y);

  // Dot everywhere on screen so axis mirroring is obvious; it only stays
  // visible in the touch area because the test pattern is static above.
  if (p.y >= TOUCH_Y + 84 && p.y < SCREEN_H - 32) {
    gfx->fillCircle(p.x, p.y, 6, GREEN);
  }
}

void logBoot() {
  esp_chip_info_t chip;
  esp_chip_info(&chip);
  uint32_t flash = 0;
  esp_flash_get_size(nullptr, &flash);
  Serial.printf("\n\nsystem-d monitor — M1 bring-up\n");
  Serial.printf("chip  ESP32-S3 rev %d, %d cores, %u MHz\n", chip.revision, chip.cores,
                (unsigned)getCpuFrequencyMhz());
  Serial.printf("flash %u MB\n", (unsigned)(flash / (1024 * 1024)));
  Serial.printf("psram %u KB total, %u KB free\n", (unsigned)(ESP.getPsramSize() / 1024),
                (unsigned)(ESP.getFreePsram() / 1024));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  logBoot();

  if (!psramFound()) {
    Serial.println("FATAL psram not found — check board_build.arduino.memory_type = qio_opi");
    return;
  }

  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed (framebuffer alloc or RGB bus)");
    return;
  }
  Serial.printf("panel ok 480x480, psram free after framebuffer %u KB\n",
                (unsigned)(ESP.getFreePsram() / 1024));
  backlight::begin();  // must follow panel::begin(), see backlight.h

  touch_ok = touch::begin(&tinfo);
  if (touch_ok) {
    Serial.printf("touch GT911 product=\"%s\" addr=0x%02X max=%ux%u\n", tinfo.product,
                  tinfo.addr, tinfo.max_x, tinfo.max_y);
  } else {
    Serial.println("touch GT911 not found on 0x5D or 0x14");
  }

  gfx->fillScreen(BLACK);
  drawBars();
  drawRamps();
  drawTouchArea();
  drawBorder();

  // Fade the backlight in so it's visible that PWM works.
  for (int level = 0; level <= 200; level += 5) {
    backlight::set(level);
    delay(10);
  }
  Serial.println("ready — touch the screen");
}

void loop() {
  static uint32_t last_poll = 0;
  uint32_t now = millis();
  if (!gfx || !touch_ok || now - last_poll < 33) return;  // ~30 Hz
  last_poll = now;

  touch::Point p = touch::read();
  if (p.fresh && p.down) {
    showTouch(p, now);
    Serial.printf("touch x=%u y=%u\n", p.x, p.y);
  }
}
