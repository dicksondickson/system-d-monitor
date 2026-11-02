#include "touch_gt911.h"

#include <Arduino.h>
#include <Wire.h>

#include "board_pins.h"

namespace touch {

namespace {
constexpr uint32_t I2C_HZ = 400000;
constexpr uint16_t REG_PRODUCT_ID = 0x8140;  // 4 ASCII bytes
constexpr uint16_t REG_X_MAX = 0x8048;       // u16 LE, then Y max
constexpr uint16_t REG_STATUS = 0x814E;      // bit7 = ready, low nibble = points
constexpr uint16_t REG_POINT1 = 0x8150;      // x lo, x hi, y lo, y hi, …

uint8_t g_addr = 0;

bool readRegs(uint16_t reg, uint8_t* buf, size_t len) {
  Wire.beginTransmission(g_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  if (Wire.endTransmission() != 0) return false;
  if (Wire.requestFrom(g_addr, (uint8_t)len) != len) return false;
  for (size_t i = 0; i < len; i++) buf[i] = Wire.read();
  return true;
}

bool writeReg(uint16_t reg, uint8_t value) {
  Wire.beginTransmission(g_addr);
  Wire.write(reg >> 8);
  Wire.write(reg & 0xFF);
  Wire.write(value);
  return Wire.endTransmission() == 0;
}
}  // namespace

bool begin(Info* info) {
  Wire.begin(pins::TOUCH_SDA, pins::TOUCH_SCL, I2C_HZ);
  *info = {};
  for (uint8_t addr : {0x5D, 0x14}) {
    Wire.beginTransmission(addr);
    if (Wire.endTransmission() == 0) {
      g_addr = addr;
      break;
    }
  }
  if (!g_addr) return false;
  info->addr = g_addr;

  uint8_t buf[4];
  if (readRegs(REG_PRODUCT_ID, buf, 4)) {
    memcpy(info->product, buf, 4);
    info->product[4] = '\0';
  }
  if (readRegs(REG_X_MAX, buf, 4)) {
    info->max_x = buf[0] | (buf[1] << 8);
    info->max_y = buf[2] | (buf[3] << 8);
  }
  return true;
}

Point read() {
  Point p = {false, false, 0, 0};
  if (!g_addr) return p;
  uint8_t status;
  if (!readRegs(REG_STATUS, &status, 1)) return p;
  if (!(status & 0x80)) return p;  // no fresh data yet
  p.fresh = true;
  uint8_t n = status & 0x0F;
  if (n > 0 && n <= 5) {
    uint8_t b[4];
    if (readRegs(REG_POINT1, b, 4)) {
      p.down = true;
      p.x = b[0] | (b[1] << 8);
      p.y = b[2] | (b[3] << 8);
    }
  }
  writeReg(REG_STATUS, 0);  // ack, or the GT911 stops updating
  return p;
}

}  // namespace touch
