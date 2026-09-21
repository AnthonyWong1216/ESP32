// MLX90640 smooth full image, rotated 90 degrees clockwise, for CYD.
// GPIO23 (SDA) -> module pin labelled SCL; GPIO18 (SCL) -> pin labelled SDA.
// CYD 5V -> VIN; CYD GND -> GND; module PS -> GND. RXD/TXD are unused.
#include <Arduino.h>
#include <Wire.h>
#include <TFT_eSPI.h>
#include <math.h>
#include "MLX90640_API.h"
#include "MLX90640_I2C_Driver.h"

static constexpr uint8_t MLX_ADDRESS = 0x33;
static constexpr uint8_t I2C_SDA = 23;
static constexpr uint8_t I2C_SCL = 18;
static constexpr uint32_t I2C_CLOCK_HZ = 100000;
static constexpr float EMISSIVITY = 0.95F;
static constexpr float TA_SHIFT = 8.0F;
static constexpr int SENSOR_W = 32;
static constexpr int SENSOR_H = 24;

// Clockwise 90-degree image is 24 x 32. It fills the screen height,
// leaving a narrow right information panel.
static constexpr int IMAGE_X = 2;
static constexpr int IMAGE_Y = 0;
static constexpr int IMAGE_W = 240;
static constexpr int IMAGE_H = 320;
static constexpr int INFO_X = 244;
static constexpr int INFO_W = 76;

TFT_eSPI tft;
paramsMLX90640 mlx;
static uint16_t eeData[832];
static uint16_t frameData[834];
static float temperatures[768];
static bool sensorReady = false;
static uint32_t goodFrames = 0;

void panelLine(int y, const String &text, uint16_t color = TFT_WHITE, uint8_t font = 2) {
  tft.setTextColor(color, TFT_BLACK);
  tft.drawString(text, INFO_X, y, font);
}

uint16_t heatColor(float value, float low, float high) {
  const float x = constrain((value - low) / (high - low), 0.0F, 1.0F);
  uint8_t r, g, b;
  if (x < 0.25F) { r = 0; g = uint8_t(x * 1020.0F); b = 255; }
  else if (x < 0.50F) { r = 0; g = 255; b = uint8_t((0.50F - x) * 1020.0F); }
  else if (x < 0.75F) { r = uint8_t((x - 0.50F) * 1020.0F); g = 255; b = 0; }
  else { r = 255; g = uint8_t((1.0F - x) * 1020.0F); b = 0; }
  return tft.color565(r, g, b);
}

float sampleBilinear(float sourceX, float sourceY) {
  sourceX = constrain(sourceX, 0.0F, float(SENSOR_W - 1));
  sourceY = constrain(sourceY, 0.0F, float(SENSOR_H - 1));
  const int x0 = int(sourceX), y0 = int(sourceY);
  const int x1 = min(x0 + 1, SENSOR_W - 1), y1 = min(y0 + 1, SENSOR_H - 1);
  const float fx = sourceX - x0, fy = sourceY - y0;
  const float top = temperatures[y0 * SENSOR_W + x0] * (1.0F - fx) + temperatures[y0 * SENSOR_W + x1] * fx;
  const float bottom = temperatures[y1 * SENSOR_W + x0] * (1.0F - fx) + temperatures[y1 * SENSOR_W + x1] * fx;
  return top * (1.0F - fy) + bottom * fy;
}

void showError(const String &message) {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_RED, TFT_BLACK);
  tft.drawCentreString("MLX90640 ERROR", 160, 40, 4);
  tft.setTextColor(TFT_YELLOW, TFT_BLACK);
  tft.drawCentreString(message, 160, 90, 2);
  tft.drawCentreString("PS->GND; check 5V/SDA/SCL", 160, 120, 2);
  Serial.println(message);
}

