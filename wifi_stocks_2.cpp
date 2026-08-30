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

const int STOCK_LIST_Y = 51 + 28;             // 掣下面先開始 list
const int STOCK_LIST_H = 189 - 28;            // = 161
const int STOCK_VISIBLE = STOCK_LIST_H / STOCK_ROW_H;  // 60px → 2 行(有掣後)

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

float weatherTemp = 0; int weatherCode = -1; bool weatherOk = false;
uint32_t lastWeather = 0;

// ---- Crypto ----
struct Coin { const char* sym; float price; float chg; bool ok; };
Coin coins[] = { { "BTCUSDT",0,0,false }, { "ETHUSDT",0,0,false } };
const int N_COIN = sizeof(coins)/sizeof(coins[0]);
uint32_t lastCrypto = 0;

// ---- Stock(可增減)----
#define MAX_STOCK 20
struct Stock { String sym; float price; float chg; bool ok; };
Stock stocks[MAX_STOCK];
int N_STOCK = 0;                    // 而家唔再係 const

// 開機預設清單
void initStocks() {
  const char* def[] = { "NVDA", "SPCX", "9988.HK", "9618.HK", "2800.HK" };
  N_STOCK = 0;
  for (auto s : def) {
    stocks[N_STOCK].sym = s;
    stocks[N_STOCK].ok = false;
    N_STOCK++;
  }
}

// 加一隻
bool addStock(String sym) {
  sym.trim(); sym.toUpperCase();
  if (sym.length() == 0 || N_STOCK >= MAX_STOCK) return false;
  // 港股補 0:941.HK → 0941.HK
  if (sym.endsWith(".HK")) {
    int dot = sym.indexOf('.');
    String num = sym.substring(0, dot);
    while (num.length() < 4) num = "0" + num;
    sym = num + ".HK";
  }
  // 避免重複
  for (int i=0;i<N_STOCK;i++) if (stocks[i].sym==sym) return false;
  stocks[N_STOCK].sym = sym;
  stocks[N_STOCK].ok = false;
  N_STOCK++;
  return true;
}

// 刪最後一隻
void delLastStock() {
  if (N_STOCK > 0) N_STOCK--;
}


// ---- Stock scroll ----
int stockScroll = 0;          // 目前捲到第幾行(0 = 最頂)
const int STOCK_ROW_H = 60;   // 每行高
const int STOCK_AREA_Y = 51;  // 內容區起點 y
const int STOCK_AREA_H = 189; // 內容區高
const int STOCK_VISIBLE = STOCK_AREA_H / STOCK_ROW_H;  // = 3 行

uint32_t lastStock = 0;

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
  http.addHeader("User-Agent", "Mozilla/5.0");   // Yahoo 需要
  int code = http.GET();
  bool ok = (code == 200);
  if (ok) out = http.getString();
  http.end();
  return ok;
}

// ================= 天氣 =================
String weatherDesc(int c) {
  if (c==0) return "Sunny"; if (c<=3) return "Cloudy"; if (c<=48) return "Fog";
  if (c<=67) return "Rain"; if (c<=77) return "Snow"; if (c<=82) return "Shower";
  if (c<=99) return "Storm"; return "?";
}
void fetchWeather() {
  String body;
  if (!httpsGet("https://api.open-meteo.com/v1/forecast?latitude=22.30&longitude=114.17&current=temperature_2m,weather_code", body)) { weatherOk=false; return; }
  StaticJsonDocument<128> f;
  f["current"]["temperature_2m"]=true; f["current"]["weather_code"]=true;
  StaticJsonDocument<256> d;
  if (deserializeJson(d, body, DeserializationOption::Filter(f))) { weatherOk=false; return; }
  weatherTemp = d["current"]["temperature_2m"] | 0.0f;
  weatherCode = d["current"]["weather_code"]   | -1;
  weatherOk = true;
}

