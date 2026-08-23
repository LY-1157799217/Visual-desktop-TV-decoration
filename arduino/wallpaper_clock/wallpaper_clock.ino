// ============================================================
// wallpaper_clock.ino — 静态壁纸(/1.jpg) + 时钟叠加(透明数字·复用日期逻辑)
// 数字: alpha 位图(背景 alpha=0 透明, 笔画纯色+边缘抗锯齿), 与日期同款透明叠加
// 库:   TFT_eSPI + TJpg_Decoder(仅壁纸) + (时间ESP32原生 configTime+getLocalTime)
// 引脚(已在 User_Setup.h 配好): SCL=IO3 SDA=IO5 DC=IO2 RST=IO6 BL=IO1 CS=GND
// ============================================================
#include <Arduino.h>
#include <WiFi.h>
#include <time.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include "number_alpha.h"

// ===== 用户配置 =====
// Fill these values locally before uploading. Never commit real credentials.
const char* WIFI_SSID = "YOUR_WIFI_SSID";
const char* WIFI_PASS = "YOUR_WIFI_PASSWORD";

#define COL_BG      0x0000
#define COL_TEXT    0xFFFF
#define COL_ORANGE  0xFD20   // 纯橙(255,165,0)
#define TRANSPARENT 0x0000
#define WALLPAPER   "/2.jpg"  // 壁纸(奶龙图片2, 验证换壁纸后数字效果是否保持)

TFT_eSPI tft = TFT_eSPI();
TFT_eSprite wallpaperSpr(&tft);  // 壁纸缓存 240x240
TFT_eSprite numSpr(&tft);        // 大数字 sprite 36x60(时分)
TFT_eSprite numSprSmall(&tft);   // 小数字 sprite 18x30(秒)
TFT_eSprite dateSpr(&tft);       // 日期 sprite
struct tm timeinfo;

TFT_eSprite* currentSpr = &numSpr;  // 当前数字渲染目标 sprite

// ---------------- 背光(P-MOS低电平点亮) ----------------
void setBacklight() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
}

// ---------------- 壁纸回调(解码到 wallpaperSpr) ----------------
bool tft_output_wallpaper(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  wallpaperSpr.pushImage(x, y, w, h, bitmap);
  return 1;
}

// ---------------- 读取本地时间 ----------------
void refreshTime() {
  getLocalTime(&timeinfo, 0);
}

// ---------------- 显示单个数字(alpha 位图: 透明背景, 纯色笔画+抗锯齿) ----------------
void showDigit(int dx, int dy, const uint8_t* alphaMap, uint16_t pure, bool small) {
  currentSpr = small ? &numSprSmall : &numSpr;
  int w = currentSpr->width();
  int h = currentSpr->height();
  uint16_t pureSwapped = (pure >> 8) | (pure << 8);  // 匹配 sprite _img 的字节序(交换后)
  uint16_t* dp = (uint16_t*)currentSpr->getPointer();
  uint16_t* wp = (uint16_t*)wallpaperSpr.getPointer();
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t a = pgm_read_byte(&alphaMap[y * w + x]);
      uint16_t bg = wp[(dy + y) * 240 + (dx + x)];
      if (a == 0)        dp[y * w + x] = bg;                              // 透明 -> 露壁纸
      else if (a == 255) dp[y * w + x] = pureSwapped;                     // 不透明笔画
      else               dp[y * w + x] = tft.alphaBlend(a, pureSwapped, bg); // 抗锯齿边缘
    }
  }
  currentSpr->pushSprite(dx, dy);
}

// ---------------- 数字数组(alpha 位图指针) ----------------
static const uint8_t* const A_O3660[10] = {A_O_3660_i0, A_O_3660_i1, A_O_3660_i2, A_O_3660_i3, A_O_3660_i4, A_O_3660_i5, A_O_3660_i6, A_O_3660_i7, A_O_3660_i8, A_O_3660_i9};
static const uint8_t* const A_W3660[10] = {A_W_3660_i0, A_W_3660_i1, A_W_3660_i2, A_W_3660_i3, A_W_3660_i4, A_W_3660_i5, A_W_3660_i6, A_W_3660_i7, A_W_3660_i8, A_W_3660_i9};
static const uint8_t* const A_W1830[10] = {A_W_1830_i0, A_W_1830_i1, A_W_1830_i2, A_W_1830_i3, A_W_1830_i4, A_W_1830_i5, A_W_1830_i6, A_W_1830_i7, A_W_1830_i8, A_W_1830_i9};

