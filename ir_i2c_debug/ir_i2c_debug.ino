// ============================================================
// MLX90640 I2C connection test for ESP32 CYD
// IMPORTANT: this module's SDA/SCL silk-screen labels are reversed.
// Wiring: GPIO23 -> module pin LABELLED SCL (actual SDA),
//         GPIO18 -> module pin LABELLED SDA (actual SCL), 5V -> VIN.
// GND -> GND and PS (PS low selects I2C mode). RXD/TXD are not connected.
// ============================================================
#include <TFT_eSPI.h>
#include <Wire.h>

static constexpr uint8_t I2C_SDA = 23;
static constexpr uint8_t I2C_SCL = 18;
// 10 kHz is deliberately slow: it works better with only ESP32 internal pull-ups.
static constexpr uint32_t I2C_CLOCK_HZ = 10000;
static constexpr uint8_t MLX90640_ADDRESS = 0x33; // native 7-bit MLX90640 address

TFT_eSPI tft;
uint8_t devices[16];
uint8_t deviceCount = 0;
uint32_t scanCount = 0;
uint8_t activeSda = I2C_SDA;
uint8_t activeScl = I2C_SCL;

void line(uint16_t y, const String &text, uint16_t colour = TFT_WHITE, uint8_t font = 2) {
  tft.setTextColor(colour, TFT_BLACK);
  tft.drawString(text, 4, y, font);
}

void clearResults() { tft.fillRect(0, 44, 320, 196, TFT_BLACK); }

void selectBus(uint8_t sda, uint8_t scl) {
  Wire.end();
  pinMode(I2C_SDA, INPUT);
  pinMode(I2C_SCL, INPUT);
  Wire.begin(sda, scl);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(30);
  activeSda = sda;
  activeScl = scl;
  delay(20);
}

String pinLevels() {
  pinMode(I2C_SDA, INPUT_PULLUP);
  pinMode(I2C_SCL, INPUT_PULLUP);
  delay(2);
  return "GPIO23=" + String(digitalRead(I2C_SDA)) +
         " GPIO18=" + String(digitalRead(I2C_SCL));
}

bool i2cAck(uint8_t address) {
  Wire.beginTransmission(address);
  return Wire.endTransmission(true) == 0;
}

// MLX90640 registers have 16-bit addresses and big-endian 16-bit data.
bool mlxReadWord(uint16_t reg, uint16_t &value, uint8_t &wireError) {
  Wire.beginTransmission(MLX90640_ADDRESS);
  Wire.write(static_cast<uint8_t>(reg >> 8));
  Wire.write(static_cast<uint8_t>(reg & 0xFF));
  wireError = Wire.endTransmission(false);
  if (wireError != 0) return false;

  uint8_t received = Wire.requestFrom(MLX90640_ADDRESS, static_cast<uint8_t>(2), static_cast<uint8_t>(true));
  if (received != 2 || Wire.available() != 2) {
    wireError = 10; // local incomplete-read error
    while (Wire.available()) Wire.read();
    return false;
  }
  value = (static_cast<uint16_t>(Wire.read()) << 8) | Wire.read();
  wireError = 0;
  return true;
}

void scanAndTest() {
  ++scanCount;
  deviceCount = 0;
  clearResults();
  String levels = pinLevels();
  line(44, "Line idle: " + levels, TFT_CYAN, 1);

  // Test both ESP32 pin assignments. The module's printed SDA/SCL labels are known
  // to be reversed, but this also catches a CYD connector pin-order mistake.
  selectBus(I2C_SDA, I2C_SCL);
  for (uint8_t address = 1; address < 127; ++address) {
    if (i2cAck(address) && deviceCount < sizeof(devices)) devices[deviceCount++] = address;
  }
  bool normalFound = deviceCount > 0;

  if (!normalFound) {
    selectBus(I2C_SCL, I2C_SDA);
    for (uint8_t address = 1; address < 127; ++address) {
      if (i2cAck(address) && deviceCount < sizeof(devices)) devices[deviceCount++] = address;
    }
  }

  clearResults();
  line(44, "I2C scan #" + String(scanCount) + (normalFound ? " normal" : " swapped"), TFT_CYAN);
  line(58, levels, TFT_WHITE, 1);
  if (deviceCount == 0) {
    line(76, "NO I2C DEVICE FOUND", TFT_RED);
    line(100, "Both pin orders were tested", TFT_YELLOW, 1);
    line(114, "PS->GND; VIN must be 5V", TFT_YELLOW, 1);
    line(128, "Idle levels should both be 1", TFT_YELLOW, 1);
    Serial.println("No I2C device found. " + levels);
    return;
  }

  String found = "Found:";
  for (uint8_t i = 0; i < deviceCount; ++i) found += " 0x" + String(devices[i], HEX);
  line(70, found, TFT_GREEN);
  Serial.println(found);

  if (!i2cAck(MLX90640_ADDRESS)) {
    line(98, "MLX90640 address 0x33 NOT found", TFT_RED);
    line(122, "A device ACKed, but not MLX90640", TFT_YELLOW, 1);
    return;
  }

  line(98, "MLX90640 ACK at 0x33", TFT_GREEN);
  uint16_t eeWord, controlWord;
  uint8_t error;
  if (mlxReadWord(0x2400, eeWord, error)) {
    line(124, "EEPROM[2400] = 0x" + String(eeWord, HEX), TFT_GREEN);
    Serial.printf("EEPROM[0x2400] = 0x%04X\n", eeWord);
  } else {
    line(124, "EEPROM read error: " + String(error), TFT_RED);
    Serial.printf("EEPROM read error: %u\n", error);
  }

  if (mlxReadWord(0x800D, controlWord, error)) {
    line(148, "CTRL[800D] = 0x" + String(controlWord, HEX), TFT_GREEN);
    line(172, "I2C LINK WORKING", TFT_GREEN, 4);
    Serial.printf("CTRL[0x800D] = 0x%04X\nI2C LINK WORKING\n", controlWord);
  } else {
    line(148, "CTRL read error: " + String(error), TFT_RED);
    line(172, "ACK OK, READ FAILED", TFT_ORANGE, 2);
    Serial.printf("Control read error: %u\n", error);
  }
}

void setup() {
  Serial.begin(115200);
  delay(300);
  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  line(8, "MLX90640 I2C TEST", TFT_CYAN, 4);
  line(32, "SDA=GPIO23  SCL=GPIO18", TFT_WHITE, 1);

  selectBus(I2C_SDA, I2C_SCL);
  Serial.println("MLX90640 I2C test: pins 23/18, address=0x33");
  scanAndTest();
}

void loop() {
  delay(5000);
  scanAndTest();
}