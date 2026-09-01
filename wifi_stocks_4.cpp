#include <WiFi.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <SPI.h>
#include <XPT2046_Touchscreen.h>
#include <Preferences.h>
#include <time.h>
#include <U8g2_for_TFT_eSPI.h>   // 顯示中文股票名(WenQuanYi 點陣字,簡體 GB2312)

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

// ===== Cyber 配色 =====
#define C_BG      0x0000        // 純黑
#define C_CYAN    0x07FF        // 霓虹青
#define C_MAGENTA 0xF81F        // 洋紅
#define C_GREEN   0x07E0        // 升(亮綠)
#define C_RED     0xF9A6        // 跌(亮紅粉)
#define C_DIM     0x39C7        // 暗灰藍
#define C_DIM2    0x18C3        // 更暗
#define C_YELLOW  0xFFE0        // 黃
#define C_PANEL   0x0841        // 面板底(極深藍)
#define C_LINE    0x2145        // 分隔線


// ---- 內建 RGB LED ----
#define LED_R 4
#define LED_G 16
#define LED_B 17
// ⚠️ CYD RGB 係「共陽」,LOW=著 HIGH=熄(相反!)
void setLED(bool r,bool g,bool b){
  digitalWrite(LED_R, r?LOW:HIGH);
  digitalWrite(LED_G, g?LOW:HIGH);
  digitalWrite(LED_B, b?LOW:HIGH);
}


SPIClass touchSPI = SPIClass(VSPI);
XPT2046_Touchscreen ts(XPT2046_CS, XPT2046_IRQ);
TFT_eSPI tft = TFT_eSPI();
U8g2_for_TFT_eSPI u8f;           // 中文字體 wrapper(接住 tft)
Preferences prefs;

enum Tab { TAB_CRYPTO, TAB_STOCK, TAB_METALS, TAB_SETTING };
Tab curTab = TAB_CRYPTO;
const char* tabNames[] = { "Crypto", "Stock", "Metals" };   // Setup 用齒輪 icon,唔用文字

// ---- WiFi 設定(存 flash)----
String wifiSSID = "";
String wifiPASS = "";
// ---- WiFi 掃描 ----
#define MAX_SCAN 20
String scanSSID[MAX_SCAN];
int    scanRSSI[MAX_SCAN];
bool   scanLock[MAX_SCAN];      // 有無密碼鎖
int    scanCount = 0;
int    scanScroll = 0;
bool   scanActive = false;      // 掃描清單畫面開住?
const int SCAN_ROW_H = 34;
const int SCAN_TOP = 51;
const int SCAN_VISIBLE = 5;     // 一屏顯示 5 個

// ---- 天氣 ----
float weatherTemp=0; int weatherCode=-1; bool weatherOk=false;
uint32_t lastWeather=0;

// ---- Crypto ----
struct Coin { const char* sym; float price; float chg; bool ok; };
Coin coins[] = { {"BTCUSDT",0,0,false}, {"ETHUSDT",0,0,false} };
const int N_COIN = sizeof(coins)/sizeof(coins[0]);
uint32_t lastCrypto=0;

// ---- Stock ----
#define MAX_STOCK 20
struct Stock { String sym; float price; float chg; bool ok; String cname; bool nameOk; };
Stock stocks[MAX_STOCK];
int N_STOCK = 0;
uint32_t lastStock=0;

// ---- 貴金屬(Metals,COMEX 期貨報價)----
struct Metal { const char* sym; const char* nameCN; float price; float chg; bool ok; };
Metal metals[] = {
  { "GC=F", "黄金", 0,0,false },   // 金
  { "SI=F", "白银", 0,0,false },   // 銀
  { "PL=F", "铂金", 0,0,false },   // 鉑金
};
const int N_METAL = sizeof(metals)/sizeof(metals[0]);
uint32_t lastMetal=0;

// ---- Stock scroll / layout ----
int stockScroll = 0;
const int STOCK_TOP     = 51;
const int STOCK_BTN_H   = 28;
const int STOCK_LIST_Y  = STOCK_TOP + STOCK_BTN_H;
const int STOCK_LIST_H  = 189 - STOCK_BTN_H;
const int STOCK_ROW_H   = 50;
const int STOCK_VISIBLE = STOCK_LIST_H / STOCK_ROW_H;

// ---- 觸控鍵盤 ----
// 大楷 layout
const char* KB_UPPER[] = { "QWERTYUIOP", "ASDFGHJKL", "ZXCVBNM", "1234567890", ".-:/_@#$%&" };
// 細楷 layout
const char* KB_LOWER[] = { "qwertyuiop", "asdfghjkl", "zxcvbnm", "1234567890", ".-:/_@#$%&" };
const int KB_NROW = 5;
bool kbShift = true;                 // true=大楷, false=細楷
String kbInput = "";
int  kbMode = 0;
String kbTitle = "";

// 取得目前 layout 某行
String kbRow(int r){
  return String(kbShift ? KB_UPPER[r] : KB_LOWER[r]);
}


// ======================================================
//  Preferences:存 / 讀
// ======================================================
void saveStocks(){
  prefs.begin("cfg", false);
  prefs.putInt("nStk", N_STOCK);
  for(int i=0;i<N_STOCK;i++){
    prefs.putString(("s"+String(i)).c_str(), stocks[i].sym);
    prefs.putString(("n"+String(i)).c_str(), stocks[i].cname);   // 中文名快取
  }
  prefs.end();
}
void loadStocks(){
  prefs.begin("cfg", true);
  int n = prefs.getInt("nStk", -1);
  if(n<=0 || n>MAX_STOCK){          // 冇存過 → 用預設
    prefs.end();
    const char* def[]={"0941.HK","0700.HK","AAPL"};
    N_STOCK=0;
    for(auto s:def){ stocks[N_STOCK].sym=s; stocks[N_STOCK].ok=false; stocks[N_STOCK].cname=""; stocks[N_STOCK].nameOk=false; N_STOCK++; }
    saveStocks();
    return;
  }
  N_STOCK=n;
  for(int i=0;i<N_STOCK;i++){
    stocks[i].sym=prefs.getString(("s"+String(i)).c_str(), "");
    stocks[i].cname=prefs.getString(("n"+String(i)).c_str(), "");
    stocks[i].ok=false;
    stocks[i].nameOk=(stocks[i].cname.length()>0);
  }
  prefs.end();
}
void saveWiFi(){
  prefs.begin("cfg", false);
  prefs.putString("ssid", wifiSSID);
  prefs.putString("pass", wifiPASS);
  prefs.end();
}
void loadWiFi(){
  prefs.begin("cfg", true);
  wifiSSID=prefs.getString("ssid","");
  wifiPASS=prefs.getString("pass","");
  prefs.end();
}

