// ============================================================
// clock.ino — 阶段①: ESP32-C3 时钟模式(移植自原厂ESP8266)
// 显示: HH:MM:SS 橙白大数字 + 英文日期
// 资源: 数字字体(JPEG数组 O/W_3660 + W_1830) + TJpg_Decoder
// 库:   TFT_eSPI + TJpg_Decoder (时间用ESP32原生 configTime+localtime_r)
// 引脚(已在 User_Setup.h 配好): SCL=IO3 SDA=IO5 DC=IO2 RST=IO6 BL=IO1 CS=GND
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "number.h"

// ===== 用户配置 =====
// Fill these values locally before uploading. Never commit real credentials.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

#define COL_BG   0x0000
#define COL_TEXT 0xFFFF

TFT_eSPI tft = TFT_eSPI();
Number dig;

// 数字日期(2026.8.15 风格, 替代原厂1.18MB中文字体)

struct tm timeinfo;   // 本地时间(北京时间, configTime设置)

// ---------------- 背光(P-MOS低电平点亮) ----------------
void setBacklight() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
}

// ---------------- TFT输出回调(JPEG解码块推屏) ----------------
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// ---------------- 读取本地时间 ----------------
void refreshTime() {
  getLocalTime(&timeinfo, 0);   // 已同步立即返回; 未同步返回false但填充1970年
}

// ---------------- 数字日期(2026.8.15 居中) ----------------
void drawDate() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d.%d.%d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  int w = tft.textWidth(buf);
  tft.setCursor((240 - w) / 2, 166);
  tft.print(buf);
}

// ---------------- 冒号(静态装饰, setup画一次) ----------------
void drawColon() {
  tft.fillCircle(114, 74, 4, COL_TEXT);   // 上点
  tft.fillCircle(114, 102, 4, COL_TEXT);  // 下点
}

// ---------------- 时钟数字(只刷变化的, 避免闪屏) ----------------
void digitalClockDisplay() {
  static int lastH = -1, lastM = -1, lastS = -1, lastD = -1;
  refreshTime();
  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;
  int s = timeinfo.tm_sec;
  int d = timeinfo.tm_mday;

  if (h != lastH) {
    dig.printfW3660(34, 58, h/10);   // 时(白 36x60)
    dig.printfW3660(70, 58, h%10);
    lastH = h;
  }
  if (m != lastM) {
    dig.printfO3660(122, 58, m/10);  // 分(橙 36x60)
    dig.printfO3660(158, 58, m%10);
    lastM = m;
  }
  if (s != lastS) {
    dig.printfW1830(102, 128, s/10); // 秒(白 18x30)
    dig.printfW1830(120, 128, s%10);
    lastS = s;
  }
  if (d != lastD) {
    drawDate();
    lastD = d;
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== clock (ESP32-C3 时钟模式) ===");

  setBacklight();
  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(COL_BG);

  // TJpg_Decoder 初始化(照搬原厂参数: 1/2缩放 + swapBytes + 回调)
  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  // WiFi
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.drawCentreString("WiFi...", 120, 100, 2);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);  // 降功率, 缓解USB供电跌落
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) Serial.println("WiFi OK: " + WiFi.localIP().toString());
  else                               Serial.println("WiFi FAILED");

  // NTP 同步(时区GMT+8, 无夏令时)
  configTime(8*3600, 0, "ntp.aliyun.com", "ntp6.aliyun.com");
  Serial.println("NTP 同步中...");
  if (getLocalTime(&timeinfo, 10000)) {   // 最多等10秒
    Serial.println("NTP OK");
  } else {
    Serial.println("NTP FAILED (后台继续重试)");
  }

  // 首次显示
  tft.fillScreen(COL_BG);
  drawColon();
  digitalClockDisplay();
}

void loop() {
  digitalClockDisplay();
  delay(200);
}
