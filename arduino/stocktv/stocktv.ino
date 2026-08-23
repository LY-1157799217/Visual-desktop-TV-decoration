// ============================================================
// stock-tv v0.2 — ESP32-C3 A股/基金多股轮动行情屏
// 数据流: PC daemon(JSON) -> WiFi -> 本固件轮播显示
// 库: TFT_eSPI(硬件SPI) + ArduinoJson 7.x
// 引脚(已在 User_Setup.h 配好): SCL=IO3 SDA=IO5 DC=IO2 RST=IO6 BL=IO1 CS=GND
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>

// ===== 用户配置 =====
// Fill these values locally before uploading. Never commit real credentials.
const char* WIFI_SSID   = "YOUR_WIFI_SSID";
const char* WIFI_PASS   = "YOUR_WIFI_PASSWORD";
const char* DAEMON_BASE = "http://192.168.1.100:8899/quote";

// 自选列表: {代码, 英文标签}（中文名需字库, v0.2 先用英文）
struct Sym { const char* code; const char* label; };
const Sym SYMBOLS[] = {
  {"sh000001", "SSE Index"},     // 上证指数
  {"sh600519", "Moutai"},        // 贵州茅台
  {"sz300308", "Innolight"},     // 中际旭创
  {"f005827",  "E Fund Blue"},   // 易方达蓝筹
};
const int SYMBOL_COUNT = sizeof(SYMBOLS) / sizeof(SYMBOLS[0]);

#define ROTATE_MS     5000UL     // 轮播翻页间隔
#define REFRESH_MS    60000UL    // 数据刷新间隔
#define SPARK_POINTS  48         // 走势图采样点数

// ===== 配色(红涨绿跌, A股习惯) =====
#define COL_BG    0x0000
#define COL_TEXT  0xFFFF
#define COL_DIM   0x7BEF
#define COL_UP    0xF800
#define COL_DOWN  0x07E0
#define COL_FLAT  0xBDF7

TFT_eSPI tft = TFT_eSPI();

struct Quote {
  float price     = 0;
  float change    = 0;
  float changePct = 0;
  float spark[SPARK_POINTS];
  int   sparkLen  = 0;
  bool  ok        = false;
};

Quote quotes[SYMBOL_COUNT];
int  curIdx      = 0;
unsigned long lastRotate  = 0;
unsigned long lastRefresh = 0;

// ---------------- 背光(P-MOS低电平点亮) ----------------
void setBacklight() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
}

// ---------------- 取数 ----------------
bool fetchQuote(const char* symbol, Quote& q) {
  char url[128];
  snprintf(url, sizeof(url), "%s?symbol=%s&points=%d",
           DAEMON_BASE, symbol, SPARK_POINTS);

  HTTPClient http;
  http.setTimeout(2000);   // 2秒超时，避免阻塞过久触发看门狗
  http.begin(url);
  int code = http.GET();
  if (code != 200) { http.end(); return false; }

  // 流式解析，避免中间 String；静态文档避免堆碎片
  StaticJsonDocument<2048> doc;
  DeserializationError err = deserializeJson(doc, http.getStream());
  http.end();
  if (err) return false;
  if (!doc["ok"]) return false;

  q.price     = doc["price"] | 0.0f;
  q.change    = doc["change"] | 0.0f;
  q.changePct = doc["changePct"] | 0.0f;
  q.ok        = true;

  JsonArray arr = doc["spark"].as<JsonArray>();
  q.sparkLen = 0;
  for (JsonVariant v : arr) {
    if (q.sparkLen >= SPARK_POINTS) break;
    q.spark[q.sparkLen++] = v.as<float>();
  }
  return true;
}

// ---------------- 走势图 sparkline ----------------
void drawSpark(const Quote& q, int x, int y, int w, int h, uint16_t col) {
  if (q.sparkLen < 2) return;
  float minV = q.spark[0], maxV = q.spark[0];
  for (int i = 1; i < q.sparkLen; i++) {
    if (q.spark[i] < minV) minV = q.spark[i];
    if (q.spark[i] > maxV) maxV = q.spark[i];
  }
  float range = maxV - minV;
  if (range < 0.001) range = 1;
  for (int i = 1; i < q.sparkLen; i++) {
    int x1 = x + (i - 1) * w / (q.sparkLen - 1);
    int y1 = y + h - (int)((q.spark[i - 1] - minV) / range * h);
    int x2 = x + i * w / (q.sparkLen - 1);
    int y2 = y + h - (int)((q.spark[i] - minV) / range * h);
    tft.drawLine(x1, y1, x2, y2, col);
  }
}

// ---------------- 画单个股票 ----------------
void drawQuote(int idx) {
  const Sym&   s = SYMBOLS[idx];
  const Quote& q = quotes[idx];
  tft.fillScreen(COL_BG);

  if (!q.ok) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(8, 100);
    tft.print(s.label);
    tft.setCursor(8, 130);
    tft.print("No Data");
    return;
  }

  uint16_t col = (q.changePct > 0.01) ? COL_UP :
                 (q.changePct < -0.01) ? COL_DOWN : COL_FLAT;

  // 名称
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(8, 10);
  tft.print(s.label);

  // 价格(大字)
  tft.setTextColor(col, COL_BG);
  tft.setTextSize(4);
  tft.setCursor(8, 45);
  tft.print(q.price, 2);

  // 涨跌幅
  tft.setTextSize(2);
  tft.setCursor(8, 95);
  tft.print(q.changePct, 2);
  tft.print("%");

  // 走势图
  drawSpark(q, 8, 125, 224, 80, col);

  // 底部代码
  tft.setTextColor(COL_DIM, COL_BG);
  tft.setTextSize(1);
  tft.setCursor(8, 215);
  tft.print(s.code);
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== stock-tv v0.2 ===");

  setBacklight();
  Serial.println("背光已点亮");

  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(COL_BG);
  Serial.println("屏幕已初始化");

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.setCursor(8, 100);
  tft.print("WiFi...");

  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // 降低发射功率，减小WiFi功耗峰值(缓解USB供电跌落)
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("WiFi OK: " + WiFi.localIP().toString());
  } else {
    Serial.println("WiFi FAILED");
    tft.fillScreen(COL_BG);
    tft.setTextColor(COL_DOWN, COL_BG);
    tft.setCursor(8, 100);
    tft.print("WiFi FAILED");
  }

  // 首次取数
  Serial.println("取数中...");
  for (int i = 0; i < SYMBOL_COUNT; i++) {
    bool ok = fetchQuote(SYMBOLS[i].code, quotes[i]);
    Serial.printf("  %s %s\n", ok ? "OK  " : "FAIL", SYMBOLS[i].code);
    delay(200);
  }

  lastRotate = lastRefresh = millis();
  drawQuote(0);
}

void loop() {
  unsigned long now = millis();

  if (now - lastRotate >= ROTATE_MS) {
    curIdx = (curIdx + 1) % SYMBOL_COUNT;
    drawQuote(curIdx);
    lastRotate = now;
  }

  // 分片刷新：每 (REFRESH_MS/SYMBOL_COUNT) 只刷一只，避免连续请求阻塞触发看门狗
  // 注意：只更新数据不重绘，避免 drawQuote 的 fillScreen 造成原地闪屏
  if (now - lastRefresh >= REFRESH_MS / SYMBOL_COUNT) {
    static int refreshIdx = 0;
    fetchQuote(SYMBOLS[refreshIdx].code, quotes[refreshIdx]);
    refreshIdx = (refreshIdx + 1) % SYMBOL_COUNT;
    lastRefresh = now;
  }

  delay(20);
}