// ======================================================
//  Stock 管理
// ======================================================
bool addStock(String sym){
  sym.trim(); sym.toUpperCase();
  if(sym.length()==0 || N_STOCK>=MAX_STOCK) return false;
  if(sym.endsWith(".HK")){
    int dot=sym.indexOf('.'); String num=sym.substring(0,dot);
    while(num.length()<4) num="0"+num;
    sym=num+".HK";
  }
  for(int i=0;i<N_STOCK;i++) if(stocks[i].sym==sym) return false;
  stocks[N_STOCK].sym=sym; stocks[N_STOCK].ok=false; stocks[N_STOCK].cname=""; stocks[N_STOCK].nameOk=false; N_STOCK++;
  saveStocks();                    // 🔑 加完即存
  return true;
}
void delLastStock(){ if(N_STOCK>0){ N_STOCK--; saveStocks(); } }
// 刪指定第 i 隻
void delStock(int idx){
  if(idx<0 || idx>=N_STOCK) return;
  for(int i=idx;i<N_STOCK-1;i++) stocks[i]=stocks[i+1];   // 後面補上
  N_STOCK--;
  saveStocks();
}

// ======================================================
//  觸控 / HTTPS
// ======================================================
bool getTouchXY(int &sx,int &sy){
  if(!ts.touched()) return false;
  TS_Point p=ts.getPoint();
  if(p.z<300) return false;
  sx=constrain(map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,320),0,319);
  sy=constrain(map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,240),0,239);
  return true;
}
bool httpsGet(const String& url,String& out){
  WiFiClientSecure c; c.setInsecure(); c.setTimeout(12000);
  HTTPClient http; http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);
  if(!http.begin(c,url)) return false;
  http.addHeader("User-Agent","Mozilla/5.0");
  int code=http.GET(); bool ok=(code==200);
  if(ok) out=http.getString();
  http.end(); return ok;
}

// ======================================================
//  天氣 / Crypto / Stock 拎價
// ======================================================
String weatherDesc(int c){
  if(c==0)return"Sunny"; if(c<=3)return"Cloudy"; if(c<=48)return"Fog";
  if(c<=67)return"Rain"; if(c<=77)return"Snow"; if(c<=82)return"Shower";
  if(c<=99)return"Storm"; return"?";
}
void fetchWeather(){
  String body;
  if(!httpsGet("https://api.open-meteo.com/v1/forecast?latitude=22.30&longitude=114.17&current=temperature_2m,weather_code",body)){weatherOk=false;return;}
  StaticJsonDocument<128> f;
  f["current"]["temperature_2m"]=true; f["current"]["weather_code"]=true;
  StaticJsonDocument<256> d;
  if(deserializeJson(d,body,DeserializationOption::Filter(f))){weatherOk=false;return;}
  weatherTemp=d["current"]["temperature_2m"]|0.0f;
  weatherCode=d["current"]["weather_code"]|-1;
  weatherOk=true;
}
void fetchCrypto(){
  for(int i=0;i<N_COIN;i++){
    String body;
    String url=String("https://data-api.binance.vision/api/v3/ticker/24hr?symbol=")+coins[i].sym;
    if(!httpsGet(url,body)){coins[i].ok=false;continue;}
    StaticJsonDocument<96> f; f["lastPrice"]=true; f["priceChangePercent"]=true;
    StaticJsonDocument<192> d;
    if(deserializeJson(d,body,DeserializationOption::Filter(f))){coins[i].ok=false;continue;}
    coins[i].price=d["lastPrice"].as<float>();
    coins[i].chg=d["priceChangePercent"].as<float>();
    coins[i].ok=true;
  }
    // 用第一隻幣(BTC)控制燈
  if(coins[0].ok){
    Serial.printf("[LED] BTC chg=%.3f -> %s\n", coins[0].chg, (coins[0].chg>=0)?"GREEN":"RED");
    if(coins[0].chg >= 0) setLED(0,1,0);   // 升 → 綠
    else                  setLED(1,0,0);   // 跌 → 紅
  }else{
    Serial.println("[LED] BTC fetch failed, LED unchanged");
  }

}
// 去除 Yahoo 名稱尾隨嘅股份類別後綴,例如「阿里巴巴－Ｗ」→「阿里巴巴」
String stripShareSuffix(String s){
  int idx = s.indexOf("\xEF\xBC\x8D");     // 全形破折號 "－"(EF BC 8D)
  if(idx>0) s = s.substring(0, idx);
  idx = s.indexOf('-');                    // 半形 "-"(少見)
  if(idx>0) s = s.substring(0, idx);
  s.trim();
  return s;
}

// 攞 Yahoo 中文(簡體)公司名,cache 落 stocks[i].cname(只適用 .HK 股票)
void fetchStockNameCN(int i){
  if(!stocks[i].sym.endsWith(".HK")) return;
  String body;
  String url = "https://query1.finance.yahoo.com/v1/finance/search?q="+stocks[i].sym+
               "&lang=zh-Hans-CN&region=CN&quotesCount=1&newsCount=0";
  if(!httpsGet(url,body)) return;
  StaticJsonDocument<96> f;
  f["quotes"][0]["longname"]=true;
  DynamicJsonDocument d(512);
  if(deserializeJson(d,body,DeserializationOption::Filter(f))) return;
  const char* name = d["quotes"][0]["longname"];
  if(!name || !name[0]) return;
  String cname = stripShareSuffix(String(name));
  if(cname.length()==0) return;
  stocks[i].cname = cname;
  stocks[i].nameOk = true;
  saveStocks();          // cache 落 flash,下次唔用再攞
}
void fetchOneStock(int i){
  String body;
  String url="https://query1.finance.yahoo.com/v8/finance/chart/"+stocks[i].sym;
  if(!httpsGet(url,body)){stocks[i].ok=false;return;}
  StaticJsonDocument<256> f;
  f["chart"]["result"][0]["meta"]["regularMarketPrice"]=true;
  f["chart"]["result"][0]["meta"]["chartPreviousClose"]=true;
  f["chart"]["result"][0]["meta"]["previousClose"]=true;
  DynamicJsonDocument d(1024);
  if(deserializeJson(d,body,DeserializationOption::Filter(f))){stocks[i].ok=false;return;}
  JsonObject m=d["chart"]["result"][0]["meta"];
  if(m.isNull()){stocks[i].ok=false;return;}
  float p=m["regularMarketPrice"]|0.0f;
  float prev=m["chartPreviousClose"]|(m["previousClose"]|0.0f);
  if(p<=0){stocks[i].ok=false;return;}
  stocks[i].price=p;
  stocks[i].chg=(prev>0)?(p-prev)/prev*100.0f:0;
  stocks[i].ok=true;
  if(!stocks[i].nameOk) fetchStockNameCN(i);    // 冇攞過中文名先攞
}
void fetchStocks(){ for(int i=0;i<N_STOCK;i++){fetchOneStock(i);delay(100);} }

