#include <Arduino.h>
#include <TFT_eSPI.h>
// 注意：**不要** include <WiFi.h> —— 本工程是纯屏显诊断，不联网、不写 NVS。
//       具体原因见 setup() 末尾那段警告。
#include "../include/config.h"

TFT_eSPI tft = TFT_eSPI();

// RGB565颜色定义
#define TFT_RED    0xF800
#define TFT_GREEN  0x07E0
#define TFT_BLUE   0x001F
#define TFT_WHITE  0xFFFF
#define TFT_BLACK  0x0000

void setup() {
    Serial.begin(115200);
    Serial.println("\n=== Stock TV Starting ===");

    // 背光引脚初始化（P-MOS低电平点亮）
    pinMode(TFT_BL, OUTPUT);
    digitalWrite(TFT_BL, LOW);  // 点亮背光

    // TFT初始化
    Serial.println("Initializing TFT...");
    tft.init();

    // 颜色反转（关键！）
    tft.invertDisplay(true);

    // 设置旋转方向（0=USB下，1=USB右，2=USB上，3=USB左）
    // ⚠️ 必须与主工程 arduino/integrated 保持一致（那边是 setRotation(0)）。
    //    本工程是"屏显是否正常"的判据，朝向不一致会让人误以为屏有问题
    //    —— 实测踩过：rotation 2 相对主工程正好倒转 180°。
    tft.setRotation(0);

    // 清屏测试
    Serial.println("Testing display...");
    tft.fillScreen(TFT_BLACK);
    delay(500);

    // 显示测试图案
    tft.fillScreen(TFT_RED);
    delay(1000);
    tft.fillScreen(TFT_GREEN);
    delay(1000);
    tft.fillScreen(TFT_BLUE);
    delay(1000);

    // 显示文字
    tft.fillScreen(TFT_BLACK);
    tft.setTextColor(TFT_WHITE, TFT_BLACK);
    tft.setTextSize(2);
    tft.setCursor(10, 10);
    tft.println("Stock TV");
    tft.setCursor(10, 40);
    tft.println("ESP32-C3");
    tft.setCursor(10, 70);
    tft.println("Display OK!");

    Serial.println("Display test complete!");
    Serial.println("(本工程只测屏显，不碰 WiFi / NVS / SPIFFS)");

    // ⚠️⚠️ 【绝对不要在这里加 WiFi.begin()】⚠️⚠️
    //   本工程是"屏幕是否正常"的诊断工具，**不需要联网**。
    //   曾经这里有一段 `WiFi.begin(WIFI_SSID, WIFI_PASS)`，而 config.h 里那两个是
    //   占位符（"YOUR_WIFI_SSID"/"YOUR_WIFI_PASSWORD"）—— 后果是：
    //   ESP32 的 WiFi 驱动会【把这对占位符写进 NVS】，直接顶掉使用者真实的配网凭据。
    //   ⇒ 用户用它排障一次，设备就连不上自家 WiFi 了，还得重新配网。
    //   （实测踩过：排查后设备 AutoConnect 失败，读 NVS 才发现 ssid 被改成 YOUR_WIFI_SSID。）
    //   所以：本工程必须保持"只读屏幕"，一个字节都不写 NVS。
}

void loop() {
    delay(1000);
}
