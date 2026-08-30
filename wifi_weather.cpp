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

// ---- 天氣資料 ----
float weatherTemp = 0;
int   weatherCode = -1;
bool  weatherOk = false;
uint32_t lastWeather = 0;

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
  String url = "https://api.open-meteo.com/v1/forecast?latitude=22.30&longitude=114.17&current=temperature_2m,weather_code";
  if (!httpsGet(url, body)) { weatherOk = false; Serial.println("weather fail"); return; }

  StaticJsonDocument<128> filter;
  filter["current"]["temperature_2m"] = true;
  filter["current"]["weather_code"]   = true;
  StaticJsonDocument<256> doc;
  if (deserializeJson(doc, body, DeserializationOption::Filter(filter))) { weatherOk = false; return; }

  weatherTemp = doc["current"]["temperature_2m"] | 0.0f;
  weatherCode = doc["current"]["weather_code"]   | -1;
  weatherOk = true;
  Serial.printf("Weather %.1fC code=%d %s\n", weatherTemp, weatherCode, weatherDesc(weatherCode).c_str());
}

// ================= 時間 =================
String nowHK() {
  struct tm t;
  if (!getLocalTime(&t)) return "--:--";
  char buf[8];
  strftime(buf, sizeof(buf), "%H:%M", &t);
  return String(buf);
}

// ================= 頂部 bar(時間 + 天氣) =================
void drawTopBar() {
  tft.fillRect(0, 0, 320, 22, 0x18E3);         // 深藍灰
  // 左:時間
  tft.setTextColor(TFT_WHITE, 0x18E3);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(nowHK(), 6, 11, 2);
  // 右:天氣
  tft.setTextDatum(MR_DATUM);
  if (weatherOk) {
    tft.setTextColor(TFT_YELLOW, 0x18E3);
    String w = String(weatherTemp, 0) + "C " + weatherDesc(weatherCode);
    tft.drawString(w, 314, 11, 2);
  } else {
    tft.setTextColor(TFT_DARKGREY, 0x18E3);
    tft.drawString("weather --", 314, 11, 2);
  }
}

// ================= Tab 列 =================
void drawTabs() {
  int w = 320 / 3;
  int y = 22, h = 28;
  for (int i = 0; i < 3; i++) {
    uint16_t bg = (i == curTab) ? TFT_BLUE : 0x2104;
    tft.fillRect(i * w, y, w, h, bg);
    tft.drawRect(i * w, y, w, h, TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE, bg);
    tft.setTextDatum(MC_DATUM);
    tft.drawString(tabNames[i], i * w + w / 2, y + h / 2, 2);
  }
}

// ================= 內容區 =================
void drawContent() {
  tft.fillRect(0, 51, 320, 189, TFT_BLACK);
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString(tabNames[curTab], 160, 120, 4);
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.drawString("(coming soon)", 160, 155, 2);
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

  // 連 WiFi
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting WiFi...", 160, 120, 2);
  WiFi.mode(WIFI_STA);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  uint32_t t0 = millis();
  while (WiFi.status() != WL_CONNECTED && millis() - t0 < 15000) {
    delay(300); Serial.print(".");
  }

  tft.fillScreen(TFT_BLACK);
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    // 對時(香港 UTC+8)
    configTime(8 * 3600, 0, "pool.ntp.org", "time.google.com");
    // 等 NTP
    struct tm t;
    for (int i = 0; i < 10 && !getLocalTime(&t); i++) delay(500);
    fetchWeather();
  } else {
    Serial.println("\nWiFi FAILED");
  }

  drawTopBar();
  drawTabs();
  drawContent();
}

// ================= loop =================
void loop() {
  // 觸控切 tab
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

  // 每分鐘更新時間
  static uint32_t lastClock = 0;
  if (millis() - lastClock > 10000) {
    lastClock = millis();
    drawTopBar();
  }

  // 每 10 分鐘更新天氣
  if (millis() - lastWeather > 600000 || lastWeather == 0) {
    if (WiFi.status() == WL_CONNECTED) {
      lastWeather = millis();
      fetchWeather();
      drawTopBar();
    }
  }
}