// ================= 貴金屬 (Yahoo COMEX 期貨) =================
void fetchOneMetal(int i){
  String body;
  String url="https://query1.finance.yahoo.com/v8/finance/chart/"+String(metals[i].sym);
  if(!httpsGet(url,body)){metals[i].ok=false;return;}
  StaticJsonDocument<256> f;
  f["chart"]["result"][0]["meta"]["regularMarketPrice"]=true;
  f["chart"]["result"][0]["meta"]["chartPreviousClose"]=true;
  f["chart"]["result"][0]["meta"]["previousClose"]=true;
  DynamicJsonDocument d(1024);
  if(deserializeJson(d,body,DeserializationOption::Filter(f))){metals[i].ok=false;return;}
  JsonObject m=d["chart"]["result"][0]["meta"];
  if(m.isNull()){metals[i].ok=false;return;}
  float p=m["regularMarketPrice"]|0.0f;
  float prev=m["chartPreviousClose"]|(m["previousClose"]|0.0f);
  if(p<=0){metals[i].ok=false;return;}
  metals[i].price=p;
  metals[i].chg=(prev>0)?(p-prev)/prev*100.0f:0;
  metals[i].ok=true;
}
void fetchMetals(){ for(int i=0;i<N_METAL;i++){fetchOneMetal(i);delay(100);} }

String nowHK(){
  struct tm t;
  if(!getLocalTime(&t)) return "--:--";
  char b[8]; strftime(b,sizeof(b),"%H:%M",&t); return String(b);
}

// ===== 手繪天氣 icon (14x14 區域, 中心 cx,cy) =====
void drawWeatherIcon(int cx, int cy, int code){
  uint16_t cc = 0xCE79;                        // 光灰色
  
  if(code==0){                                   // 晴:太陽
    tft.fillCircle(cx,cy,5,C_YELLOW);
    for(int a=0;a<360;a+=45){
      float r=a*3.14159/180;
      int x1=cx+cos(r)*7, y1=cy+sin(r)*7;
      int x2=cx+cos(r)*9, y2=cy+sin(r)*9;
      tft.drawLine(x1,y1,x2,y2,C_YELLOW);
    }
  }else if(code<=3){                             // 多雲:雲
    tft.fillCircle(cx-3,cy+2,4,cc);
    tft.fillCircle(cx+3,cy+2,4,cc);
    tft.fillCircle(cx,cy-1,5,cc);
    tft.fillRect(cx-6,cy+2,12,4,cc);
  }else if(code<=48){                            // 霧
    for(int i=0;i<4;i++)
      tft.drawFastHLine(cx-7,cy-4+i*3,14,cc);
  }else if(code<=67 || (code>=80&&code<=82)){    // 雨
    tft.fillCircle(cx-3,cy-2,4,cc);
    tft.fillCircle(cx+3,cy-2,4,cc);
    tft.fillCircle(cx,cy-5,5,cc);
    tft.fillRect(cx-6,cy-2,12,3,cc);
    tft.drawLine(cx-4,cy+3,cx-6,cy+7,C_CYAN);    // 雨點
    tft.drawLine(cx,cy+3,cx-2,cy+7,C_CYAN);
    tft.drawLine(cx+4,cy+3,cx+2,cy+7,C_CYAN);
  }else if(code<=77){                            // 雪
    tft.fillCircle(cx,cy-3,5,cc);
    tft.fillRect(cx-5,cy-3,10,3,cc);
    tft.setTextColor(TFT_WHITE,C_BG); tft.setTextDatum(MC_DATUM);
    tft.drawString("*",cx-3,cy+5,1);
    tft.drawString("*",cx+3,cy+5,1);
  }else{                                         // 雷暴
    tft.fillCircle(cx-3,cy-3,4,cc);
    tft.fillCircle(cx+3,cy-3,4,cc);
    tft.fillRect(cx-6,cy-3,12,3,cc);
    tft.drawLine(cx,cy,cx-3,cy+5,C_YELLOW);      // 閃電
    tft.drawLine(cx-3,cy+5,cx+1,cy+5,C_YELLOW);
    tft.drawLine(cx+1,cy+5,cx-2,cy+9,C_YELLOW);
  }
}

// ===== 升/跌三角 icon =====
void drawTriangle(int cx,int cy,bool up,uint16_t col){
  if(up) tft.fillTriangle(cx,cy-6, cx-6,cy+5, cx+6,cy+5, col);
  else   tft.fillTriangle(cx,cy+6, cx-6,cy-5, cx+6,cy-5, col);
}

// ===== 垃圾桶 icon (中心 cx,cy) =====
void drawTrash(int cx,int cy,uint16_t col){
  // 桶身
  tft.drawRect(cx-5,cy-3,10,11,col);
  // 桶蓋
  tft.drawFastHLine(cx-7,cy-4,14,col);
  // 手柄
  tft.drawRect(cx-2,cy-6,4,2,col);
  // 直紋
  tft.drawFastVLine(cx-2,cy-1,7,col);
  tft.drawFastVLine(cx+2,cy-1,7,col);
}

// ======================================================
//  頂 bar / tab
// ======================================================
void drawTopBar(){
  tft.fillRect(0,0,320,22,C_PANEL);
  tft.drawFastHLine(0,22,320,C_CYAN);            // 底部青線

  // 左上角科技角位
  tft.drawFastHLine(0,0,12,C_CYAN);
  tft.drawFastVLine(0,0,8,C_CYAN);
  Serial.printf("weatherOk=%d code=%d\n", weatherOk, weatherCode);

  // 時間(青色)
  tft.setTextColor(C_CYAN,C_PANEL); tft.setTextDatum(ML_DATUM);
  tft.drawString(nowHK(),8,11,4);

  // 右:天氣 icon + 溫度
  if(weatherOk){
    drawWeatherIcon(255,11,weatherCode);        // 往左移
    tft.setTextColor(C_YELLOW,C_PANEL); tft.setTextDatum(MR_DATUM);
    tft.drawString(String(weatherTemp,0)+"C",314,11,4);
  }else{
    tft.setTextColor(C_DIM,C_PANEL); tft.setTextDatum(MR_DATUM);
    tft.drawString("--",314,11,2);
  }
}

