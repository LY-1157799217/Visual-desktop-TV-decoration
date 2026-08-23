#include <Arduino.h>
#include <TFT_eSPI.h>
#include <WiFi.h>
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
    tft.setRotation(2);  // USB在上方

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

    // WiFi连接
    Serial.println("Connecting to WiFi...");
    WiFi.begin(WIFI_SSID, WIFI_PASS);

    int attempts = 0;
    while (WiFi.status() != WL_CONNECTED && attempts < 20) {
        delay(500);
        Serial.print(".");
        attempts++;
    }

    if (WiFi.status() == WL_CONNECTED) {
        Serial.println("\nWiFi connected!");
        Serial.print("IP: ");
        Serial.println(WiFi.localIP());

        tft.setCursor(10, 100);
        tft.println("WiFi: OK");
        tft.setCursor(10, 130);
        tft.print("IP:");
        tft.println(WiFi.localIP());
    } else {
        Serial.println("\nWiFi failed!");
        tft.setCursor(10, 100);
        tft.setTextColor(TFT_RED, TFT_BLACK);
        tft.println("WiFi: FAIL");
    }
}

void loop() {
    delay(1000);
}
