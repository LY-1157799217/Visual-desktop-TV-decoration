#pragma once
// ============================================================
// stock-tv 用户配置 — 改完重新编译烧录
// ============================================================

// ---- WiFi ----
// Fill these values locally before compiling. Never commit real credentials.
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

// ---- Data source: webhook daemon running on your LAN ----
// Replace the example host with the computer running daemon/app.py.
#define WEBHOOK_BASE "http://192.168.1.100:8899/quote"

// ---- 自选列表 ----
// 股票/指数: sh600519 / sz000001 / sh000001(上证指数)
// 场外基金:  f005827(前缀f + 基金代码, 盘中显示估值)
static const char* SYMBOLS[] = {
    "sh000001",   // 上证指数
    "sh600519",   // 贵州茅台
    "sz300308",   // 中际旭创(示例)
    "f005827",    // 易方达蓝筹(示例基金)
};
static const int SYMBOL_COUNT = sizeof(SYMBOLS) / sizeof(SYMBOLS[0]);

// ---- 节奏 ----
#define ROTATE_MS     5000UL     // 轮播翻页间隔
#define REFRESH_MS    60000UL    // 全列表数据刷新间隔
#define SPARK_POINTS  48         // 走势图采样点数

// ---- 背光亮度 0-255(硬件为P-MOS低电平点亮,代码内已做反相) ----
#define BACKLIGHT_LEVEL 200