// ===== 齒輪 icon (中心 cx,cy, 半徑 r) =====
void drawGearIcon(int cx,int cy,int r,uint16_t col){
  tft.drawCircle(cx,cy,r-3,col);
  tft.fillCircle(cx,cy,2,col);
  for(int a=0;a<360;a+=45){
    float rad=a*3.14159/180;
    int x1=cx+cos(rad)*(r-3), y1=cy+sin(rad)*(r-3);
    int x2=cx+cos(rad)*r,     y2=cy+sin(rad)*r;
    tft.drawLine(x1,y1,x2,y2,col);
  }
}

void drawTabs(){
  int w=320/4,y=24,h=26;
  for(int i=0;i<4;i++){
    int x=i*w;
    bool sel=(i==curTab);
    tft.fillRect(x+1,y,w-2,h,sel?C_DIM2:C_BG);
    // 發光邊框
    uint16_t bc = sel?C_CYAN:C_DIM;
    tft.drawFastHLine(x+3,y,w-6,bc);
    tft.drawFastHLine(x+3,y+h-1,w-6,bc);
    // 選中:兩側角位
    if(sel){
      tft.drawFastVLine(x+2,y,6,C_CYAN);
      tft.drawFastVLine(x+w-3,y,6,C_CYAN);
      tft.drawFastVLine(x+2,y+h-6,6,C_CYAN);
      tft.drawFastVLine(x+w-3,y+h-6,6,C_CYAN);
    }
    if(i<3){
      tft.setTextColor(sel?C_CYAN:C_DIM, sel?C_DIM2:C_BG);
      tft.setTextDatum(MC_DATUM);
      tft.drawString(tabNames[i],x+w/2,y+h/2,2);
    }else{
      // 最右:Setup 用齒輪 icon 代替文字
      drawGearIcon(x+w/2,y+h/2,8,sel?C_CYAN:C_DIM);
    }
  }
}



// ======================================================
//  Crypto 畫面
// ======================================================
void drawCrypto(){
  tft.fillRect(0,51,320,189,C_BG);
  int y=58, rowH=88;
  for(int i=0;i<N_COIN;i++){
    bool up = coins[i].chg>=0;
    uint16_t col = coins[i].ok?(up?C_GREEN:C_RED):C_DIM;
    String name=String(coins[i].sym); name.replace("USDT","");

    tft.fillRect(0,y,4,64,col);
    tft.fillRoundRect(10,y,300,64,4,C_PANEL);
    tft.drawRoundRect(10,y,300,64,4,C_DIM);

    // 幣名(粗字,左,升高:baseline y+18)
    tft.setFreeFont(&FreeSansBold18pt7b);
    tft.setTextColor(C_CYAN,C_PANEL); tft.setTextDatum(TL_DATUM);
    tft.drawString(name, 22, y+18);

    if(coins[i].ok){
      // 價(粗字,右上,升高:baseline y+18)
      tft.setFreeFont(&FreeSansBold18pt7b);
      tft.setTextColor(col,C_PANEL); tft.setTextDatum(TR_DATUM);
      char pb[20]; dtostrf(coins[i].price,0,2,pb);
      tft.drawString(pb, 288, y+22);

      // 三角:價格右邊
      drawTriangle(300, y+18, up, col);

      // %:價格下面,右對齊(細字,同價格有距離)
      tft.setTextFont(2);
      char cb[16]; snprintf(cb,sizeof(cb),"%s%.2f%%",up?"+":"-",fabs(coins[i].chg));
      tft.setTextColor(col,C_PANEL); tft.setTextDatum(TR_DATUM);
      tft.drawString(cb, 300, y+52, 2);        // 用 font4,大啲清楚
    }else{
      tft.setTextFont(2);
      tft.setTextColor(C_DIM,C_PANEL); tft.setTextDatum(TR_DATUM);
      tft.drawString("-- no data --", 300, y+30, 2);
    }
    tft.setTextFont(2);
    y+=rowH;
  }
}


