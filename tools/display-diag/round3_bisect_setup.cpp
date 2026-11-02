// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Display diagnostic round 3 (archived, see README.md in this folder):
// bisect why the M1 app stays black while diag rounds 1–2 lit up. Steps run
// once, 5 s each, each with a huge step number; the last step keeps running
// the M1 touch test.
//
//   1  panel + fixups, backlight via digitalWrite            (known good)
//   2  backlight → LEDC PWM, pin previously a GPIO output   (diag 2 "E")
//   3  backlight pin reset, then LEDC with no pinMode        (what M1 does)
//   4  backlight digitalWrite again + GT911 touch init on I²C 19/45
//   5  backlight → LEDC (as step 2) with touch active, M1 test pattern
//
// "Saw 1 2 _ 4 5" pins it on step 3, etc.
//
// Result 2026-11-01: only step 1 visible → LEDC at 20 kHz kills the
// backlight.

#include <Arduino.h>
#include <driver/gpio.h>

#include "board_pins.h"
#include "hw/panel.h"
#include "hw/touch_gt911.h"

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

Arduino_RGB_Display* gfx = nullptr;
touch::Info tinfo;
bool touch_ok = false;

void step(int n, const char* what, uint16_t bg) {
  Serial.printf("step %d: %s\n", n, what);
  gfx->fillScreen(bg);
  gfx->setTextColor(rgb565(255, 255, 255));
  gfx->setTextSize(24);  // 6×8 font → 144×192 px digit
  gfx->setCursor(SCREEN_W / 2 - 72, 80);
  gfx->print(n);
  gfx->setTextSize(2);
  gfx->setCursor(16, SCREEN_H - 40);
  gfx->print(what);
}

void digitalBacklight() {
  ledcDetach(pins::BACKLIGHT);
  pinMode(pins::BACKLIGHT, OUTPUT);
  digitalWrite(pins::BACKLIGHT, HIGH);
}

void pwmBacklight() {
  bool ok = ledcAttach(pins::BACKLIGHT, 20000, 8);
  ledcWrite(pins::BACKLIGHT, 200);
  Serial.printf("  ledcAttach -> %s, duty %u\n", ok ? "ok" : "FAILED",
                (unsigned)ledcRead(pins::BACKLIGHT));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\ndisplay diag round 3 (bisect)");

  digitalBacklight();
  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed");
    return;
  }
  step(1, "panel+fixups, digital backlight", rgb565(0, 0, 160));
  delay(5000);

  pwmBacklight();
  step(2, "LEDC after GPIO output", rgb565(0, 120, 0));
  delay(5000);

  ledcDetach(pins::BACKLIGHT);
  gpio_reset_pin((gpio_num_t)pins::BACKLIGHT);
  pwmBacklight();
  step(3, "pin reset, LEDC no pinMode (M1)", rgb565(140, 0, 0));
  delay(5000);

  digitalBacklight();
  touch_ok = touch::begin(&tinfo);
  Serial.printf("  touch %s\n", touch_ok ? "ok" : "not found");
  step(4, "digital backlight + touch init", rgb565(120, 0, 120));
  delay(5000);

  pwmBacklight();
  step(5, "LEDC + touch: touch to draw dots", rgb565(0, 0, 0));
}

void loop() {
  static uint32_t last = 0;
  if (!gfx || !touch_ok || millis() - last < 33) return;
  last = millis();
  touch::Point p = touch::read();
  if (p.down) {
    gfx->fillCircle(p.x, p.y, 6, rgb565(0, 255, 0));
    Serial.printf("touch x=%u y=%u\n", p.x, p.y);
  }
}
