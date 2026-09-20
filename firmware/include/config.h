#pragma once
// ============================================================
// stock-tv 用户配置 — 改完重新编译烧录
// ============================================================

// ---- WiFi（⚠️ 本调试工程**不使用**，保留仅为兼容早期版本）----
// ⚠️⚠️ 千万不要在 src/main.cpp 里拿这两项去调 WiFi.begin()！
//     ESP32 的 WiFi 驱动会把它们【写进 NVS】，顶掉使用者真实的配网凭据 ——
//     本工程是"屏显是否正常"的诊断工具，不需要联网，也不能碰 NVS。
//     （实测踩过：用过一次本工程后，设备 AutoConnect 失败，NVS 里的 ssid 被改成 YOUR_WIFI_SSID。）
#define WIFI_SSID   "YOUR_WIFI_SSID"
#define WIFI_PASS   "YOUR_WIFI_PASSWORD"

// ---- Data source（⚠️ 本调试工程当前未使用，历史遗留）----
// 早期版本曾用它在局域网里连 daemon/app.py。当前的 `src/main.cpp` 只做
// 引脚/背光/屏显的最小验证，不取任何数据，下面这几项均为历史保留。
#define WEBHOOK_BASE "http://192.168.1.100:8899/quote"

// ---- 自选列表（⚠️ 同样未被当前调试工程使用）----
// 股票/指数: sh600519 / sz000001 / sh000001(上证指数)
// 注: 场外基金(如 f005827)不在本仓库主工程的取数范围内；主工程走腾讯行情接口，
//     该接口不支持场外基金代码（详见 README「符号规则」）。
static const char* SYMBOLS[] = {
    "sh000001",   // 上证指数
    "sh600519",   // 贵州茅台
    "sz300308",   // 中际旭创(示例)
};
static const int SYMBOL_COUNT = sizeof(SYMBOLS) / sizeof(SYMBOLS[0]);

// ---- 节奏（⚠️ 未被当前调试工程使用）----
#define ROTATE_MS     5000UL     // 轮播翻页间隔
#define REFRESH_MS    60000UL    // 全列表数据刷新间隔
#define SPARK_POINTS  48         // 走势图采样点数

// ---- 背光亮度 0-255(硬件为P-MOS低电平点亮,代码内已做反相) ----
#define BACKLIGHT_LEVEL 200
