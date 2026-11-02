// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Display diagnostic round 4 (archived, see README.md in this folder):
// backlight PWM.
//
// Round 3: screen showed step 1 (digitalWrite backlight) and nothing after
// the first switch to LEDC PWM at 20 kHz — not even after going back to
// digitalWrite. This round runs once, 5 s per step, huge step number each:
//
//   1  digitalWrite HIGH                       (blue)
//   2  digitalWrite + GT911 touch init         (purple)
//   3  LEDC 20 kHz, duty 200/255               (green)
//   4  back to digitalWrite                    (blue)
//   5  LEDC 1 kHz                              (yellow)
//   6  back to digitalWrite                    (blue)
//   7  LEDC 200 Hz                             (red)
//   8  back to digitalWrite, touch draws dots  (black, stays)
//
// Result 2026-11-01: step 3 blank, all others lit (colours inverted).
// Serial: 20 kHz attach "ok" but duty and frequency read back 0.

#include <Arduino.h>

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
bool pwm_attached = false;

void step(int n, const char* what, uint16_t bg, uint16_t fg) {
  Serial.printf("step %d: %s\n", n, what);
  gfx->fillScreen(bg);
  gfx->setTextColor(fg);
  gfx->setTextSize(24);
  gfx->setCursor(SCREEN_W / 2 - 72, 80);
  gfx->print(n);
  gfx->setTextSize(2);
  gfx->setCursor(16, SCREEN_H - 40);
  gfx->print(what);
}

void digitalBacklight() {
  if (pwm_attached) {
    ledcDetach(pins::BACKLIGHT);
    pwm_attached = false;
  }
  pinMode(pins::BACKLIGHT, OUTPUT);
  digitalWrite(pins::BACKLIGHT, HIGH);
}

void pwmBacklight(uint32_t hz) {
  pwm_attached = ledcAttach(pins::BACKLIGHT, hz, 8);
  ledcWrite(pins::BACKLIGHT, 200);
  Serial.printf("  ledcAttach(%u Hz) -> %s, duty %u, freq readback %u\n", (unsigned)hz,
                pwm_attached ? "ok" : "FAILED", (unsigned)ledcRead(pins::BACKLIGHT),
                (unsigned)ledcReadFreq(pins::BACKLIGHT));
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\ndisplay diag round 4 (backlight PWM)");
  digitalBacklight();
  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed");
    return;
  }
  const uint16_t WHITE = rgb565(255, 255, 255), BLACK = rgb565(0, 0, 0);
  const uint16_t BLUE = rgb565(0, 0, 160);

  step(1, "digitalWrite HIGH", BLUE, WHITE);
  delay(5000);
  touch_ok = touch::begin(&tinfo);
  step(2, "digital + touch init", rgb565(120, 0, 120), WHITE);
  delay(5000);
  pwmBacklight(20000);
  step(3, "LEDC 20 kHz", rgb565(0, 120, 0), WHITE);
  delay(5000);
  digitalBacklight();
  step(4, "digitalWrite again", BLUE, WHITE);
  delay(5000);
  pwmBacklight(1000);
  step(5, "LEDC 1 kHz", rgb565(200, 200, 0), BLACK);
  delay(5000);
  digitalBacklight();
  step(6, "digitalWrite again", BLUE, WHITE);
  delay(5000);
  pwmBacklight(200);
  step(7, "LEDC 200 Hz", rgb565(160, 0, 0), WHITE);
  delay(5000);
  digitalBacklight();
  step(8, "digital, touch to draw dots", BLACK, WHITE);
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
