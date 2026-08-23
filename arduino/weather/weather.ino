// ============================================================
// weather.ino — 阶段②: ESP32-C3 天气模式(移植自原厂ESP8266)
// 显示: 天气图标 + 温度(C) + 湿度(%) + 气压(hPa) + 空气质量(色点)
// 数据源: 中国天气网 d1.weather.com.cn (固定城市代码, dataSK段)
// 资源: 天气图标(JPEG t*.h 60x60) + 温湿度图标(24x24) + TJpg_Decoder
// 库:   TFT_eSPI + TJpg_Decoder + ArduinoJson
// 引脚(已在 User_Setup.h 配好): SCL=IO3 SDA=IO5 DC=IO2 RST=IO6 BL=IO1 CS=GND
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "weathernum.h"
#include "img/humidity.h"

// ===== 用户配置 =====
// Fill these values locally before uploading. Never commit real credentials.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";
// 城市代码(中国天气网9位): 淄博101120301 长沙101250101 北京101010100 株洲101250301
const char* CITY_CODE = "101120301";
// 城市拼音(显示用, 手动配置): 淄博"Zi Bo" 长沙"Chang Sha" 北京"Bei Jing"
const char* CITY_NAME = "Zi Bo";

#define REFRESH_MS 600000UL   // 天气10分钟刷新

#define COL_BG   0x0000
#define COL_TEXT 0xFFFF
#define COL_DIM  0x7BEF

TFT_eSPI tft = TFT_eSPI();
WeatherNum wrat;

struct Weather {
  float temp  = 0;    // 温度 ℃
  int   humi  = 0;    // 湿度 %
  int   press = 0;    // 气压 hPa
  int   aqi   = 0;    // 空气质量
  int   code  = 99;   // 天气代码(图标)
  bool  ok    = false;
};
Weather w;

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

// ---------------- 温度颜色(蓝→青→绿→黄→红) ----------------
uint16_t tempColor(float t) {
  if (t < 0)   return 0x001F;   // 蓝
  if (t < 18)  return 0x07FF;   // 青
  if (t < 24)  return 0x07E0;   // 绿
  if (t < 31)  return 0xFFE0;   // 黄
  return 0xF800;                // 红
}

// ---------------- 空气质量颜色(优绿→良黄→轻橙→中紫→重红) ----------------
uint16_t aqiColor(int aqi) {
  if (aqi <= 50)  return 0x07E0;   // 优 绿
  if (aqi <= 100) return 0xFFE0;   // 良 黄
  if (aqi <= 150) return 0xFD20;   // 轻度 橙
  if (aqi <= 200) return 0xF81F;   // 中度 紫
  return 0xF800;                   // 重度 红
}

// ---------------- 获取天气(解析dataSK段) ----------------
bool fetchWeather() {
  char url[96];
  snprintf(url, sizeof(url),
           "http://d1.weather.com.cn/weather_index/%s.html?_=%ld",
           CITY_CODE, (long)millis());
  Serial.println(url);

  HTTPClient http;
  http.setTimeout(5000);
  http.begin(url);
  http.addHeader("Referer", "http://www.weather.com.cn/");
  http.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X)");
  int code = http.GET();
  Serial.printf("HTTP code: %d\n", code);
  if (code != 200) { http.end(); return false; }

  Serial.printf("响应大小: %d\n", http.getSize());
  String str = http.getString();
  http.end();
  Serial.printf("读入: %d 字节\n", str.length());

  int s = str.indexOf("dataSK =");
  int e = str.indexOf(";var dataZS");
  Serial.printf("dataSK=%d dataZS=%d\n", s, e);
  if (s < 0 || e < 0) return false;
  String jsonSK = str.substring(s + 8, e);
  Serial.println(jsonSK.substring(0, 160));

  StaticJsonDocument<1024> doc;
  DeserializationError err = deserializeJson(doc, jsonSK);
  if (err) {
    Serial.printf("JSON解析失败: %s\n", err.c_str());
    return false;
  }

  w.temp  = doc["temp"].as<float>();       // "29.3"
  w.humi  = atoi(doc["SD"].as<String>().c_str());  // "50%" -> 50
  w.press = doc["qy"].as<int>();           // 气压 "998"
  w.aqi   = doc["aqi"].as<int>();          // "33"
  String wc = doc["weathercode"].as<String>();  // "d01"
  w.code  = atoi(wc.substring(1, 3).c_str());   // -> 1
  w.ok    = true;
  return true;
}

// ---------------- 显示天气 ----------------
void drawWeather() {
  tft.fillScreen(COL_BG);

  if (!w.ok) {
    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(8, 100);
    tft.print("No Weather Data");
    return;
  }

  // 天气图标(60x60 居中)
  wrat.printfweather(90, 12, w.code);

  // 温度(大字+颜色, Font16有°符号: 0x60 -> °)
  char buf[24];
  snprintf(buf, sizeof(buf), "%.1f\140C", w.temp);  // \140=0x60=° => "31.1°C"
  tft.setTextFont(2);    // Font16(16px, 支持°)
  tft.setTextSize(2);    // 32px
  tft.setTextColor(tempColor(w.temp), COL_BG);
  int tw = tft.textWidth(buf);
  tft.setCursor((240 - tw) / 2, 78);
  tft.print(buf);

  // 城市名(拼音, 温度下方居中, glcdfont放大到约色点大小)
  tft.setTextFont(1);   // glcdfont(5x7)
  tft.setTextSize(3);   // 约18px字形, 接近色点16px
  tft.setTextColor(COL_TEXT, COL_BG);
  int cw = tft.textWidth(CITY_NAME);
  tft.setCursor((240 - cw) / 2, 116);
  tft.print(CITY_NAME);

  // 湿度(图标 + %)
  TJpgDec.drawJpg(51, 168, humidity, sizeof(humidity));
  snprintf(buf, sizeof(buf), "%d%%", w.humi);
  tft.setTextFont(2);   // 切回 Font16
  tft.setTextSize(1);
  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setCursor(79, 171);
  tft.print(buf);

  // 气压(hPa)
  snprintf(buf, sizeof(buf), "%dhPa", w.press);
  tft.setCursor(129, 171);
  tft.print(buf);

  // 空气质量(AQI标签 + 色点 + 数值)
  tft.fillCircle(95, 206, 8, aqiColor(w.aqi));
  snprintf(buf, sizeof(buf), "AQI %d", w.aqi);
  tft.setCursor(108, 200);
  tft.print(buf);
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== weather (ESP32-C3 天气模式) ===");

  setBacklight();
  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(COL_BG);

  TJpgDec.setJpgScale(1);
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  tft.setTextColor(COL_TEXT, COL_BG);
  tft.setTextSize(2);
  tft.drawCentreString("WiFi...", 120, 100, 2);
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
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

  // 首次取天气
  if (fetchWeather()) {
    Serial.printf("天气: %.1fC 湿度%d%% 气压%dhPa AQI%d code%d\n",
                  w.temp, w.humi, w.press, w.aqi, w.code);
  } else {
    Serial.println("天气获取失败");
  }

  drawWeather();
}

void loop() {
  static unsigned long lastRefresh = 0;
  if (millis() - lastRefresh >= REFRESH_MS) {
    if (fetchWeather()) {
      drawWeather();
      Serial.printf("刷新: %.1fC 湿度%d%% 气压%dhPa AQI%d\n",
                    w.temp, w.humi, w.press, w.aqi);
    }
    lastRefresh = millis();
  }
  delay(200);
}
