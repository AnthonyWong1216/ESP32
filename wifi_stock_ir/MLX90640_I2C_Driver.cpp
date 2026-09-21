#include <Arduino.h>
#include <Wire.h>
#include "MLX90640_I2C_Driver.h"

// The ESP32 Wire buffer is 128 bytes, but 32-byte transfers are more reliable
// with the module's level shifter and long Dupont wires.
static constexpr uint8_t I2C_CHUNK_BYTES = 32;

void MLX90640_I2CInit(void) {}

int MLX90640_I2CRead(uint8_t address, unsigned int startAddress,
                     unsigned int wordCount, uint16_t *data) {
  uint16_t wordsDone = 0;

  while (wordsDone < wordCount) {
    const uint16_t wordsRemaining = wordCount - wordsDone;
    const uint8_t wordsThisTransfer = wordsRemaining < (I2C_CHUNK_BYTES / 2)
        ? static_cast<uint8_t>(wordsRemaining)
        : (I2C_CHUNK_BYTES / 2);
    const uint8_t bytesThisTransfer = wordsThisTransfer * 2;
    const uint16_t registerAddress = startAddress + wordsDone;

    Wire.beginTransmission(address);
    Wire.write(registerAddress >> 8);
    Wire.write(registerAddress & 0xFF);
    if (Wire.endTransmission(false) != 0) return -1;

    // Explicit int arguments select ESP32 Arduino core's int/int/int overload.
    // This avoids an otherwise harmless overload-ambiguity compiler diagnostic.
    const uint8_t got = Wire.requestFrom(static_cast<int>(address),
                                         static_cast<int>(bytesThisTransfer), 1);
    if (got != bytesThisTransfer || Wire.available() != bytesThisTransfer) {
      while (Wire.available()) Wire.read();
      return -1;
    }

    for (uint8_t i = 0; i < wordsThisTransfer; ++i) {
      data[wordsDone + i] = (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
    }
    wordsDone += wordsThisTransfer;
  }
  return 0;
}

int MLX90640_I2CWrite(uint8_t address, unsigned int registerAddress, uint16_t value) {
  Wire.beginTransmission(address);
  Wire.write(registerAddress >> 8);
  Wire.write(registerAddress & 0xFF);
  Wire.write(value >> 8);
  Wire.write(value & 0xFF);
  return Wire.endTransmission(true) == 0 ? 0 : -1;
}

void MLX90640_I2CFreqSet(int freqKHz) {
  Wire.setClock(static_cast<uint32_t>(freqKHz) * 1000UL);
}
