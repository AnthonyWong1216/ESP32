#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <time.h>

// ========= 改呢兩行 =========
const char* WIFI_SSID = "Anthony_WIFI7";
const char* WIFI_PASS = "anthonynethome7";
// ============================

// ---- CYD 觸控 ----
#define XPT2046_IRQ  36
#define XPT2046_MOSI 32
#define XPT2046_MISO 39
#define XPT2046_CLK  25
#define XPT2046_CS   33
#define TOUCH_X_MIN  306
#define TOUCH_X_MAX  3697
#define TOUCH_Y_MIN  438
#define TOUCH_Y_MAX  3781

SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();

enum Tab { TAB_CRYPTO, TAB_STOCK, TAB_SETTING };
Tab curTab = TAB_CRYPTO;
const char* tabNames[] = { "Crypto", "Stock", "Setup" };

// ---- 天氣 ----
float weatherTemp = 0; int weatherCode = -1; bool weatherOk = false;
uint32_t lastWeather = 0;

// ---- Crypto ----
struct Coin { const char* sym; float price; float chg; bool ok; };
Coin coins[] = {
  { "BTCUSDT", 0, 0, false },
  { "ETHUSDT", 0, 0, false },
};
const int N_COIN = sizeof(coins) / sizeof(coins[0]);
uint32_t lastCrypto = 0;

// ================= 觸控 =================
bool getTouchXY(int &sx, int &sy) {
  if (!ts.touched()) return false;
  TS_Point p = ts.getPoint();
  if (p.z < 300) return false;
  sx = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320), 0, 319);
  sy = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240), 0, 239);
  return true;
}

// ================= HTTPS GET =================
bool httpsGet(const String& url, String& out) {
  WiFiClientSecure c;
  c.setInsecure();
  c.setTimeout(12000);
  HTTPClient http;
  http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if (!http.begin(c, url)) return false;
  http.addHeader("User-Agent", "ESP32");
  int code = http.GET();
  bool ok = (code == 200);
  if (ok) out = http.getString();
  http.end();
  return ok;
}

// ================= 天氣 =================
String weatherDesc(int c) {
  if (c == 0) return "Sunny";
  if (c <= 3)  return "Cloudy";
  if (c <= 48) return "Fog";
  if (c <= 67) return "Rain";
  if (c <= 77) return "Snow";
  if (c <= 82) return "Shower";
  if (c <= 99) return "Storm";
  return "?";
}
void fetchWeather() {
  String body;
  if (!httpsGet("https://api.open-meteo.com/v1/forecast?latitude=22.30&longitude=114.17&current=temperature_2m,weather_code", body)) { weatherOk = false; return; }
  StaticJsonDocument<128> f;
  f["current"]["temperature_2m"] = true; f["current"]["weather_code"] = true;
  StaticJsonDocument<256> d;
  if (deserializeJson(d, body, DeserializationOption::Filter(f))) { weatherOk = false; return; }
  weatherTemp = d["current"]["temperature_2m"] | 0.0f;
  weatherCode = d["current"]["weather_code"]   | -1;
  weatherOk = true;
}

// ================= Crypto 拎價 =================
void fetchCrypto() {
  for (int i = 0; i < N_COIN; i++) {
    String body;
    String url = String("https://api.binance.com/api/v3/ticker/24hr?symbol=") + coins[i].sym;
    if (!httpsGet(url, body)) { coins[i].ok = false; continue; }
    StaticJsonDocument<96> f;
    f["lastPrice"] = true; f["priceChangePercent"] = true;
    StaticJsonDocument<192> d;
    if (deserializeJson(d, body, DeserializationOption::Filter(f))) { coins[i].ok = false; continue; }
    coins[i].price = d["lastPrice"].as<float>();
    coins[i].chg   = d["priceChangePercent"].as<float>();
    coins[i].ok = true;
    Serial.printf("%s %.2f (%.2f%%)\n", coins[i].sym, coins[i].price, coins[i].chg);
  }
}

// ================= 時間 =================
String nowHK() {
  struct tm t;
  if (!getLocalTime(&t)) return "--:--";
  char buf[8]; strftime(buf, sizeof(buf), "%H:%M", &t);
  return String(buf);
}

// ================= 頂 bar =================
void drawTopBar() {
  tft.fillRect(0, 0, 320, 22, 0x18E3);
  tft.setTextColor(TFT_WHITE, 0x18E3);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(nowHK(), 6, 11, 2);
  tft.setTextDatum(MR_DATUM);
  if (weatherOk) {
    tft.setTextColor(TFT_YELLOW, 0x18E3);
    tft.drawString(String(weatherTemp, 0) + "C " + weatherDesc(weatherCode), 314, 11, 2);
  } else {
    tft.setTextColor(TFT_DARKGREY, 0x18E3);
    tft.drawString("weather --", 314, 11, 2);
  }
}

