#include "backlight.h"

#include <Arduino.h>

#include "board_pins.h"

namespace backlight {

namespace {
// 1 kHz: at 20 kHz the LEDC channel never starts on this board (duty and
// frequency read back 0) and the screen stays black; 1 kHz and 200 Hz
// work (display diag round 4, 2026-11-01).
constexpr uint32_t PWM_HZ = 1000;
constexpr uint8_t PWM_BITS = 8;
}  // namespace

void begin() {
  ledcAttach(pins::BACKLIGHT, PWM_HZ, PWM_BITS);
  set(0);
}

void set(uint8_t level) { ledcWrite(pins::BACKLIGHT, level); }

}  // namespace backlight