// ======================================================
//  Stock 畫面
// ======================================================
void drawStock(){
  tft.fillRect(0,STOCK_TOP,320,189,C_BG);

  // 頂部掣
  tft.fillRoundRect(6,STOCK_TOP+2,302,24,3,C_DIM2);
  tft.drawRoundRect(6,STOCK_TOP+2,302,24,3,C_GREEN);
  tft.setTextColor(C_GREEN,C_DIM2); tft.setTextDatum(MC_DATUM);
  tft.drawString("+ ADD STOCK",160,STOCK_TOP+14,2);


  int maxScroll=max(0,N_STOCK-STOCK_VISIBLE);
  if(stockScroll<0) stockScroll=0;
  if(stockScroll>maxScroll) stockScroll=maxScroll;

  for(int v=0;v<STOCK_VISIBLE;v++){
    int i=stockScroll+v; if(i>=N_STOCK) break;
    int y=STOCK_LIST_Y+v*STOCK_ROW_H;
    bool up=stocks[i].chg>=0;
    uint16_t col=stocks[i].ok?(up?C_GREEN:C_RED):C_DIM;

    tft.fillRect(2,y,3,STOCK_ROW_H-6,col);
    tft.fillRoundRect(8,y,298,STOCK_ROW_H-6,3,C_PANEL);

    // 代號 / 中文名(左)
    bool isHK = stocks[i].sym.endsWith(".HK");
    if(isHK && stocks[i].nameOk && stocks[i].cname.length()>0){
      // 中文名(WenQuanYi 點陣字,簡體,16px 大字,字庫最廣涵蓋)
      u8f.setFont(u8g2_font_wqy16_t_gb2312b);
      u8f.setFontMode(1);                    // 透明背景
      u8f.setForegroundColor(C_CYAN);
      u8f.setCursor(16, y+26);               // baseline,配合 16px 字體升高
      u8f.print(stocks[i].cname);
      int nameW = u8f.getUTF8Width(stocks[i].cname.c_str());

      // " - 數字code"(細一半字體,貼住中文名後面)
      String code = stocks[i].sym.substring(0, stocks[i].sym.indexOf('.'));
      tft.setTextFont(1);
      tft.setTextColor(C_DIM,C_PANEL); tft.setTextDatum(TL_DATUM);
      tft.drawString(" - "+code, 16+nameW, y+18);
    }else{
      tft.setFreeFont(&FreeSansBold12pt7b);
      tft.setTextColor(C_CYAN,C_PANEL); tft.setTextDatum(TL_DATUM);
      tft.drawString(stocks[i].sym, 16, y+14);       // baseline y+14(升高)
    }

    if(stocks[i].ok){
      // 價(粗字,右上)
      tft.setFreeFont(&FreeSansBold12pt7b);
      tft.setTextColor(col,C_PANEL); tft.setTextDatum(TR_DATUM);
      char pb[20]; dtostrf(stocks[i].price,0,3,pb);
      tft.drawString(pb, 280, y+16);

      // 三角:價格右邊
      drawTriangle(270, y+10, up, col);

      // %:價格下面,右對齊
      tft.setTextFont(2);
      char cb[16]; snprintf(cb,sizeof(cb),"%s%.2f%%",up?"+":"-",fabs(stocks[i].chg));
      tft.setTextColor(col,C_PANEL); tft.setTextDatum(TR_DATUM);
      tft.drawString(cb, 298, y+36, 2);
    }else{
      tft.setTextFont(2);
      tft.setTextColor(C_DIM,C_PANEL); tft.setTextDatum(TR_DATUM);
      tft.drawString("-- no data --", 298, y+16, 2);
    }
    tft.setTextFont(2);
      // 垃圾桶(最右)
    drawTrash(292, y+(STOCK_ROW_H-6)/2, C_RED);
  }

  // 捲動條
  int barX=314,barW=4;
  tft.fillRect(barX,STOCK_LIST_Y,barW,STOCK_LIST_H,C_DIM2);
  if(N_STOCK>STOCK_VISIBLE){
    int thumbH=STOCK_LIST_H*STOCK_VISIBLE/N_STOCK; if(thumbH<15)thumbH=15;
    int track=STOCK_LIST_H-thumbH;
    int thumbY=STOCK_LIST_Y+(maxScroll>0?track*stockScroll/maxScroll:0);
    tft.fillRect(barX,thumbY,barW,thumbH,C_CYAN);
  }
}


// ================= 掃描 WiFi =================
void doScan(){
  tft.fillRect(0,51,320,189,TFT_BLACK);
  tft.setTextColor(TFT_YELLOW,TFT_BLACK); tft.setTextDatum(MC_DATUM);
  tft.drawString("Scanning WiFi...",160,140,4);

  WiFi.mode(WIFI_STA);
  WiFi.disconnect();
  delay(100);
  int n = WiFi.scanNetworks();

  scanCount = 0;
  for(int i=0;i<n && scanCount<MAX_SCAN;i++){
    String s = WiFi.SSID(i);
    if(s.length()==0) continue;              // 跳過隱藏
    // 去重複(同名只留最強)
    bool dup=false;
    for(int j=0;j<scanCount;j++) if(scanSSID[j]==s){ dup=true; break; }
    if(dup) continue;
    scanSSID[scanCount]=s;
    scanRSSI[scanCount]=WiFi.RSSI(i);
    scanLock[scanCount]=(WiFi.encryptionType(i)!=WIFI_AUTH_OPEN);
    scanCount++;
  }
  WiFi.scanDelete();
  scanScroll=0;
  scanActive=true;
}

// ================= 畫掃描清單 =================
void drawScanList(){
  tft.fillRect(0,SCAN_TOP,320,189,TFT_BLACK);

  // 頂部:標題 + Rescan + Back
  tft.fillRect(6,SCAN_TOP+2,120,22,TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE,TFT_DARKGREEN); tft.setTextDatum(MC_DATUM);
  tft.drawString("Rescan",6+60,SCAN_TOP+13,2);
  tft.fillRect(194,SCAN_TOP+2,120,22,0x2104);
  tft.setTextColor(TFT_WHITE,0x2104);
  tft.drawString("Back",194+60,SCAN_TOP+13,2);

  int listY = SCAN_TOP+28;
  int maxScroll=max(0,scanCount-SCAN_VISIBLE);
  if(scanScroll>maxScroll) scanScroll=maxScroll;
  if(scanScroll<0) scanScroll=0;

  if(scanCount==0){
    tft.setTextColor(TFT_DARKGREY,TFT_BLACK); tft.setTextDatum(MC_DATUM);
    tft.drawString("No WiFi found",160,150,2);
    return;
  }

  for(int v=0;v<SCAN_VISIBLE;v++){
    int i=scanScroll+v; if(i>=scanCount) break;
    int y=listY+v*SCAN_ROW_H;
    tft.drawRect(6,y,300,SCAN_ROW_H-2,0x39C7);
    // SSID
    tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextDatum(ML_DATUM);
    String name=scanSSID[i];
    if(name.length()>20) name=name.substring(0,20);
    tft.drawString(name,12,y+(SCAN_ROW_H-2)/2,2);
    // 鎖 + 訊號
    tft.setTextDatum(MR_DATUM);
    tft.setTextColor(TFT_CYAN,TFT_BLACK);
    String info = (scanLock[i]?"* ":"  ") + String(scanRSSI[i]) + "dBm";
    tft.drawString(info,300,y+(SCAN_ROW_H-2)/2,2);
  }

  // 捲動條
  if(scanCount>SCAN_VISIBLE){
    int barX=312,barW=4, areaY=listY, areaH=SCAN_VISIBLE*SCAN_ROW_H;
    tft.fillRect(barX,areaY,barW,areaH,0x2104);
    int thumbH=areaH*SCAN_VISIBLE/scanCount; if(thumbH<12)thumbH=12;
    int track=areaH-thumbH;
    int thumbY=areaY+(maxScroll>0?track*scanScroll/maxScroll:0);
    tft.fillRect(barX,thumbY,barW,thumbH,TFT_CYAN);
  }
}