// ================= Tab 列 =================
void drawTabs() {
  int w = 320 / 3, y = 22, h = 28;
  for (int i = 0; i < 3; i++) {
    uint16_t bg = (i == curTab) ? TFT_BLUE : 0x2104;
    tft.fillRect(i * w, y, w, h, bg);
    tft.drawRect(i * w, y, w, h, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tabNames[i], i * w + w / 2, y + h / 2, 2);
  }
}

// ================= 內容:Crypto =================
void drawCrypto() {
  tft.fillRect(0, 51, 320, 189, TFT_BLACK);
  int y = 60;
  int rowH = 88;
  for (int i = 0; i < N_COIN; i++) {
    uint16_t col = coins[i].ok ? (coins[i].chg >= 0 ? TFT_GREEN : TFT_RED) : TFT_DARKGREY;

    // 幣名(去掉 USDT)
    String name = String(coins[i].sym);
    name.replace("USDT", "");
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(name, 10, y, 4);

    if (coins[i].ok) {
      // 價錢
      tft.setTextColor(col, TFT_BLACK);
      tft.setTextDatum(TR_DATUM);
      char pb[20];
      dtostrf(coins[i].price, 0, 2, pb);
      tft.drawString(pb, 312, y, 4);
      // 升跌 %
      char cb[16];
      snprintf(cb, sizeof(cb), "%c%.2f%%", coins[i].chg >= 0 ? '+' : '-', fabs(coins[i].chg));
      tft.drawString(cb, 312, y + 30, 4);
    } else {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("-- no data --", 312, y + 8, 2);
    }
    y += rowH;
    if (i < N_COIN - 1) tft.drawFastHLine(0, y - 12, 320, 0x2104);
  }
}

// ================= 內容分派 =================
void drawContent() {
  if (curTab == TAB_CRYPTO)      drawCrypto();
  else {
    tft.fillRect(0, 51, 320, 189, TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tabNames[curTab], 160, 120, 4);
    tft.setTextColor(TFT_CYAN, TFT_BLACK);
    tft.drawString("(coming soon)", 160, 155, 2);
  }
}

// ================= setup =================
void setup() {
  Serial.begin(115200);
  delay(300);
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, HIGH);

  tft.init();
  tft.setRotation(1);
  tft.fillScreen(TFT_BLACK);
  touchSPI.begin(XPT2046_CLK, XPT2046_MISO, XPT2046_MOSI, XPT2046_CS);
  ts.begin(touchSPI);
  ts.setRotation(1);

  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting WiFi...", 160, 120, 2);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) { delay(300); Serial.print("."); }

  tft.fillScreen(TFT_BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    struct tm t;
    for (int i = 0; i < 10 && !getLocalTime(&t); i++) delay(500);
    fetchWeather();
    fetchCrypto();
  } else {
    Serial.println("\nWiFi FAILED");
  }

  drawTopBar();
  drawTabs();
  drawContent();
}

// ================= loop =================
void loop() {
  // --- 觸控切 tab ---
  int sx, sy;
  if (getTouchXY(sx, sy)) {
    if (sy >= 22 && sy < 50) {                 // tab 列
      int w = 320 / 3;
      Tab t = (Tab)(sx / w);
      if (t >= 0 && t <= 2 && t != curTab) {
        curTab = t;
        drawTabs();
        drawContent();
      }
    }
    delay(200);
  }

  // --- 每 10 秒更新時間 ---
  static uint32_t lastClock = 0;
  if (millis() - lastClock > 10000) {
    lastClock = millis();
    drawTopBar();
  }

  // --- 每 10 分鐘更新天氣 ---
  if (millis() - lastWeather > 600000 || lastWeather == 0) {
    if (WiFi.status() == WL_CONNECTED) {
      lastWeather = millis();
      fetchWeather();
      drawTopBar();
    }
  }

  // --- 每 15 秒更新 crypto ---
  if (millis() - lastCrypto > 15000 || lastCrypto == 0) {
    if (WiFi.status() == WL_CONNECTED) {
      lastCrypto = millis();
      fetchCrypto();
      if (curTab == TAB_CRYPTO) drawCrypto();   // 只喺 crypto tab 時先重畫
    }
  }
}

