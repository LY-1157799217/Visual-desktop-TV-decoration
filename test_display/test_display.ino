// 硬件SPI测试 — 用 TFT_eSPI 库（引脚已在 User_Setup.h 配置）
// SCL=IO3, SDA=IO5, DC=IO2, RST=IO6, BL=IO1
//
// 【用途】屏不亮时最省事的自检：能出红绿蓝白四色 ⇒ 屏/背光/接线/引脚都对。
//
// ⚠️ 两个前提/局限（详见仓库根 README「疑难排查」的「🆘 先做这两件事」）：
//   1) 它【自己不带显示配置】，用的是库目录里那份 —— 所以必须先把仓库的
//      TFT_eSPI_Setup.h 覆盖到 TFT_eSPI 库的 User_Setup.h，再编译烧录。
//      不做这步可能因库里的旧配置而黑屏，会被误判成"硬件坏了"。
//   2) 它【只测纯色、不显示文字】—— 测不出"字体定义漏了"那个坑
//      （那个坑的表现是：有颜色、一个字都没有）。要连字体一起验，用 firmware/。
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
