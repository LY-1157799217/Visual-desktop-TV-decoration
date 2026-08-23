// 硬件SPI测试 — 用 TFT_eSPI 库（引脚已在 User_Setup.h 配置）
// SCL=IO3, SDA=IO5, DC=IO2, RST=IO6, BL=IO1
#include <TFT_eSPI.h>
#include <SPI.h>

TFT_eSPI tft = TFT_eSPI();

void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== 硬件SPI测试 (TFT_eSPI) ===");

  // 点亮背光（P-MOS低电平点亮）
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
  Serial.println("背光已点亮");

  // 初始化屏幕
  Serial.println("初始化屏幕...");
  tft.init();
  Serial.println("屏幕初始化完成");

  tft.setRotation(0);
  tft.invertDisplay(true);

  // 颜色测试
  Serial.println("红色");
  tft.fillScreen(TFT_RED);
  delay(2000);

  Serial.println("绿色");
  tft.fillScreen(TFT_GREEN);
  delay(2000);

  Serial.println("蓝色");
  tft.fillScreen(TFT_BLUE);
  delay(2000);

  Serial.println("白色");
  tft.fillScreen(TFT_WHITE);
  Serial.println("测试完成");
}

void loop() {
  // 空循环
}