// ======================================================
//  貴金屬畫面(黃金 / 白銀 / 鉑金,COMEX 期貨報價)
// ======================================================
void drawMetals(){
  tft.fillRect(0,51,320,189,C_BG);
  int y=58, rowH=63;
  for(int i=0;i<N_METAL;i++){
    bool up = metals[i].chg>=0;
    uint16_t col = metals[i].ok?(up?C_GREEN:C_RED):C_DIM;

    tft.fillRect(0,y,4,rowH-6,col);
    tft.fillRoundRect(10,y,300,rowH-6,4,C_PANEL);
    tft.drawRoundRect(10,y,300,rowH-6,4,C_DIM);

    // 金屬名(簡體中文,WenQuanYi 點陣字,左)
    u8f.setFont(u8g2_font_wqy16_t_gb2312b);
    u8f.setFontMode(1);
    u8f.setForegroundColor(C_CYAN);
    u8f.setCursor(22, y+22);
    u8f.print(metals[i].nameCN);

    if(metals[i].ok){
      // 價(粗字,右上)
      tft.setFreeFont(&FreeSansBold12pt7b);
      tft.setTextColor(col,C_PANEL); tft.setTextDatum(TR_DATUM);
      char pb[20]; dtostrf(metals[i].price,0,2,pb);
      tft.drawString(pb, 288, y+8);

      // 三角:價格右邊
      drawTriangle(300, y+16, up, col);

      // %:價格下面,右對齊
      tft.setTextFont(2);
      char cb[16]; snprintf(cb,sizeof(cb),"%s%.2f%%",up?"+":"-",fabs(metals[i].chg));
      tft.setTextColor(col,C_PANEL); tft.setTextDatum(TR_DATUM);
      tft.drawString(cb, 300, y+38, 2);
    }else{
      tft.setTextFont(2);
      tft.setTextColor(C_DIM,C_PANEL); tft.setTextDatum(TR_DATUM);
      tft.drawString("-- no data --", 300, y+22, 2);
    }
    tft.setTextFont(2);
    y+=rowH;
  }
}

// ======================================================
//  Setup 畫面
// ======================================================
void drawSetup(){
  tft.fillRect(0,51,320,189,TFT_BLACK);
  tft.setTextColor(TFT_CYAN,TFT_BLACK); tft.setTextDatum(TL_DATUM);
  tft.drawString("WiFi Settings",10,58,2);

  // 目前 WiFi 狀態
  tft.setTextColor(TFT_WHITE,TFT_BLACK);
  tft.drawString("SSID: " + (wifiSSID.length()?wifiSSID:String("(none)")), 10, 82, 2);
  tft.drawString("PASS: " + String(wifiPASS.length()?"********":"(none)"), 10, 104, 2);

  bool conn = (WiFi.status()==WL_CONNECTED);
  tft.setTextColor(conn?TFT_GREEN:TFT_RED,TFT_BLACK);
  tft.drawString(conn ? ("Connected  "+WiFi.localIP().toString()) : "Not connected", 10, 126, 2);

  // 掣:Set SSID / Set PASS / Connect
  tft.fillRect(10,150,140,28,TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE,TFT_DARKGREEN); tft.setTextDatum(MC_DATUM);
  tft.drawString("Set SSID",80,164,2);
  tft.fillRect(160,150,150,28,TFT_DARKGREEN);
  tft.drawString("Set Password",235,164,2);
  tft.fillRect(10,184,145,28,TFT_BLUE);
  tft.setTextColor(TFT_WHITE,TFT_BLUE); tft.setTextDatum(MC_DATUM);
  tft.drawString("Save&Connect",10+72,198,2);
  tft.fillRect(165,184,145,28,TFT_PURPLE);
  tft.setTextColor(TFT_WHITE,TFT_PURPLE);
  tft.drawString("Scan WiFi",165+72,198,2);
}


// ======================================================
//  內容分派
// ======================================================
void drawContent(){
  if(curTab==TAB_CRYPTO) drawCrypto();
  else if(curTab==TAB_STOCK) drawStock();
  else if(curTab==TAB_METALS) drawMetals();
  else drawSetup();
}

// ======================================================
//  觸控鍵盤
// ======================================================
void drawKeyboard(){
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_CYAN,TFT_BLACK); tft.setTextDatum(TL_DATUM);
  tft.drawString(kbTitle,6,2,2);

  tft.fillRect(6,18,308,24,0x2104); tft.drawRect(6,18,308,24,TFT_WHITE);
  tft.setTextColor(TFT_GREEN,0x2104); tft.setTextDatum(ML_DATUM);
  tft.drawString(kbInput+"_",12,30,2);

  int startY=44, keyH=26, gap=2;
  for(int r=0;r<KB_NROW;r++){
    String row=kbRow(r); int n=row.length();
    int keyW=(320-gap)/n-gap;
    int rowY=startY+r*(keyH+gap);
    for(int c=0;c<n;c++){
      int kx=gap+c*(keyW+gap);
      tft.fillRect(kx,rowY,keyW,keyH,0x4208); tft.drawRect(kx,rowY,keyW,keyH,TFT_DARKGREY);
      tft.setTextColor(TFT_WHITE,0x4208); tft.setTextDatum(MC_DATUM);
      char ch[2]={row[c],0}; tft.drawString(ch,kx+keyW/2,rowY+keyH/2,2);
    }
  }

  // 功能列:SHIFT | DEL | Cancel | OK
  int fy=startY+KB_NROW*(keyH+gap)+2; int fw=(320-5*gap)/4;
  // SHIFT
  tft.fillRect(gap,fy,fw,keyH,kbShift?TFT_BLUE:0x39C7);
  tft.setTextColor(TFT_WHITE,kbShift?TFT_BLUE:0x39C7); tft.setTextDatum(MC_DATUM);
  tft.drawString(kbShift?"ABC":"abc",gap+fw/2,fy+keyH/2,2);
  // DEL
  tft.fillRect(2*gap+fw,fy,fw,keyH,TFT_MAROON);
  tft.setTextColor(TFT_WHITE,TFT_MAROON);
  tft.drawString("DEL",2*gap+fw+fw/2,fy+keyH/2,2);
  // Cancel
  tft.fillRect(3*gap+2*fw,fy,fw,keyH,0x2104);
  tft.setTextColor(TFT_WHITE,0x2104);
  tft.drawString("Esc",3*gap+2*fw+fw/2,fy+keyH/2,2);
  // OK
  tft.fillRect(4*gap+3*fw,fy,fw,keyH,TFT_DARKGREEN);
  tft.setTextColor(TFT_WHITE,TFT_DARKGREEN);
  tft.drawString("OK",4*gap+3*fw+fw/2,fy+keyH/2,2);
}