bool initialiseSensor() {
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawCentreString("MLX90640 THERMAL", 160, 40, 4);
  tft.drawCentreString("Reading calibration...", 160, 90, 2);
  Wire.begin(I2C_SDA, I2C_SCL);
  Wire.setClock(I2C_CLOCK_HZ);
  Wire.setTimeOut(100);
  Wire.beginTransmission(MLX_ADDRESS);
  if (Wire.endTransmission() != 0) { showError("No I2C response at 0x33"); return false; }
  const int dumpStatus = MLX90640_DumpEE(MLX_ADDRESS, eeData);
  if (dumpStatus != 0) { showError("EEPROM read failed: " + String(dumpStatus)); return false; }
  const int parameterStatus = MLX90640_ExtractParameters(eeData, &mlx);
  if (parameterStatus != 0) { showError("Calibration invalid: " + String(parameterStatus)); return false; }
  if (MLX90640_SetRefreshRate(MLX_ADDRESS, 0x03) != 0 || MLX90640_SetChessMode(MLX_ADDRESS) != 0) {
    showError("Sensor configuration failed"); return false;
  }
  tft.fillScreen(TFT_BLACK);
  tft.fillRect(242, 0, 2, 320, TFT_DARKGREY);
  return true;
}

bool acquireTemperatures(float &ambient) {
  for (uint8_t subpage = 0; subpage < 2; ++subpage) {
    const int status = MLX90640_GetFrameData(MLX_ADDRESS, frameData);
    if (status < 0) { Serial.printf("GetFrameData error: %d\n", status); return false; }
    ambient = MLX90640_GetTa(frameData, &mlx);
    MLX90640_CalculateTo(frameData, &mlx, EMISSIVITY, ambient - TA_SHIFT, temperatures);
  }
  return true;
}


void drawThermalImage90CW(float ambient) {
  float minimum = temperatures[0], maximum = temperatures[0];
  for (int i = 1; i < 768; ++i) {
    minimum = min(minimum, temperatures[i]);
    maximum = max(maximum, temperatures[i]);
  }
  const float midpoint = (minimum + maximum) * 0.5F;
  const float range = max(8.0F, maximum - minimum);
  const float paletteLow = midpoint - range * 0.5F;
  const float paletteHigh = midpoint + range * 0.5F;

  // Match the physical mounting: after the 90-degree transform, reverse the
  // sensor X axis to correct the observed top/bottom inversion. Source Y stays
  // reversed so the previously corrected left/right orientation is preserved.
  tft.startWrite();
  for (int y = 0; y < IMAGE_H; ++y) {
    const float sourceX = float(SENSOR_W - 1) -
                          float(y) * float(SENSOR_W - 1) / float(IMAGE_H - 1);
    for (int x = 0; x < IMAGE_W; ++x) {
      const float sourceY = float(SENSOR_H - 1) -
                            float(x) * float(SENSOR_H - 1) / float(IMAGE_W - 1);
      tft.drawPixel(IMAGE_X + x, IMAGE_Y + y,
                    heatColor(sampleBilinear(sourceX, sourceY), paletteLow, paletteHigh));
    }
  }
  tft.endWrite();

  const float centre = temperatures[(SENSOR_H / 2) * SENSOR_W + SENSOR_W / 2];
  tft.fillRect(INFO_X, 0, INFO_W, 320, TFT_BLACK);
  panelLine(8, "90 CW", TFT_CYAN, 2);
  panelLine(33, "F" + String(++goodFrames), TFT_LIGHTGREY, 2);
  panelLine(67, "MAX", TFT_YELLOW, 2);
  panelLine(86, String(maximum, 1), TFT_WHITE, 4);
  panelLine(122, "MIN", TFT_BLUE, 2);
  panelLine(140, String(minimum, 1), TFT_WHITE, 2);
  panelLine(174, "MID", TFT_GREEN, 2);
  panelLine(192, String(centre, 1), TFT_WHITE, 2);
  panelLine(226, "AMB", TFT_ORANGE, 2);
  panelLine(244, String(ambient, 1), TFT_WHITE, 2);
  panelLine(282, String(paletteLow, 0) + "-", TFT_LIGHTGREY, 1);
  panelLine(294, String(paletteHigh, 0) + "C", TFT_LIGHTGREY, 1);
}

void setup() {
  Serial.begin(115200);
  delay(300);
  tft.init();
  tft.setRotation(1);
  sensorReady = initialiseSensor();
}

void loop() {
  if (!sensorReady) { delay(2000); sensorReady = initialiseSensor(); return; }
  float ambient;
  if (acquireTemperatures(ambient)) drawThermalImage90CW(ambient);
  else { sensorReady = false; showError("Frame read failed; retrying..."); }
}