// ================= Crypto =================
void fetchCrypto() {
  for (int i=0;i<N_COIN;i++){
    String body;
    String url = String("https://data-api.binance.vision/api/v3/ticker/24hr?symbol=") + coins[i].sym;
    if (!httpsGet(url, body)) { coins[i].ok=false; continue; }
    StaticJsonDocument<96> f; f["lastPrice"]=true; f["priceChangePercent"]=true;
    StaticJsonDocument<192> d;
    if (deserializeJson(d, body, DeserializationOption::Filter(f))) { coins[i].ok=false; continue; }
    coins[i].price = d["lastPrice"].as<float>();
    coins[i].chg   = d["priceChangePercent"].as<float>();
    coins[i].ok=true;
  }
}

// ================= Stock (Yahoo) =================
void fetchOneStock(int i) {
  String body;
  String url = "https://query1.finance.yahoo.com/v8/finance/chart/" + stocks[i].sym;
  if (!httpsGet(url, body)) { stocks[i].ok=false; return; }

  StaticJsonDocument<256> f;
  f["chart"]["result"][0]["meta"]["regularMarketPrice"] = true;
  f["chart"]["result"][0]["meta"]["chartPreviousClose"] = true;
  f["chart"]["result"][0]["meta"]["previousClose"]      = true;

  DynamicJsonDocument d(1024);
  if (deserializeJson(d, body, DeserializationOption::Filter(f))) { stocks[i].ok=false; return; }

  JsonObject m = d["chart"]["result"][0]["meta"];
  if (m.isNull()) { stocks[i].ok=false; return; }

  float p    = m["regularMarketPrice"] | 0.0f;
  float prev = m["chartPreviousClose"] | (m["previousClose"] | 0.0f);
  if (p <= 0) { stocks[i].ok=false; return; }

  stocks[i].price = p;
  stocks[i].chg   = (prev > 0) ? (p - prev) / prev * 100.0f : 0;
  stocks[i].ok = true;
  Serial.printf("%s %.3f (%.2f%%)\n", stocks[i].sym.c_str(), p, stocks[i].chg);
}
void fetchStocks() {
  for (int i=0;i<N_STOCK;i++){ fetchOneStock(i); delay(100); }
}

// ================= 時間 =================
String nowHK() {
  struct tm t;
  if (!getLocalTime(&t)) return "--:--";
  char buf[8]; strftime(buf,sizeof(buf),"%H:%M",&t); return String(buf);
}

// ================= 頂 bar =================
void drawTopBar() {
  tft.fillRect(0,0,320,22,0x18E3);
  tft.setTextColor(TFT_WHITE,0x18E3); tft.setTextDatum(ML_DATUM);
  tft.drawString(nowHK(),6,11,2);
  tft.setTextDatum(MR_DATUM);
  if (weatherOk){ tft.setTextColor(TFT_YELLOW,0x18E3);
    tft.drawString(String(weatherTemp,0)+"C "+weatherDesc(weatherCode),314,11,2);
  } else { tft.setTextColor(TFT_DARKGREY,0x18E3); tft.drawString("weather --",314,11,2); }
}

// ================= Tab 列 =================
void drawTabs() {
  int w=320/3,y=22,h=28;
  for(int i=0;i<3;i++){
    uint16_t bg=(i==curTab)?TFT_BLUE:0x2104;
    tft.fillRect(i*w,y,w,h,bg); tft.drawRect(i*w,y,w,h,TFT_DARKGREY);
    tft.setTextColor(TFT_WHITE,bg); tft.setTextDatum(MC_DATUM);
    tft.drawString(tabNames[i],i*w+w/2,y+h/2,2);
  }
}

// ================= Crypto 畫面 =================
void drawCrypto() {
  tft.fillRect(0,51,320,189,TFT_BLACK);
  int y=60, rowH=88;
  for(int i=0;i<N_COIN;i++){
    uint16_t col=coins[i].ok?(coins[i].chg>=0?TFT_GREEN:TFT_RED):TFT_DARKGREY;
    String name=String(coins[i].sym); name.replace("USDT","");
    tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextDatum(TL_DATUM);
    tft.drawString(name,10,y,4);
    if(coins[i].ok){
      tft.setTextColor(col,TFT_BLACK); tft.setTextDatum(TR_DATUM);
      char pb[20]; dtostrf(coins[i].price,0,2,pb); tft.drawString(pb,312,y,4);
      char cb[16]; snprintf(cb,sizeof(cb),"%c%.2f%%",coins[i].chg>=0?'+':'-',fabs(coins[i].chg));
      tft.drawString(cb,312,y+30,4);
    } else {
      tft.setTextColor(TFT_DARKGREY,TFT_BLACK); tft.setTextDatum(TR_DATUM);
      tft.drawString("-- no data --",312,y+8,2);
    }
    y+=rowH;
    if(i<N_COIN-1) tft.drawFastHLine(0,y-12,320,0x2104);
  }
}