// 開鍵盤
void openKeyboard(int mode,String title,String preset=""){
  kbMode=mode; kbTitle=title; kbInput=preset;
  drawKeyboard();
}

// ======================================================
//  連 WiFi
// ======================================================
void connectWiFi(){
  if(wifiSSID.length()==0) return;
  tft.fillScreen(TFT_BLACK);
  tft.setTextColor(TFT_WHITE,TFT_BLACK); tft.setTextDatum(MC_DATUM);
  tft.drawString("Connecting to",160,105,2);
  tft.drawString(wifiSSID,160,130,4);

  WiFi.disconnect(true); delay(200);
  WiFi.mode(WIFI_STA);
  WiFi.begin(wifiSSID.c_str(), wifiPASS.c_str());
  uint32_t t0=millis();
  while(WiFi.status()!=WL_CONNECTED && millis()-t0<15000){ delay(300); Serial.print("."); }

  tft.fillScreen(TFT_BLACK);
  if(WiFi.status()==WL_CONNECTED){
    Serial.println("\nWiFi OK: "+WiFi.localIP().toString());
    configTime(8*3600,0,"pool.ntp.org","time.google.com");
    struct tm t; for(int i=0;i<10&&!getLocalTime(&t);i++) delay(500);
    fetchWeather(); fetchCrypto(); fetchStocks(); fetchMetals();
  }else{
    Serial.println("\nWiFi FAILED");
  }
  drawTopBar(); drawTabs(); drawContent();
}

// 回傳 true = 關閉鍵盤
bool handleKeyboard(int sx,int sy){
  int startY=44, keyH=26, gap=2;

  // 字元區
  for(int r=0;r<KB_NROW;r++){
    int rowY=startY+r*(keyH+gap);
    if(sy>=rowY&&sy<rowY+keyH){
      String row=kbRow(r); int n=row.length();
      int keyW=(320-gap)/n-gap;
      for(int c=0;c<n;c++){
        int kx=gap+c*(keyW+gap);
        if(sx>=kx&&sx<kx+keyW){
          if(kbInput.length()<32) kbInput+=row[c];
          drawKeyboard();
          return false;
        }
      }
    }
  }

  // 功能列
  int fy=startY+KB_NROW*(keyH+gap)+2;
  int fw=(320-5*gap)/4;
  if(sy>=fy && sy<fy+keyH){
    if(sx < 2*gap+fw){                       // SHIFT
      kbShift=!kbShift;
      drawKeyboard();
      return false;
    }else if(sx < 3*gap+2*fw){               // DEL
      if(kbInput.length()>0) kbInput.remove(kbInput.length()-1);
      drawKeyboard();
      return false;
    }else if(sx < 4*gap+3*fw){               // Esc / Cancel
      return true;
    }else{                                   // OK
      if(kbMode==1){
        if(addStock(kbInput)){
          stockScroll=max(0,N_STOCK-STOCK_VISIBLE);
          if(WiFi.status()==WL_CONNECTED) fetchOneStock(N_STOCK-1);
        }
      }else if(kbMode==2){
        wifiSSID=kbInput;
        saveWiFi();
      }else if(kbMode==3){
        wifiPASS=kbInput;
        saveWiFi();
        kbMode=0;              // 先關鍵盤狀態
        connectWiFi();         // 打完密碼即刻連線
        return true;
      }
      return true;            // kbMode 1 / 2 完成後關鍵盤
    }                          // ← 關 OK 嘅 else{  ★ 你之前漏咗呢個
  }                            // ← 關「功能列 if」

  return false;               // 冇㩒到任何嘢
}


// ======================================================
//  setup
// ======================================================
void setup(){
  Serial.begin(115200); delay(300);
  pinMode(LED_R,OUTPUT); pinMode(LED_G,OUTPUT); pinMode(LED_B,OUTPUT);
  setLED(0,0,0);   // 全熄

  // ---- LED 自我測試:開機逐一亮 R -> G -> B,用嚴查硬體/接線 ----
  Serial.println("[LED TEST] RED on");   setLED(1,0,0); delay(600);
  Serial.println("[LED TEST] GREEN on"); setLED(0,1,0); delay(600);
  Serial.println("[LED TEST] BLUE on");  setLED(0,0,1); delay(600);
  setLED(0,0,0);

  pinMode(TFT_BL,OUTPUT); digitalWrite(TFT_BL,HIGH);
  tft.init(); tft.setRotation(1); tft.fillScreen(TFT_BLACK);
  u8f.begin(tft);          // 接駁中文字體 wrapper
    // 開機 cyber 動畫
  tft.fillScreen(C_BG);
  tft.setTextColor(C_CYAN,C_BG); tft.setTextDatum(MC_DATUM);
  tft.drawString("CRYPTO // STOCK",160,100,4);
  tft.setTextColor(C_MAGENTA,C_BG);
  tft.drawString("T E R M I N A L",160,135,2);
  for(int w=0;w<320;w+=8){ tft.drawFastHLine(0,160,w,C_CYAN); delay(4); }
  delay(400);

  touchSPI.begin(XPT2046_CLK,XPT2046_MISO,XPT2046_MOSI,XPT2046_CS);
  ts.begin(touchSPI); ts.setRotation(1);

  loadWiFi();       // 讀返上次 WiFi
  loadStocks();     // 讀返股票清單

  if(wifiSSID.length()>0){
    connectWiFi();
  }else{
    // 冇 WiFi 設定 → 直接開去 Setup tab 叫用戶設定
    curTab=TAB_SETTING;
    tft.fillScreen(TFT_BLACK);
    drawTopBar(); drawTabs(); drawContent();
    tft.setTextColor(TFT_YELLOW,TFT_BLACK); tft.setTextDatum(MC_DATUM);
    tft.drawString("Set WiFi in Setup tab ->",160,44,1);
  }
}

