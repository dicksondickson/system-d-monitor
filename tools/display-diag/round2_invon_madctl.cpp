// Display diagnostic round 2 (archived, see README.md in this folder).
//
// Round 1 showed: backlight OK with digitalWrite, panel OK with every init
// sequence, but colours inverted (+ likely R/B swapped). This round fixes
// inversion (0x21) and tries the four colour-order combinations, then tests
// PWM backlight. Each phase shows four bars labelled RED GREEN BLUE WHITE
// and a phase letter; look for the phase where every bar matches its label.
//
//   A  INVON, MADCTL 0x00 (RGB)
//   B  INVON, MADCTL 0x08 (BGR)
//   C  INVON, MADCTL 0x00, R/B swapped in software (= swapping the pins)
//   D  INVON, MADCTL 0x08, R/B swapped in software
//   E  as A, but backlight switched to LEDC PWM (attached after panel init)
//      → if E is dark, the PWM backlight path is what blacked out M1.
//
// Result 2026-11-01: reported "B correct, E lit" — both later contradicted
// (rounds 4–5).

#include <Arduino.h>

#include "board_pins.h"
#include "hw/panel.h"

namespace {

constexpr uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b) {
  return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3);
}
uint16_t swapRB(uint16_t c) {
  return ((c & 0x001F) << 11) | (c & 0x07E0) | ((c & 0xF800) >> 11);
}

struct Phase {
  char letter;
  const char* name;
  uint8_t madctl;
  bool sw_swap;
  bool pwm_backlight;
};

const Phase PHASES[] = {
    {'A', "INVON MADCTL=00", 0x00, false, false},
    {'B', "INVON MADCTL=08", 0x08, false, false},
    {'C', "INVON MADCTL=00 swapRB", 0x00, true, false},
    {'D', "INVON MADCTL=08 swapRB", 0x08, true, false},
    {'E', "as A + LEDC PWM backlight", 0x00, false, true},
};

Arduino_RGB_Display* gfx = nullptr;

void sendPanelRegs(uint8_t madctl) {
  const uint8_t ops[] = {
      BEGIN_WRITE,
      WRITE_COMMAND_8, 0xFF, WRITE_BYTES, 5, 0x77, 0x01, 0x00, 0x00, 0x00,  // page 0
      WRITE_COMMAND_8, 0x21,                                                // INVON
      WRITE_C8_D8, 0x36, madctl,
      END_WRITE,
  };
  panel::initBus()->batchOperation(ops, sizeof(ops));
}

void backlightDigital() {
  ledcDetach(pins::BACKLIGHT);
  pinMode(pins::BACKLIGHT, OUTPUT);
  digitalWrite(pins::BACKLIGHT, HIGH);
}

void backlightPwm() {
  bool ok = ledcAttach(pins::BACKLIGHT, 20000, 8);
  ledcWrite(pins::BACKLIGHT, 200);
  Serial.printf("  ledcAttach(38, 20 kHz, 8 bit) -> %s, duty readback %u\n",
                ok ? "ok" : "FAILED", (unsigned)ledcRead(pins::BACKLIGHT));
}

void drawPhase(const Phase& p) {
  struct Bar { uint16_t colour, text; const char* name; };
  const Bar bars[] = {{rgb565(255, 0, 0), rgb565(255, 255, 255), "RED"},
                      {rgb565(0, 255, 0), rgb565(0, 0, 0), "GREEN"},
                      {rgb565(0, 0, 255), rgb565(255, 255, 255), "BLUE"},
                      {rgb565(255, 255, 255), rgb565(0, 0, 0), "WHITE"}};
  auto c = [&](uint16_t v) { return p.sw_swap ? swapRB(v) : v; };
  const int barH = 90;
  gfx->fillScreen(c(rgb565(0, 0, 0)));
  for (int i = 0; i < 4; i++) {
    gfx->fillRect(0, i * barH, SCREEN_W, barH, c(bars[i].colour));
    gfx->setTextSize(4);
    gfx->setTextColor(c(bars[i].text));
    gfx->setCursor(20, i * barH + 30);
    gfx->print(bars[i].name);
  }
  gfx->setTextColor(c(rgb565(255, 255, 255)));
  gfx->setTextSize(10);
  gfx->setCursor(SCREEN_W / 2 - 30, 4 * barH + 20);
  gfx->print(p.letter);
}

}  // namespace

void setup() {
  Serial.begin(115200);
  delay(200);
  Serial.println("\n\ndisplay diag round 2");
  backlightDigital();
  gfx = panel::begin();
  if (!gfx) {
    Serial.println("FATAL panel begin failed");
    return;
  }
}

void loop() {
  if (!gfx) return;
  for (const Phase& p : PHASES) {
    Serial.printf("phase %c: %s\n", p.letter, p.name);
    if (p.pwm_backlight) backlightPwm(); else backlightDigital();
    sendPanelRegs(p.madctl);
    drawPhase(p);
    delay(4000);
  }
}
