// Copyright (c) 2026 Dickson Chow (https://github.com/dicksondickson)
// SPDX-License-Identifier: MIT

// Display diagnostic round 1 (archived, see README.md in this folder).
// Separates "backlight dead" from "panel not taking pixels" from "wrong
// ST7701 init sequence".
//
//   1. Backlight blinks 3× via plain digitalWrite (no PWM), then stays on.
//      → no blinking at all: backlight/power problem, not the panel.
//   2. Loops over init sequences A, B, C. Each is re-sent over the 3-wire
//      SPI bus, then the screen cycles RED → GREEN → BLUE → WHITE with the
//      sequence letter drawn huge in the middle (~6 s per sequence).
//      → note which letter(s) show colours.
//
// Result 2026-11-01: blinks; all three sequences show a picture, colours
// inverted.

#include <Arduino.h>

#include "board_pins.h"
#include "hw/panel.h"
#include "st7701_hsd040bpn1.h"

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

struct InitSeq {
  char letter;
  const char* name;
  const uint8_t* ops;
  size_t len;
};

const InitSeq SEQS[] = {
    {'A', "arduino_gfx type9 (86BOX GUITION)", st7701_type9_init_operations,
     sizeof(st7701_type9_init_operations)},
    {'B', "vendor HSD040BPN1 listing", st7701_hsd040bpn1_init_operations,
     sizeof(st7701_hsd040bpn1_init_operations)},
    {'C', "arduino_gfx type1 (vendor demo)", st7701_type1_init_operations,
     sizeof(st7701_type1_init_operations)},
};

Arduino_RGB_Display* gfx = nullptr;

void blinkBacklight() {
  pinMode(pins::BACKLIGHT, OUTPUT);
  for (int i = 0; i < 3; i++) {
    digitalWrite(pins::BACKLIGHT, HIGH);
    delay(700);
    digitalWrite(pins::BACKLIGHT, LOW);
    delay(400);
  }
  digitalWrite(pins::BACKLIGHT, HIGH);
}

void showColour(uint16_t bg, uint16_t fg, char letter, const char* colour) {
  gfx->fillScreen(bg);
  gfx->setTextColor(fg);
  gfx->setTextSize(20);  // 6×8 font ×20 → 120×160 px letter
  gfx->setCursor(SCREEN_W / 2 - 60, SCREEN_H / 2 - 100);
  gfx->print(letter);
  gfx->setTextSize(4);
  gfx->setCursor(SCREEN_W / 2 - (int)strlen(colour) * 12, SCREEN_H / 2 + 90);
  gfx->print(colour);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\ndisplay diag");
  Serial.println("backlight: blinking 3x via digitalWrite(38)");
  blinkBacklight();

  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed");
    return;
  }
  Serial.printf("panel begin ok, framebuffer=%p\n", gfx->getFramebuffer());
}

void loop() {
  if (!gfx) return;
  for (const InitSeq& s : SEQS) {
    Serial.printf("sequence %c: %s\n", s.letter, s.name);
    panel::initBus()->batchOperation(s.ops, s.len);
    showColour(rgb565(255, 0, 0), rgb565(255, 255, 255), s.letter, "RED");
    delay(1500);
    showColour(rgb565(0, 255, 0), rgb565(0, 0, 0), s.letter, "GREEN");
    delay(1500);
    showColour(rgb565(0, 0, 255), rgb565(255, 255, 255), s.letter, "BLUE");
    delay(1500);
    showColour(rgb565(255, 255, 255), rgb565(0, 0, 0), s.letter, "WHITE");
    delay(1500);
  }
}