// ======================================================
//  loop
// ======================================================
void loop(){
  // ===== 鍵盤模式 =====
  if(kbMode!=0){
    static bool kbDown=false;
    if(ts.touched()){
      TS_Point p=ts.getPoint();
      if(p.z>=300&&!kbDown){
        kbDown=true;
        int sx=constrain(map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,320),0,319);
        int sy=constrain(map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,240),0,239);
        bool closed=handleKeyboard(sx,sy);
        if(closed){
          int prevMode=kbMode;
          kbMode=0;
          drawTopBar(); drawTabs(); drawContent();
        }
      }
    }else kbDown=false;
    return;
  }
  
  // ===== WiFi 掃描清單模式 =====
  if(scanActive){
    static bool scDown=false;
    static int  scDownX=0,scDownY=0,scScrollAt=0; static bool scMoved=false;
    if(ts.touched()){
      TS_Point p=ts.getPoint();
      if(p.z>=300){
        int sx=constrain(map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,320),0,319);
        int sy=constrain(map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,240),0,239);
        if(!scDown){ scDown=true; scMoved=false; scDownX=sx; scDownY=sy; scScrollAt=scanScroll; }
        else{
          int listY=SCAN_TOP+28;
          if(scDownY>=listY){
            int dy=sy-scDownY;
            if(abs(dy)>8) scMoved=true;
            int rows=-dy/(SCAN_ROW_H/2);
            int maxScroll=max(0,scanCount-SCAN_VISIBLE);
            int ns=constrain(scScrollAt+rows,0,maxScroll);
            if(ns!=scanScroll){ scanScroll=ns; drawScanList(); }
          }
        }
      }
    }else{
      if(scDown){
        scDown=false;
        // Rescan / Back
        if(scDownY>=SCAN_TOP+2 && scDownY<SCAN_TOP+24){
          if(scDownX>=6 && scDownX<126){ doScan(); drawScanList(); return; }
          else if(scDownX>=194 && scDownX<314){ scanActive=false; drawContent(); return; }
        }
        // 揀 WiFi
        else if(!scMoved){
          int listY=SCAN_TOP+28;
          int v=(scDownY-listY)/SCAN_ROW_H;
          int i=scanScroll+v;
          if(v>=0 && v<SCAN_VISIBLE && i>=0 && i<scanCount){
            wifiSSID=scanSSID[i]; saveWiFi();
            scanActive=false;
            openKeyboard(3,"Password for "+scanSSID[i]+":");  // 直接打密碼
          }
        }
      }
    }
    return;   // 掃描清單模式下唔做其他
  }

  // ===== 一般模式 =====
  static bool wasDown=false;
  static int  downX=0,downY=0,scrollAtDown=0;
  static bool moved=false;

  if(ts.touched()){
    TS_Point p=ts.getPoint();
    if(p.z>=300){
      int sx=constrain(map(p.x,TOUCH_X_MIN,TOUCH_X_MAX,0,320),0,319);
      int sy=constrain(map(p.y,TOUCH_Y_MIN,TOUCH_Y_MAX,0,240),0,239);
      if(!wasDown){ wasDown=true; moved=false; downX=sx; downY=sy; scrollAtDown=stockScroll; }
      else{
        if(curTab==TAB_STOCK && downY>=STOCK_LIST_Y){
          int dy=sy-downY;
          if(abs(dy)>8) moved=true;
          int rows=-dy/(STOCK_ROW_H/2);
          int maxScroll=max(0,N_STOCK-STOCK_VISIBLE);
          int ns=constrain(scrollAtDown+rows,0,maxScroll);
          if(ns!=stockScroll){ stockScroll=ns; drawStock(); }
        }
      }
    }
  }else{
    if(wasDown){
      wasDown=false;
      // --- tab 切換 ---
      if(downY>=22 && downY<50){
        int w=320/4; Tab t=(Tab)(downX/w);
        if(t>=0&&t<=3&&t!=curTab){ curTab=t; stockScroll=0; drawTabs(); drawContent(); }
      }
      // --- Stock 頂部 Add 掣 ---
      else if(curTab==TAB_STOCK && !moved && downY>=STOCK_TOP+2 && downY<STOCK_TOP+26){
        if(downX>=6 && downX<308){
          openKeyboard(1,"Enter code (e.g. 0700.HK / TSLA):");
        }
      }
      // --- Stock list 內垃圾桶 ---
      else if(curTab==TAB_STOCK && !moved && downY>=STOCK_LIST_Y){
        int v=(downY-STOCK_LIST_Y)/STOCK_ROW_H;
        int i=stockScroll+v;
        // 垃圾桶區:x 約 280-310
        if(downX>=278 && downX<310 && v>=0 && v<STOCK_VISIBLE && i>=0 && i<N_STOCK){
          delStock(i);
          int maxScroll=max(0,N_STOCK-STOCK_VISIBLE);
          if(stockScroll>maxScroll) stockScroll=maxScroll;
          drawStock();
        }
      }


      // --- Setup 掣 ---
      else if(curTab==TAB_SETTING && !moved){
        if(downY>=150 && downY<178 && downX>=10 && downX<150){
          openKeyboard(2,"Enter WiFi SSID:",wifiSSID);
        }else if(downY>=150 && downY<178 && downX>=160 && downX<310){
          openKeyboard(3,"Enter WiFi Password:",wifiPASS);
        }else if(downY>=184 && downY<212 && downX>=10 && downX<155){
          connectWiFi();                       // Save & Connect
        }else if(downY>=184 && downY<212 && downX>=165 && downX<310){
          doScan(); drawScanList();            // Scan WiFi
        }
      }
    }
  }

  // ===== 定時更新 =====
  static uint32_t lastClock=0;
  if(millis()-lastClock>10000){ lastClock=millis(); drawTopBar(); }

    // 天氣:成功後 10 分鐘;未成功每 30 秒重試
  uint32_t wxInterval = weatherOk ? 600000 : 30000;
  if(WiFi.status()==WL_CONNECTED && (millis()-lastWeather>wxInterval || lastWeather==0)){
    lastWeather=millis();
    fetchWeather();
    drawTopBar();
  }

  if(millis()-lastCrypto>15000 || (lastCrypto==0 && WiFi.status()==WL_CONNECTED)){
    if(WiFi.status()==WL_CONNECTED){ lastCrypto=millis(); fetchCrypto();
      if(curTab==TAB_CRYPTO) drawCrypto(); }
  }
  if(millis()-lastStock>60000 || (lastStock==0 && WiFi.status()==WL_CONNECTED)){
    if(WiFi.status()==WL_CONNECTED){ lastStock=millis(); fetchStocks();
      if(curTab==TAB_STOCK && kbMode==0) drawStock(); }
  }
  if(millis()-lastMetal>60000 || (lastMetal==0 && WiFi.status()==WL_CONNECTED)){
    if(WiFi.status()==WL_CONNECTED){ lastMetal=millis(); fetchMetals();
      if(curTab==TAB_METALS && kbMode==0) drawMetals(); }
  }
}