void showClockDigit(int x, int y, int n, char style) {
  if (style == 'W')      showDigit(x, y, A_W3660[n], COL_TEXT,   false);  // 白大
  else if (style == 'O') showDigit(x, y, A_O3660[n], COL_ORANGE, false);  // 橙大
  else                   showDigit(x, y, A_W1830[n], COL_TEXT,   true);   // 白小
}

// ---------------- 日期(透明文字, 与数字同款逻辑) ----------------
void drawDate() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d.%d.%d",
           timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  dateSpr.fillSprite(TRANSPARENT);
  dateSpr.setTextColor(COL_TEXT);
  dateSpr.setTextSize(2);
  int w = dateSpr.textWidth(buf);
  dateSpr.setCursor((dateSpr.width() - w) / 2, 0);
  dateSpr.print(buf);
  dateSpr.pushSprite((240 - w) / 2, 166, TRANSPARENT);
}

// ---------------- 冒号(白点) ----------------
void drawColon() {
  tft.fillCircle(114, 74, 4, COL_TEXT);
  tft.fillCircle(114, 102, 4, COL_TEXT);
}

// ---------------- 时钟(变化时透明叠加, 无背景矩形) ----------------
void digitalClockDisplay() {
  static int lastH = -1, lastM = -1, lastS = -1, lastD = -1;
  refreshTime();
  int h = timeinfo.tm_hour;
  int m = timeinfo.tm_min;
  int s = timeinfo.tm_sec;
  int d = timeinfo.tm_mday;

  // 数字: 透明背景 alpha 位图就地混合(每次从壁纸缓存读原始色, 无残影)
  if (h != lastH) {
    showClockDigit(34, 58, h/10, 'W');
    showClockDigit(70, 58, h%10, 'W');
    lastH = h;
  }
  if (m != lastM) {
    showClockDigit(122, 58, m/10, 'O');
    showClockDigit(158, 58, m%10, 'O');
    lastM = m;
  }
  if (s != lastS) {
    showClockDigit(102, 128, s/10, 'w');
    showClockDigit(120, 128, s%10, 'w');
    lastS = s;
  }
  // 日期是矢量文字, 保留"背景恢复+透明色"原方案(一直干净)
  if (d != lastD) {
    wallpaperSpr.pushSprite(40, 166, 40, 166, 160, 24); // 恢复日期区背景
    drawDate();
    lastD = d;
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== wallpaper_clock (壁纸+透明数字时钟) ===");

  setBacklight();
  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(COL_BG);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 挂载失败");
  }

  wallpaperSpr.setColorDepth(16);
  wallpaperSpr.createSprite(240, 240);
  numSpr.setColorDepth(16);
  numSpr.createSprite(36, 60);
  numSprSmall.setColorDepth(16);
  numSprSmall.createSprite(18, 30);
  dateSpr.setColorDepth(16);
  dateSpr.createSprite(120, 24);

  TJpgDec.setSwapBytes(true);

  // 1. 壁纸解码到 wallpaperSpr, 再推到屏幕
  if (SPIFFS.exists(WALLPAPER)) {
    TJpgDec.setJpgScale(1);   // 1/1 不缩放
    TJpgDec.setCallback(tft_output_wallpaper);
    TJpgDec.drawFsJpg(0, 0, WALLPAPER);
    wallpaperSpr.pushSprite(0, 0);
    Serial.print("壁纸 ");
    Serial.print(WALLPAPER);
    Serial.println(" 已显示");
  } else {
    Serial.print(WALLPAPER);
    Serial.println(" 不存在");
  }

  // WiFi + NTP(壁纸保持显示)
  WiFi.setTxPower(WIFI_POWER_8_5dBm);
  WiFi.begin(WIFI_SSID, WIFI_PASS);
  int retry = 0;
  while (WiFi.status() != WL_CONNECTED && retry < 40) {
    delay(500);
    Serial.print(".");
    retry++;
  }
  Serial.println();
  if (WiFi.status() == WL_CONNECTED) Serial.println("WiFi OK");
  else                               Serial.println("WiFi FAILED");

  configTime(8*3600, 0, "ntp.aliyun.com", "ntp6.aliyun.com");
  if (getLocalTime(&timeinfo, 10000)) Serial.println("NTP OK");
  else                               Serial.println("NTP FAILED");

  // 2. 叠加透明时钟
  drawColon();
  digitalClockDisplay();
}

void loop() {
  digitalClockDisplay();
  delay(200);
}