// ================= 觸控鍵盤 =================
// 鍵盤佈局(3 行字母 + 一行數字 + 功能)
const char* KB_ROWS[] = {
  "QWERTYUIOP",
  "ASDFGHJKL",
  "ZXCVBNM.",
  "1234567890"
};
const int KB_NROW = 4;

String kbInput = "";      // 目前輸入
bool   kbActive = false;  // 鍵盤開住?

void drawKeyboard() {
  tft.fillScreen(TFT_BLACK);

  // 標題 + 輸入框
  tft.setTextColor(TFT_CYAN, TFT_BLACK);
  tft.setTextDatum(TL_DATUM);
  tft.drawString("Enter stock code:", 6, 4, 2);

  tft.fillRect(6, 24, 308, 26, 0x2104);
  tft.drawRect(6, 24, 308, 26, TFT_WHITE);
  tft.setTextColor(TFT_GREEN, 0x2104);
  tft.setTextDatum(ML_DATUM);
  tft.drawString(kbInput + "_", 12, 37, 4);

  // 鍵盤:由 y=56 開始
  int startY = 56;
  int keyH = 32, gap = 2;
  for (int r = 0; r < KB_NROW; r++) {
    String row = KB_ROWS[r];
    int n = row.length();
    int keyW = (320 - gap) / n - gap;
    int rowY = startY + r * (keyH + gap);
    for (int c = 0; c < n; c++) {
      int kx = gap + c * (keyW + gap);
      tft.fillRect(kx, rowY, keyW, keyH, 0x4208);
      tft.drawRect(kx, rowY, keyW, keyH, TFT_DARKGREY);
      tft.setTextColor(TFT_WHITE, 0x4208);
      tft.setTextDatum(MC_DATUM);
      char ch[2] = { row[c], 0 };
      tft.drawString(ch, kx + keyW/2, rowY + keyH/2, 2);
    }
  }

  // 底部功能列:DEL | Cancel | OK
  int fy = startY + KB_NROW*(keyH+gap) + 4;
  int fw = (320 - 4*gap) / 3;
  // DEL
  tft.fillRect(gap, fy, fw, keyH, TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_MAROON);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("DEL", gap+fw/2, fy+keyH/2, 2);
  // Cancel
  tft.fillRect(2*gap+fw, fy, fw, keyH, 0x2104);
  tft.setTextColor(TFT_WHITE, 0x2104);
  tft.drawString("Cancel", 2*gap+fw+fw/2, fy+keyH/2, 2);
  // OK
  tft.fillRect(3*gap+2*fw, fy, fw, keyH, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.drawString("OK", 3*gap+2*fw+fw/2, fy+keyH/2, 2);
}

// 處理鍵盤觸控;回傳 true = 鍵盤關閉(要重畫主畫面)
bool handleKeyboard(int sx, int sy) {
  int startY = 56, keyH = 32, gap = 2;

  // 字母/數字區
  for (int r = 0; r < KB_NROW; r++) {
    int rowY = startY + r*(keyH+gap);
    if (sy >= rowY && sy < rowY+keyH) {
      String row = KB_ROWS[r];
      int n = row.length();
      int keyW = (320-gap)/n - gap;
      for (int c = 0; c < n; c++) {
        int kx = gap + c*(keyW+gap);
        if (sx >= kx && sx < kx+keyW) {
          if (kbInput.length() < 10) kbInput += row[c];
          drawKeyboard();
          return false;
        }
      }
    }
  }

  // 功能列
  int fy = startY + KB_NROW*(keyH+gap) + 4;
  if (sy >= fy && sy < fy+keyH) {
    int fw = (320-4*gap)/3;
    if (sx < 2*gap+fw) {                        // DEL
      if (kbInput.length()>0) kbInput.remove(kbInput.length()-1);
      drawKeyboard(); return false;
    } else if (sx < 3*gap+2*fw) {               // Cancel
      kbActive = false; return true;
    } else {                                    // OK
      if (addStock(kbInput)) {
        int i = N_STOCK-1;
        if (WiFi.status()==WL_CONNECTED) fetchOneStock(i);
      }
      kbActive = false; return true;
    }
  }
  return false;
}

// ================= Stock 畫面(可捲動) =================
void drawStock() {
  tft.fillRect(0, STOCK_AREA_Y, 320, STOCK_AREA_H, TFT_BLACK);

  // ---- 頂部兩個掣(在內容區最上面一條)----
  tft.fillRect(6, STOCK_AREA_Y+2, 150, 24, TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE, TFT_DARKGREEN);
  tft.setTextDatum(MC_DATUM);
  tft.drawString("+ Add", 6+75, STOCK_AREA_Y+14, 2);

  tft.fillRect(164, STOCK_AREA_Y+2, 144, 24, TFT_MAROON);
  tft.setTextColor(TFT_WHITE, TFT_MAROON);
  tft.drawString("Del last", 164+72, STOCK_AREA_Y+14, 2);

  // 限制 scroll 範圍
  int maxScroll = N_STOCK - STOCK_VISIBLE;
  if (maxScroll < 0) maxScroll = 0;
  if (stockScroll < 0) stockScroll = 0;
  if (stockScroll > maxScroll) stockScroll = maxScroll;

  // 畫可見嘅行
  for (int v = 0; v < STOCK_VISIBLE; v++) {
    int i = stockScroll + v;
    if (i >= N_STOCK) break;

    int y = STOCK_AREA_Y + 5 + v * STOCK_ROW_H;
    uint16_t col = stocks[i].ok ? (stocks[i].chg>=0?TFT_GREEN:TFT_RED) : TFT_DARKGREY;

    // 代號
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextDatum(TL_DATUM);
    tft.drawString(stocks[i].sym, 10, y+8, 4);

    if (stocks[i].ok) {
      tft.setTextColor(col, TFT_BLACK);
      tft.setTextDatum(TR_DATUM);
      char pb[20]; dtostrf(stocks[i].price, 0, 3, pb);
      tft.drawString(pb, 300, y, 4);
      char cb[16]; snprintf(cb,sizeof(cb),"%c%.2f%%",stocks[i].chg>=0?'+':'-',fabs(stocks[i].chg));
      tft.drawString(cb, 300, y+28, 2);
    } else {
      tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
      tft.setTextDatum(TR_DATUM);
      tft.drawString("-- no data --", 300, y+10, 2);
    }

    if (v < STOCK_VISIBLE-1 && i < N_STOCK-1)
      tft.drawFastHLine(0, y + STOCK_ROW_H - 8, 310, 0x2104);
  }

  // ---- 右邊捲動條 ----
  int barX = 314, barW = 5;
  tft.fillRect(barX, STOCK_AREA_Y, barW, STOCK_AREA_H, 0x2104);   // 底槽
  if (N_STOCK > STOCK_VISIBLE) {
    int thumbH = STOCK_AREA_H * STOCK_VISIBLE / N_STOCK;
    if (thumbH < 15) thumbH = 15;
    int track = STOCK_AREA_H - thumbH;
    int thumbY = STOCK_AREA_Y + (maxScroll>0 ? track * stockScroll / maxScroll : 0);
    tft.fillRect(barX, thumbY, barW, thumbH, TFT_CYAN);           // 滑塊
  }

  // 頂/底可捲動箭頭提示
  tft.setTextColor(TFT_DARKGREY, TFT_BLACK);
  tft.setTextDatum(TC_DATUM);
  if (stockScroll > 0)          tft.drawString("^", 155, STOCK_AREA_Y+1, 1);
  if (stockScroll < maxScroll)  tft.drawString("v", 155, 232, 1);
}


// ================= 內容分派 =================
void drawContent() {
  if (curTab == TAB_CRYPTO)      drawCrypto();
  else if (curTab == TAB_STOCK)  drawStock();
  else {
    tft.fillRect(0,51,320,189,TFT_BLACK);
    tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextDatum(MC_DATUM);
    tft.drawString("Setup",160,120,4);
    tft.setTextColor(TFT_CYAN,TFT_BLACK);
    tft.drawString("(coming soon)",160,155,2);
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
  while (WiFi.status()!=WL_CONNECTED && millis()-t0<15000){ delay(300); Serial.print("."); }

  tft.fillScreen(TFT_BLACK);
  if (WiFi.status()==WL_CONNECTED){
    Serial.println("\nWiFi OK: " + WiFi.localIP().toString());
    configTime(8*3600, 0, "pool.ntp.org", "time.google.com");
    struct tm t;
    for(int i=0;i<10 && !getLocalTime(&t);i++) delay(500);
    fetchWeather();
    fetchCrypto();
    fetchStocks();
  } else {
    Serial.println("\nWiFi FAILED");
  }

  drawTopBar();
  drawTabs();
  drawContent();
}

// ================= loop =================
void loop() {
  // ---- 觸控處理:分「tab 切換」同「內容捲動」----
  static bool wasDown = false;
  static int  downX = 0, downY = 0;
  static int  scrollAtDown = 0;

    // ===== 若鍵盤開住,只處理鍵盤 =====
  if (kbActive) {
    static bool kbDown = false;
    if (ts.touched()) {
      TS_Point p = ts.getPoint();
      if (p.z >= 300 && !kbDown) {
        kbDown = true;
        int sx = constrain(map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,320),0,319);
        int sy = constrain(map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,240),0,239);
        bool closed = handleKeyboard(sx, sy);
        if (closed) { drawTopBar(); drawTabs(); drawStock(); }
      }
    } else kbDown = false;
    return;   // 鍵盤模式下唔做其他嘢
  }

  if (ts.touched()) {
    TS_Point p = ts.getPoint();
    if (p.z >= 300) {
      int sx = constrain(map(p.x, TOUCH_X_MIN, TOUCH_X_MAX, 0, 320), 0, 319);
      int sy = constrain(map(p.y, TOUCH_Y_MIN, TOUCH_Y_MAX, 0, 240), 0, 239);

      if (!wasDown) {                    // 剛㩒落
        wasDown = true;
        downX = sx; downY = sy;
        scrollAtDown = stockScroll;
      } else {                           // 拖動中
        if (curTab == TAB_STOCK && downY >= STOCK_AREA_Y) {
          int dy = sy - downY;                     // 手指移動
          int rows = -dy / (STOCK_ROW_H/2);        // 半行=1 格,好郁啲
          int ns = scrollAtDown + rows;
          int maxScroll = max(0, N_STOCK - STOCK_VISIBLE);
          ns = constrain(ns, 0, maxScroll);
          if (ns != stockScroll) { stockScroll = ns; drawStock(); }
        }
      }
    }
  } else {
    if (wasDown) {
      // 放手:若係細移動(似㩒一下)且喺 tab 區 → 切 tab
      wasDown = false;
      if (abs(downY - downY) < 10) {}   // placeholder
      if (downY >= 22 && downY < 50) {  // tab 列
        int w = 320/3;
        Tab t = (Tab)(downX / w);
        if (t>=0 && t<=2 && t!=curTab) { curTab=t; drawTabs(); drawContent(); }
      }
    }
  }

  // 每 10 秒時間
  static uint32_t lastClock=0;
  if (millis()-lastClock>10000){ lastClock=millis(); drawTopBar(); }

  // 每 10 分鐘天氣
  if (millis()-lastWeather>600000 || lastWeather==0){
    if (WiFi.status()==WL_CONNECTED){ lastWeather=millis(); fetchWeather(); drawTopBar(); }
  }

  // 每 15 秒 crypto
  if (millis()-lastCrypto>15000 || lastCrypto==0){
    if (WiFi.status()==WL_CONNECTED){ lastCrypto=millis(); fetchCrypto();
      if (curTab==TAB_CRYPTO) drawCrypto(); }
  }

  // 每 60 秒 stock
  if (millis()-lastStock>60000 || lastStock==0){
    if (WiFi.status()==WL_CONNECTED){ lastStock=millis(); fetchStocks();
      if (curTab==TAB_STOCK) drawStock(); }
  }
}


