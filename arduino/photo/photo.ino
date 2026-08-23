// ============================================================
// photo.ino — 阶段③: ESP32-C3 相册(图片轮播 + 静态壁纸)
// 图片: SPIFFS 根目录 /1.jpg /2.jpg /3.jpg (240x240 JPEG)
// 轮播: 5秒/张; 只有1张图时即静态壁纸效果
// 库:   TFT_eSPI + TJpg_Decoder
// 引脚(已在 User_Setup.h 配好): SCL=IO3 SDA=IO5 DC=IO2 RST=IO6 BL=IO1 CS=GND
// ============================================================
#include <Arduino.h>
#include <SPIFFS.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>

#define ROTATE_MS   5000UL      // 轮播间隔
#define PHOTO_COUNT 3           // 最多图片数

const char* PHOTOS[] = {"/1.jpg", "/2.jpg", "/3.jpg"};

#define COL_BG   0x0000
#define COL_TEXT 0xFFFF

TFT_eSPI tft = TFT_eSPI();
int  curIdx      = 0;
int  totalPhotos = 0;
unsigned long lastRotate = 0;

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

// ---------------- 显示单张图片 ----------------
void drawPhoto(int idx) {
  if (SPIFFS.exists(PHOTOS[idx])) {
    TJpgDec.drawFsJpg(0, 0, PHOTOS[idx]);   // 240x240 全屏
  } else {
    tft.fillScreen(COL_BG);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(8, 100);
    tft.print("No Photo:");
    tft.setCursor(8, 130);
    tft.print(PHOTOS[idx]);
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== photo (ESP32-C3 相册) ===");

  setBacklight();
  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(COL_BG);

  if (!SPIFFS.begin(true)) {
    Serial.println("SPIFFS 挂载失败");
  }

  TJpgDec.setJpgScale(0);   // 1/1, 图片需240x240
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  // 扫描图片
  for (int i = 0; i < PHOTO_COUNT; i++) {
    bool ok = SPIFFS.exists(PHOTOS[i]);
    Serial.printf("%s %s\n", PHOTOS[i], ok ? "存在" : "缺失");
    if (ok) totalPhotos++;
  }
  if (totalPhotos == 0) Serial.println("SPIFFS 无图片, 请先上传 data/ 目录");

  drawPhoto(0);
  lastRotate = millis();
}

void loop() {
  // 只有 >1 张图才轮播(1张=静态壁纸)
  if (totalPhotos > 1 && millis() - lastRotate >= ROTATE_MS) {
    do {
      curIdx = (curIdx + 1) % PHOTO_COUNT;
    } while (!SPIFFS.exists(PHOTOS[curIdx]));   // 跳过缺失的图
    drawPhoto(curIdx);
    lastRotate = millis();
  }
  delay(20);
}
