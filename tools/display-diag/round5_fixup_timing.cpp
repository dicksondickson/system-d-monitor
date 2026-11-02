// Display diagnostic round 5 (archived, see README.md in this folder): when
// does the panel accept the colour fixups (INVON + MADCTL=BGR)?
//
// M1 sends them inside the init sequence and again 50 ms after begin(), yet
// colours stay inverted + R/B swapped. Diag round 2 re-sent them every 4 s
// and they took. This round draws RED/GREEN/BLUE/WHITE bars once, then
// re-sends the fixups at growing delays, showing the step number:
//
//   step 1  t = 0.1 s     step 4  t = 2 s
//   step 2  t = 0.5 s     step 5  t = 4 s
//   step 3  t = 1 s       step 6  t = 8 s
//
// The step at which the bars start matching their labels is the answer.
//
// Result 2026-11-01: never correct → INVON / MADCTL have no visible effect.

#include <Arduino.h>

#include "board_pins.h"
#include "hw/backlight.h"
#include "hw/panel.h"

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}

Arduino_RGB_Display* gfx = nullptr;

void sendFixups() {
  const uint8_t ops[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,
      WRITE_COMMAND_8, 0x21,
      WRITE_C8_D8, 0x36, 0x08,
      END_WRITE,
  };
  panel::initBus()->batchOperation(ops, sizeof(ops));
}

void drawBars() {
  struct Bar { uint16_t colour, text; const char* name; };
  const Bar bars[] = {{rgb565(255, 0, 0), rgb565(255, 255, 255), "RED"},
                      {rgb565(0, 255, 0), rgb565(0, 0, 0), "GREEN"},
                      {rgb565(0, 0, 255), rgb565(255, 255, 255), "BLUE"},
                      {rgb565(255, 255, 255), rgb565(0, 0, 0), "WHITE"}};
  gfx->fillScreen(rgb565(0, 0, 0));
  for (int i = 0; i < 4; i++) {
    gfx->fillRect(0, i * 80, SCREEN_W, 80, bars[i].colour);
    gfx->setTextSize(4);
    gfx->setTextColor(bars[i].text);
    gfx->setCursor(20, i * 80 + 24);
    gfx->print(bars[i].name);
  }
}

void showStep(int n, const char* when) {
  gfx->fillRect(0, 320, SCREEN_W, SCREEN_H - 320, rgb565(0, 0, 0));
  gfx->setTextColor(rgb565(255, 255, 255));
  gfx->setTextSize(12);
  gfx->setCursor(30, 340);
  gfx->print(n);
  gfx->setTextSize(4);
  gfx->setCursor(150, 380);
  gfx->print(when);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\ndisplay diag round 5 (fixup timing)");
  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed");
    return;
  }
  uint32_t t0 = millis();
  backlight::begin();
  backlight::set(200);
  drawBars();

  struct Step { uint32_t at_ms; const char* label; };
  const Step steps[] = {{100, "t=0.1s"}, {500, "t=0.5s"}, {1000, "t=1s"},
                        {2000, "t=2s"},  {4000, "t=4s"},  {8000, "t=8s"}};
  for (int i = 0; i < 6; i++) {
    while (millis() - t0 < steps[i].at_ms) delay(1);
    sendFixups();
    showStep(i + 1, steps[i].label);
    Serial.printf("step %d: fixups sent at %s\n", i + 1, steps[i].label);
  }
  Serial.println("done");
}

void loop() {}
