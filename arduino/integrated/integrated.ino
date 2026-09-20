// ============================================================
// integrated.ino — SDD 小电视 整合版 (Step2)
// 四模式: 时钟 / 天气 / 相册 / 股票(分时+日K双视图)
// 配网: WiFiManager | 切模式: WebServer | 配置: Preferences
// 引脚(已在 User_Setup.h 配好): SCL=IO3 SDA=IO5 DC=IO2 RST=IO6 BL=IO1 CS=GND
// ============================================================
//
// ── 诊断构建开关 ───────────────────────────────────────────────────────────
//
//   ⚠️⚠️ 这是【源码里的一行】—— 改数字后必须【重新编译 + 重新烧录】才生效。
//        不是网页按钮，也不是运行时可切的东西。
//        原因：要的是"发布版里那段埋点代码【根本不存在】"（串口干净、固件更小）。
//        网页按钮只能让它"不执行"——代码、字符串、printf 格式串仍然全在固件里。
//
//   CMP_BUILD = 1  → 诊断构建：带 [HB] 心跳 / loopPeakMs / [T] 逐笔取数耗时
//   CMP_BUILD = 0  → 发布构建：上述埋点【整段不编译】（正常使用）
//
//   ⚠️ 两种构建【只差埋点】，不含任何业务逻辑差异 —— 取数/节流/网页/防抖全部原样。
//   ⚠️ 两种构建的 FW_TAG 故意不同（new-rel- 前缀）⇒ 排障时一眼能认出烧的是哪种。
#define CMP_BUILD 0

#if CMP_BUILD
  #define FW_TAG "new-cmp9-20260917"     // 诊断构建
#else
  #define FW_TAG "new-rel-20260918"      // 发布构建
#endif

// ── 取数逐笔耗时打点（仅 CMP_BUILD=1 生效）───────────────────────────────────
//   用途：把每一笔网络取数（quote/spark/kline/weather）各自计时打出来，
//         并给 handleClient / autoBrightness / showWallpaper / renderCurrentMode
//         加 >=200ms 阈值打点。
//   ⇒ 判读：把 [T] 行的 ms【求和】塞进同一个 [HB] 窗口 ——
//       · 能对上 loopPeakMs 的尖峰 ⇒ 嫌疑锁定在那个类型上
//       · 全加起来还凑不出尖峰    ⇒ 病灶在没打点的别处（绘图/屏刷/WebServer）
//   ⚠️ 写成【宏】而不是函数：Arduino .ino 会给顶层函数插自动原型，
//      插错位置就是满屏 "has not been declared"。
//   ⚠️ 标签特意用 `[T]` 且不含 `[spark]`：长跑分析脚本用【子串】匹配 `[spark]`
//      计失败数，写成 `[T] spark ...` 不含该子串 ⇒ 不会污染那个计数。
#if CMP_BUILD
  #define T_FETCH(kind, sym, call) do {                     \
      unsigned long _t0 = millis();                         \
      bool _ok = (call);                                    \
      Serial.printf("[T] %s %s %lums ok=%d\n",              \
                    kind, sym, millis() - _t0, (int)_ok);   \
    } while (0)

  // 慢操作阈值打点：只打 >=200ms 的，避免刷屏（这些每轮都可能跑）
  #define T_SLOW_MS 200UL
  #define T_SLOW_PRINT(label, d) do { \
      if ((d) >= T_SLOW_MS) Serial.printf("[T] %s %lums\n", label, (unsigned long)(d)); \
    } while (0)
#else
  // 发布构建：⚠️ 仍然【必须真的调用取数本身】，只是不打点
  #define T_SLOW_MS 200UL
  #define T_FETCH(kind, sym, call)  do { (void)(call); } while (0)
  #define T_SLOW_PRINT(label, d)     do { (void)(d); } while (0)
#endif

// ── 导航防抖（`/set` 确认页 + 控制台首页）────────────────────────────────────
//   现象（用户实测）：在"已切换到…返回"这个页面上【连点"返回"】⇒ 浏览器弹出
//     `net::ERR_CONNECTION_RESET`（多数）/ `ERR_CONNECTION_REFUSED`（少数，且可能伴随重启）。
//   成因链（推断，但每一环都有实测支撑）：
//     ① 点"返回" = 加载控制台首页 `/`，而首页约 5KB，在慢速网络往返下要 3.9~6.6 秒
//        （它由 48 个 sendContent_P 分块发送；机制候选：分块数 / Nagle+延迟ACK，均未定证）
//     ② 用户等不及 ⇒ 再点一下 ⇒ 浏览器【取消上一次导航】另起请求
//     ③ 设备还在给那条被取消的连接写 chunk ⇒ 对它发 RST ⇒ ERR_CONNECTION_RESET
//   ⇒ 防抖切中的正是 ②：本次导航没结束前，后续点击一律 preventDefault。
//   ⚠️ 只拦【导航类】点击（<a> 或 onclick 里含 location.href/replace），
//      否则会把相册页的"选图并裁剪 / 确认并上传"那类纯 JS 按钮一起拦死。
//   ⚠️ 带 9 秒兜底解禁：万一导航失败（浏览器错误页），页面不会永久锁死，按 F5 也行。
#define NAV_GUARD_JS \
  "<script>(function(){var b=0;document.addEventListener('click',function(e){" \
  "var el=e.target.closest('a,button');if(!el)return;" \
  "var oc=el.getAttribute('onclick')||'';" \
  "if(!el.closest('a')&&!/location\\.(href|replace)/.test(oc))return;" \
  "if(b){e.preventDefault();e.stopPropagation();return;}" \
  "b=1;document.body.style.opacity='.45';" \
  "setTimeout(function(){b=0;document.body.style.opacity='';},9000);},true);})();</script>"

// 发一个 HTML 页面并自动挂上防抖脚本（替换 `server.send(200, "text/html; charset=utf-8", X)`）
// ⚠️ 写成宏而不是函数：绕开 .ino 自动原型陷阱
#define SEND_PAGE(body) do { \
    String _pg = body; \
    _pg += NAV_GUARD_JS; \
    server.send(200, "text/html; charset=utf-8", _pg); \
  } while (0)
#include <Arduino.h>
#include <WiFi.h>
#include <WebServer.h>
#include <WiFiManager.h>
#include <Preferences.h>
#include <TFT_eSPI.h>
#include <TJpg_Decoder.h>
#include <SPIFFS.h>
#include <HTTPClient.h>
#include <WiFiClientSecure.h>
#include <ArduinoJson.h>
#include <time.h>
#include <esp_heap_caps.h>
#include "number_alpha.h"  // alpha 数字字体(纯二值, 无JPEG伪影)
#include "weathernum.h"    // 天气图标/数字
#include "img/humidity.h"  // 湿度图标

TFT_eSPI tft = TFT_eSPI();
Preferences prefs;
WebServer server(80);
fs::File fsUploadFile;             // 网页上传文件句柄（写的是【临时文件】，见下）
String uploadError = "";            // 上传错误信息
bool uploadSuccess = false;         // 上传成功标志
// ★ 上传采用"写临时文件 → 校验 → 替换"，避免上传中断毁掉设备上原有的文件。
//   收数据时写 UPLOAD_TMP，只有完整收到并校验通过，才改名成 uploadTargetName。
#define UPLOAD_TMP        "/upload.tmp"
#define UPLOAD_MAX_BYTES  512000UL   // 单个文件上限 500KB
String uploadTargetName = "";        // 本次上传的最终文件名（已过白名单）
size_t uploadProgress = 0;           // 已写入临时文件的字节数（累计限额 + 完整性校验用）

#define COL_BG   0x0000
#define COL_TEXT 0xFFFF
#define COL_DIM  0x7BEF

// ---------------- 背光PWM ----------------
#define BL_PIN      1     // GPIO1 背光引脚
#define BL_CHANNEL  0     // LEDC通道0
#define BL_FREQ     5000  // PWM频率5kHz
#define BL_RES      8     // 8位分辨率(0-255)

// ---------------- 模式 ----------------
enum Mode { MODE_CLOCK = 0, MODE_WEATHER = 1, MODE_PHOTO = 2, MODE_STOCK = 3 };
const char* MODE_NAMES[]    = {"时钟", "天气", "相册", "股票"};
const char* MODE_NAMES_EN[] = {"CLOCK", "WEATHER", "PHOTO", "STOCK"};
Mode currentMode = MODE_CLOCK;

// ---------------- 配置 ----------------
String cityCode        = "101120301";
String cityLabel       = "Zi Bo";     // 城市显示名称(拼音,可配置)
String stockCode       = "sh600519";
int    brightness      = 50;
bool   autoBrightness  = false;  // 自动亮度调节开关
int    defaultMode     = 0;
int    refreshInterval = 60;  // ⚠️ 仅 NVS 无值时生效；NVS 已有值则用存储值 ⇒ 可用 /set?refresh= 对齐
int    wallpaperMode   = 1;   // 壁纸模式: 0无 1静态 2动态
int    wallpaperIndex  = 0;   // 静态壁纸索引(0-2)

// ---------------- Web HTML 静态模板(存Flash节省RAM) ----------------
const char HTML_HEADER[] PROGMEM = R"(<!DOCTYPE html><html><head><meta charset='utf-8'>
<meta name='viewport' content='width=device-width,initial-scale=1'>
<title>SDD 小电视</title>
<style>
body{font-family:-apple-system,sans-serif;max-width:480px;margin:0 auto;padding:14px;background:#1a1a2e url('/m.jpg') no-repeat center center fixed;background-size:cover;color:#eee;min-height:100vh;box-sizing:border-box;}
.card{background:rgba(22,33,62,.9);border-radius:12px;padding:12px;margin:10px 0;}
h3{text-align:center;margin:4px 0;}
h4{margin:4px 0;}
.grid{display:flex;flex-wrap:wrap;gap:8px;}
button{font-size:18px;padding:10px 16px;border:none;border-radius:8px;background:#0f3460;color:#eee;cursor:pointer;flex:1;min-width:80px;}
button:active{background:#16537e;}
a{text-decoration:none;}
details.card>summary{font-size:18px;font-weight:bold;cursor:pointer;padding:6px;list-style:none;}
details.card>summary::before{content:'\25B8 ';}
details.card[open]>summary::before{content:'\25BE ';}
.sub{font-size:14px;color:#9ab;margin-top:8px;}
.center{text-align:center;}
</style></head><body>
<h3>SDD 小电视控制台</h3>
)";

struct tm timeinfo;

// ---------------- 天气数据 ----------------
struct Weather {
  float temp = 0; int humi = 0; int press = 0; int aqi = 0; int code = 99; bool ok = false;
};
Weather w;
WeatherNum wrat;

// ===== 股票模块(多股轮动 + 分时/日K双视图) =====
#define SPARK_POINTS 48
#define KLINE_COUNT  30
#define SYMBOL_COUNT 4

struct Sym { const char* code; const char* label; };
Sym SYMBOLS[SYMBOL_COUNT] = {
  {"sh000001", "SZZS"},   // 上证指数
  {"sh600519", "GZMT"},   // 贵州茅台
  {"sz300308", "ZJXC"},   // 中际旭创
  {"sz000725", "JDFA"}    // 京东方A
};
// 启动时从 NVS 加载股票代码，没有就用上面的默认值

struct Quote {
  float price = 0, change = 0, changePct = 0, prevClose = 0, high = 0, low = 0;
  float spark[SPARK_POINTS];       // 分时采样点
  int   sparkLen = 0;
  float kl[KLINE_COUNT][4];        // 日K: [i][0]=开 [1]=收 [2]=高 [3]=低
  int   klCount = 0;
  bool ok = false;                 // 行情
  bool sparkOk = false;            // 分时
  bool klOk = false;               // 日K
};
Quote quotes[SYMBOL_COUNT];
int stockCurIdx = 0;               // 当前轮播索引
int stockView = 0;                 // 0=分时图 1=日K图
unsigned long stockLastRotate = 0;

// ── 取数节流：每股一份戳 + TTL ──────────────────────────────────────────────
//   ★为什么必须【每股一份戳】：全局戳会退化成"只有一只是新的、另外三只永远不更新"
//     —— 这是一类【周期共振】错误：多个目标共用一个节流戳时相位会锁死。
//   ★为什么用 TTL 而不是每轮拉：分时线每秒都在动，但**两分钟内的变化肉眼不可辨**；
//     而每拉一次都要一次完整 TLS 握手（web.ifzq.gtimg.cn 强制 HTTPS）。
//     本版无中转主机 ⇒ 分时只能设备自己走 TLS ⇒ 这是本固件**最贵**的一次取数。
//
//   ★可调旋钮★：SPARK_TTL_MS = 分时图多久重新拉一次。
//     ⚠️ 定 120000 不是拍脑袋，依据是【分时图自己的采样精度】：
//        fetchSpark 把腾讯的【逐分钟】数组(≤241点)均匀抽成 SPARK_POINTS=48 个点 ⇒
//        **屏幕上每个点代表 5 分钟**，最后一个点 = prices[47*cnt/48] ⇒
//        天生滞后 cnt/48 分钟（11:30 时 2.5 分钟，收盘 5 分钟）。
//        ⇒ 一天里约 80% 的时间，60 秒刷新中有 4/5 次拉回来的是【逐像素相同】的图。
//        ⇒ 120s 只是砍掉这些无用重复；肉眼不可分辨。
//        ⚠️ 唯一例外：开盘头 48 分钟(cnt<48)抽样几乎逐分钟，右端点确实实时 ⇒
//           那时 120s 会让右端慢约 1 分钟（≈图上 4 像素）。可接受。
//     ★若哪天想更保守★：改回 60000UL。但须知【取数率会完全回到未节流时的水平】
//       （每股仍需 60s 一副新图 ⇒ 省不下任何一次 TLS）。
#define SPARK_TTL_MS 120000UL                    // 分时图：每股 2 分钟（依据见上）
#define KLINE_TTL_MS 1800000UL                   // 日K：30 分钟（一天才出一根，给长毫无损失）
unsigned long sparkStamp[SYMBOL_COUNT] = {0};    // 0 = 从未拉过（首屏必定拉）
unsigned long klineStamp[SYMBOL_COUNT] = {0};

// alpha 数字字体(时钟, 无JPEG伪影)
TFT_eSprite numSpr(&tft);        // 36x60(时分)
TFT_eSprite numSprSmall(&tft);   // 18x30(秒)
TFT_eSprite wallpaperSpr(&tft);  // 壁纸缓存 240x240
TFT_eSprite dateSpr(&tft);       // 日期 sprite 120x24
TFT_eSprite tempSpr(&tft);       // 温度大字 sprite 160x40
TFT_eSprite textSpr(&tft);       // 小字 sprite 160x24(城市/湿度/气压/AQI)
TFT_eSprite chartSpr(&tft);      // 股票走势图帧缓冲 224x80(16bpp 35KB, 原子推屏)
bool wallpaperFreed = false;     // 股票模式是否已释放壁纸缓存(腾 115KB 给 HTTPS)
bool chartSprFreed = false;      // 时钟/天气模式是否已释放图表缓冲(腾 35KB 给壁纸重建)
bool smallFreed = false;         // 股票模式是否已释放小 sprite(时钟/天气数字文字缓冲, 腾 32KB)
#define COL_ORANGE 0xFD20
#define TRANSPARENT 0x0000
static const uint8_t* const A_O3660[10] = {A_O_3660_i0, A_O_3660_i1, A_O_3660_i2, A_O_3660_i3, A_O_3660_i4, A_O_3660_i5, A_O_3660_i6, A_O_3660_i7, A_O_3660_i8, A_O_3660_i9};
static const uint8_t* const A_W3660[10] = {A_W_3660_i0, A_W_3660_i1, A_W_3660_i2, A_W_3660_i3, A_W_3660_i4, A_W_3660_i5, A_W_3660_i6, A_W_3660_i7, A_W_3660_i8, A_W_3660_i9};
static const uint8_t* const A_W1830[10] = {A_W_1830_i0, A_W_1830_i1, A_W_1830_i2, A_W_1830_i3, A_W_1830_i4, A_W_1830_i5, A_W_1830_i6, A_W_1830_i7, A_W_1830_i8, A_W_1830_i9};

// ---------------- 相册 ----------------
const char* PHOTOS[] = {"/1.jpg", "/2.jpg", "/3.jpg"};
int curIdx = 0;
unsigned long lastRotate = 0;

// ---------------- 工具函数 ----------------
void loadConfig() {
  cityCode        = prefs.getString("cityCode", "101120301");
  cityLabel       = prefs.getString("cityLabel", "Zi Bo");
  stockCode       = prefs.getString("stockCode", "sh600519");
  brightness      = prefs.getInt("brightness", 50);
  autoBrightness  = prefs.getBool("autoBrightness", false);
  defaultMode     = prefs.getInt("defaultMode", 0);
  refreshInterval = prefs.getInt("refreshInterval", 60);  // 缺省 60s（与全局默认值一致）
  wallpaperMode   = prefs.getInt("wallpaperMode", 1);
  wallpaperIndex  = prefs.getInt("wallpaperIndex", 0);
}

void setBacklight() {
  // 先不启用PWM，只用pinMode初始化
  pinMode(BL_PIN, OUTPUT);
  digitalWrite(BL_PIN, LOW);  // 默认全亮

  Serial.println("背光初始化: GPIO1, 模式=OUTPUT, 默认全亮");
  Serial.println("PWM测试模式：将在setBrightness()中尝试启用PWM");
}

// SPIFFS 启动清理(删除不在白名单的文件，保持存储干净)
void cleanupSPIFFS() {
  // 白名单：有效文件
  const char* VALID_FILES[] = {
    "/1.jpg", "/2.jpg", "/3.jpg",  // 相册3张
    "/m.jpg",                       // 网页壁纸
    "/city_data.json"               // 城市数据
  };

  // ★ 第一步：先把"上次替换中断"救回来 —— 必须早于下面任何删除动作。
  //   /do_upload 替换一个【已存在】的文件的顺序是：
  //       目标 → <目标>.bak  ⇒  tmp → 目标  ⇒  删 <目标>.bak
  //   所以开机时若发现 <目标>.bak：
  //     · 目标已存在 ⇒ 替换其实已经完成，只是没来得及删备份 ⇒ 删掉 .bak
  //     · 目标不存在 ⇒ 卡在中间那一步（掉电/改名失败）⇒ 把 .bak 改回来，旧文件救回
  //   ⚠️ .bak 故意【不】放进下面的 OLD_FILES —— 放进去就等于在恢复之前，
  //      先把唯一还留着旧数据的那个文件删了。
  //   ⚠️ 这张表必须与 /do_upload 的 ALLOWED_FILES 一致。
  const char* BAK_TARGETS[] = {
    "/1.jpg", "/2.jpg", "/3.jpg", "/m.jpg", "/city_data.json"
  };
  for (int i = 0; i < sizeof(BAK_TARGETS) / sizeof(BAK_TARGETS[0]); i++) {
    String bakPath = String(BAK_TARGETS[i]) + ".bak";
    if (!SPIFFS.exists(bakPath)) continue;
    if (SPIFFS.exists(BAK_TARGETS[i])) {
      SPIFFS.remove(bakPath);
      Serial.println("替换备份清理: " + bakPath);
    } else {
      bool back = SPIFFS.rename(bakPath, BAK_TARGETS[i]);
      Serial.printf("上次替换中断，恢复 %s ← %s (%s)\n",
                    BAK_TARGETS[i], bakPath.c_str(), back ? "成功" : "失败，下次开机再试");
    }
  }

  // 按【固定名单】删除旧版本遗留文件（直接尝试删除法，避免遍历API问题）
  const char* OLD_FILES[] = {
    "/4.jpg", "/5.jpg", "/6.jpg", "/7.jpg", "/8.jpg",  // 旧版相册残留
    "/test.jpg", "/temp.jpg", "/backup.jpg",            // 可能的测试文件
    UPLOAD_TMP                                          // 上传中断留下的临时文件残片
  };

  Serial.println("开始清理SPIFFS...");
  int deleted = 0;

  for (int i = 0; i < sizeof(OLD_FILES) / sizeof(OLD_FILES[0]); i++) {
    if (SPIFFS.exists(OLD_FILES[i])) {
      Serial.println("删除无用文件: " + String(OLD_FILES[i]));
      SPIFFS.remove(OLD_FILES[i]);
      deleted++;
    }
  }

  Serial.println("清理完成，删除了 " + String(deleted) + " 个文件");
}

// 背光亮度设置(0-100 → PWM占空比)
void setBrightness(int level) {
  if (level < 0) level = 0;
  if (level > 100) level = 100;

  if (level == 0) {
    // 完全关闭：先detach PWM，再用GPIO拉高
    ledcDetachPin(BL_PIN);
    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, HIGH);  // P-MOS：高电平关闭
    Serial.println("背光: 关闭 (GPIO HIGH)");
  } else if (level == 100) {
    // 完全打开：先detach PWM，再用GPIO拉低
    ledcDetachPin(BL_PIN);
    pinMode(BL_PIN, OUTPUT);
    digitalWrite(BL_PIN, LOW);   // P-MOS：低电平全亮
    Serial.println("背光: 全亮 (GPIO LOW)");
  } else {
    // PWM调光：动态启用PWM
    ledcSetup(BL_CHANNEL, BL_FREQ, BL_RES);
    ledcAttachPin(BL_PIN, BL_CHANNEL);
    int duty = map(level, 0, 100, 255, 0);  // 反向映射
    ledcWrite(BL_CHANNEL, duty);
    Serial.printf("背光: %d%% (PWM duty=%d, 频率=%dHz)\n", level, duty, BL_FREQ);
  }

  brightness = level;  // 更新全局变量
}

// 自动亮度调节(根据时间段)
void autoAdjustBrightness() {
  if (!autoBrightness) return;  // 未开启自动调节，跳过
  if (!getLocalTime(&timeinfo)) return;  // 时间未同步，跳过

  int hour = timeinfo.tm_hour;
  int targetBrightness = brightness;

  // 时间段亮度策略
  if (hour >= 8 && hour < 12) {
    targetBrightness = 60;  // 08:00-11:59 → 60%
  } else if (hour >= 12 && hour < 15) {
    targetBrightness = 90;  // 12:00-14:59 → 90%
  } else if (hour >= 15 && hour < 20) {
    targetBrightness = 70;  // 15:00-19:59 → 70%
  } else if (hour >= 20 || hour < 8) {
    targetBrightness = 35;  // 20:00-07:59 → 35%
  }

  // 只在亮度变化时调整
  if (targetBrightness != brightness) {
    setBrightness(targetBrightness);
    Serial.printf("自动亮度: 时段%02d:xx → %d%%\n", hour, targetBrightness);
  }
}

// TFT输出回调(JPEG解码块推屏)
bool tft_output(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  if (y >= tft.height()) return 0;
  tft.pushImage(x, y, w, h, bitmap);
  return 1;
}

// 壁纸回调(解码到 wallpaperSpr)
bool tft_output_wallpaper(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  wallpaperSpr.pushImage(x, y, w, h, bitmap);
  return 1;
}

// 图标色键回调: 黑底转透明, 直接透明叠加到屏幕(壁纸)
bool tft_output_icon(int16_t x, int16_t y, uint16_t w, uint16_t h, uint16_t* bitmap) {
  for (int i = 0; i < w * h; i++) {
    uint16_t c = (bitmap[i] >> 8) | (bitmap[i] << 8);   // 交换回标准字节序(TJpgDec输出是交换序)
    uint8_t r = (c >> 11) & 0x1F;  r = (r << 3) | (r >> 2);
    uint8_t g = (c >> 5) & 0x3F;   g = (g << 2) | (g >> 4);
    uint8_t b = c & 0x1F;          b = (b << 3) | (b >> 2);
    uint8_t mx = r > g ? (r > b ? r : b) : (g > b ? g : b);
    if (mx < 50) bitmap[i] = 0x0000;   // 黑底+振铃+暗过渡(0-49)转透明
  }
  tft.pushImage(x, y, w, h, bitmap, 0x0000);   // 0x0000 不画, 叠到壁纸
  return 1;
}

// 解码相册图片到壁纸缓存
void loadWallpaper(int idx) {
  if (SPIFFS.exists(PHOTOS[idx])) {
    TJpgDec.setJpgScale(1);   // 1/1 不缩放
    TJpgDec.setCallback(tft_output_wallpaper);
    TJpgDec.drawFsJpg(0, 0, PHOTOS[idx]);
    TJpgDec.setCallback(tft_output);
  }
}

// 根据壁纸模式显示背景(无=纯黑, 静态=固定, 动态=当前索引)
void showWallpaper() {
  if (wallpaperMode == 0) {
    tft.fillScreen(COL_BG);          // 无壁纸, 屏幕纯黑
    wallpaperSpr.fillSprite(COL_BG); // 壁纸缓存也清黑(避免数字/天气读旧壁纸)
  } else {
    loadWallpaper(wallpaperIndex);
    wallpaperSpr.pushSprite(0, 0);
  }
}

// 壁纸缓存可能在股票模式被释放, 进入时钟/天气前重建(需在其他 sprite 都释放后再重建)
void ensureWallpaper() {
  if (wallpaperFreed) {
    wallpaperSpr.setColorDepth(16);
    if (wallpaperSpr.createSprite(240, 240) != nullptr) {
      wallpaperFreed = false;
    } else {
      Serial.printf("[wp] fail free=%d maxBlk=%d\n", ESP.getFreeHeap(), heap_caps_get_largest_free_block(MALLOC_CAP_DMA));
    }
  }
}

// 图表缓冲可能在时钟/天气模式被释放, 进入股票前重建
void ensureChart() {
  if (chartSprFreed) {
    chartSpr.setColorDepth(16);
    if (chartSpr.createSprite(224, 80) != nullptr) {
      chartSprFreed = false;
    } else {
      Serial.println("[chart] createSprite fail");
    }
  }
}

// 释放小 sprite(时钟/天气的数字文字缓冲), 股票模式用不到, 腾堆
void freeSmallSprites() {
  if (!smallFreed) {
    numSpr.deleteSprite();
    numSprSmall.deleteSprite();
    dateSpr.deleteSprite();
    tempSpr.deleteSprite();
    textSpr.deleteSprite();
    smallFreed = true;
  }
}

// 重建小 sprite(时钟/天气需要)
void ensureSmallSprites() {
  if (smallFreed) {
    numSpr.setColorDepth(16); numSpr.createSprite(36, 60);
    numSprSmall.setColorDepth(16); numSprSmall.createSprite(18, 30);
    dateSpr.setColorDepth(16); dateSpr.createSprite(120, 24);
    tempSpr.setColorDepth(16); tempSpr.createSprite(160, 40);
    textSpr.setColorDepth(16); textSpr.createSprite(160, 24);
    smallFreed = false;
  }
}

// ---------------- WiFiManager 配网 ----------------
void setupWifi() {
  WiFiManager wm;
  WiFiManagerParameter p_cc("CityCode", "城市代码", cityCode.c_str(), 9);
  WiFiManagerParameter p_bl("LCDBL", "屏幕亮度(1-100)", String(brightness).c_str(), 3);
  WiFiManagerParameter p_stock("StockCode", "股票代码", stockCode.c_str(), 9);
  WiFiManagerParameter p_mode("DefMode", "默认模式(0时钟1天气2相册3股票)", String(defaultMode).c_str(), 1);
  WiFiManagerParameter p_refresh("Refresh", "股票刷新间隔(秒)", String(refreshInterval).c_str(), 3);
  wm.addParameter(&p_cc); wm.addParameter(&p_bl); wm.addParameter(&p_stock);
  wm.addParameter(&p_mode); wm.addParameter(&p_refresh);
  wm.setConfigPortalTimeout(180);

  if (!wm.autoConnect("SDD小电视")) {
    Serial.println("配网失败, 重启");
    ESP.restart();
  }
  prefs.putString("cityCode", p_cc.getValue());
  prefs.putString("stockCode", p_stock.getValue());
  prefs.putInt("brightness", atoi(p_bl.getValue()));
  prefs.putInt("defaultMode", atoi(p_mode.getValue()));
  prefs.putInt("refreshInterval", atoi(p_refresh.getValue()));
  loadConfig();
  Serial.println("WiFi 已连接: " + WiFi.localIP().toString());
}

// ---------------- WebServer ----------------
void handleRoot() {
  // ⚠️ 早期写法是"chunked 传输，避免拼接大 String"，实测发现在本机上拼接反而更快
  // 故改为：整页先拼进一个 String，最后一次性发完
  //   原写法 = setContentLength(UNKNOWN) + 49 个 sendContent_P 分块。
  //   实测：4997 字节要 3.9~6.6 秒才传完 ⇒ 用户等不及连点 ⇒ 浏览器取消导航 ⇒ RST。
  //   ⚠️ PROGMEM 在 ESP32 上就是普通指针（flash 可直接寻址），所以 PSTR() 能直接 += 进 String。
  String page;
  page.reserve(7168);            // 实测整页 ~5.7KB，多留余量避免中途 realloc


  // 发送HTML头部（从Flash读取）
  page += (HTML_HEADER);
  page += (String(NAV_GUARD_JS));   // 控制台首页也挂防抖（防连点模式按钮）

  // 动态内容：当前状态
  char buf[512];  // 增大缓冲区到512字节
  snprintf(buf, sizeof(buf),
    "<div class='card center'>当前模式: <b>%s</b> &middot; IP: %s</div>",
    MODE_NAMES[currentMode], WiFi.localIP().toString().c_str());
  page += (buf);

  // 模式切换
  page += (PSTR("<div class='card'><h4>模式切换</h4><div class='grid'>"));
  page += (PSTR("<a href='/set?mode=0'><button>时钟</button></a>"));
  page += (PSTR("<a href='/set?mode=1'><button>天气</button></a>"));
  page += (PSTR("<a href='/set?mode=2'><button>相册</button></a>"));
  page += (PSTR("<a href='/set?mode=3'><button>股票</button></a>"));
  page += (PSTR("</div></div>"));

  // 壁纸折叠区
  page += (PSTR("<details class='card'><summary>壁纸</summary><div class='grid'>"));
  page += (PSTR("<a href='/wp_select'><button>静态壁纸</button></a>"));
  page += (PSTR("<a href='/set?wp=2'><button>动态壁纸</button></a>"));
  page += (PSTR("<a href='/set?wp=0'><button>关闭壁纸</button></a>"));
  page += (PSTR("</div></details>"));

  // 股票视图折叠区
  page += (PSTR("<details class='card'><summary>股票视图</summary><div class='grid'>"));
  page += (PSTR("<a href='/set?stockview=0'><button>分时图</button></a>"));
  page += (PSTR("<a href='/set?stockview=1'><button>日K图</button></a>"));
  page += (PSTR("</div>"));
  snprintf(buf, sizeof(buf), "<div class='sub'>当前视图: %s</div></details>",
    stockView == 0 ? "分时图" : "日K图");
  page += (buf);

  // 屏幕亮度调节
  page += (PSTR("<details class='card'><summary>屏幕亮度</summary><div style='margin-top:12px'>"));

  // 自动亮度开关
  snprintf(buf, sizeof(buf),
    "<div style='margin-bottom:12px;padding:8px;background:rgba(15,52,96,0.5);border-radius:6px'>"
    "<label style='display:flex;align-items:center;cursor:pointer'>"
    "<input type='checkbox' id='autoBr' %s onchange='location.href=\"/set?auto_brightness=\"+(this.checked?1:0)' "
    "style='width:20px;height:20px;margin-right:8px'>"
    "<span>自动亮度调节</span></label>"
    "<div class='sub' style='margin-top:4px;font-size:12px'>08:00→60%% | 12:00→90%% | 15:00→70%% | 20:00→35%%</div>"
    "</div>",
    autoBrightness ? "checked" : "");
  page += (buf);

  // 手动亮度滑块
  snprintf(buf, sizeof(buf),
    "<input type='range' min='0' max='100' value='%d' id='brightness' "
    "style='width:100%%;height:8px;border-radius:4px;outline:none;background:#0f3460' "
    "oninput='document.getElementById(\"brValue\").innerText=this.value'>"
    "<div style='text-align:center;margin-top:8px;font-size:20px;color:#38bdf8'>"
    "<span id='brValue'>%d</span>%%</div>"
    "<button onclick='location.href=\"/set?brightness=\"+document.getElementById(\"brightness\").value' "
    "style='width:100%%;margin-top:8px'>手动调节亮度</button>"
    "</div></details>",
    brightness, brightness);
  page += (buf);

  // 天气城市切换
  snprintf(buf, sizeof(buf),
    "<details class='card'><summary>天气城市</summary>"
    "<div class='sub' style='margin-bottom:8px'>当前: <b>%s</b> (%s) "
    "<a href='/city_list' style='color:#38bdf8'>[查询城市代码]</a></div>"
    "<div class='grid'>",
    cityLabel.c_str(), cityCode.c_str());
  page += (buf);

  // 快捷城市按钮
  page += (PSTR("<a href='/set?city=101010100&label=Beijing'><button>北京</button></a>"));
  page += (PSTR("<a href='/set?city=101020100&label=Shanghai'><button>上海</button></a>"));
  page += (PSTR("<a href='/set?city=101280101&label=Gz'><button>广州</button></a>"));
  page += (PSTR("<a href='/set?city=101280601&label=Shenzhen'><button>深圳</button></a>"));
  page += (PSTR("<a href='/set?city=101210101&label=Hangzhou'><button>杭州</button></a>"));
  page += (PSTR("<a href='/set?city=101270101&label=Chengdu'><button>成都</button></a>"));
  page += (PSTR("<a href='/set?city=101110101&label=Xian'><button>西安</button></a>"));
  page += (PSTR("<a href='/set?city=101200101&label=Wuhan'><button>武汉</button></a>"));
  page += (PSTR("<a href='/set?city=101120301&label=Zi Bo'><button>淄博</button></a>"));
  page += (PSTR("<a href='/set?city=101250101&label=Jinan'><button>济南</button></a>"));
  page += (PSTR("</div></details>"));

  // 快捷功能
  page += (PSTR("<div class='card center'><p>"));
  page += (PSTR("<a href='/upload' style='color:#9ab'>上传图片...</a> &middot; "));
  page += (PSTR("<a href='/stock_edit' style='color:#9ab'>股票配置...</a> &middot; "));
  page += (PSTR("<a href='/spiffs_list' style='color:#9ab'>SPIFFS存储信息</a></p>"));
  page += (PSTR("</div>"));

  // WiFi管理折叠区
  page += (PSTR("<details class='card' style='background:rgba(120,30,30,.85);border:1px solid #f87171;margin-top:20px'>"));
  page += (PSTR("<summary style='color:#fca5a5'>⚠ WiFi 管理</summary>"));
  page += (PSTR("<div class='grid' style='margin-top:12px'>"));
  page += (PSTR("<a href='/change_wifi'><button style='background:#16a34a;color:#fff'>更换 WiFi</button></a>"));
  page += (PSTR("<button onclick=\"if(confirm('确认重置 WiFi？\\n\\n设备将重启并进入配网模式（AP 热点）。\\n\\n请在设备重启后（约 5 秒），手动连接热点 SDD小电视，再打开 192.168.4.1 重新配网。')){location.href='/reset_wifi'}\" style='background:#dc2626;color:#fff'>重置 WiFi</button>"));
  page += (PSTR("</div>"));
  page += (PSTR("<div class='sub' style='margin-top:8px'>更换: 输入新 WiFi 快速切换<br>重置: 清空配置进 AP 配网模式（保底）</div>"));
  page += (PSTR("</details>"));

  // 结束标签
  page += (PSTR("</body></html>"));
  server.send(200, "text/html; charset=utf-8", page);   // 一次发完
}

void handleSet() {
  // 切模式
  if (server.hasArg("mode")) {
    int m = server.arg("mode").toInt();
    if (m >= 0 && m < 4) {
      setMode((Mode)m);
      SEND_PAGE(
                  "已切换到 <b>" + String(MODE_NAMES[m]) + "</b>  <a href='/'>返回</a>");
      return;
    }
  }
  // 设壁纸模式
  if (server.hasArg("wp")) {
    int wp = server.arg("wp").toInt();
    if (wp >= 0 && wp <= 2) {
      wallpaperMode = wp;
      prefs.putInt("wallpaperMode", wp);
      if (server.hasArg("idx")) {          // 静态壁纸索引
        wallpaperIndex = server.arg("idx").toInt();
        prefs.putInt("wallpaperIndex", wallpaperIndex);
      }
      if (currentMode == MODE_CLOCK || currentMode == MODE_WEATHER) {
        setMode(currentMode);              // 重新显示背景
      }
      SEND_PAGE(
                  "壁纸已设置  <a href='/'>返回</a>");
      return;
    }
  }
  // 切换城市
  if (server.hasArg("city")) {
    String newCity = server.arg("city");
    String newLabel = server.hasArg("label") ? server.arg("label") : "";
    newCity.trim();
    newLabel.trim();
    if (newCity.length() >= 6 && newCity.length() <= 12) {  // 城市代码一般9位
      cityCode = newCity;
      prefs.putString("cityCode", cityCode);
      // 如果有label参数，保存label；否则保持旧label
      if (newLabel.length() > 0) {
        cityLabel = newLabel;
        prefs.putString("cityLabel", cityLabel);
      }
      w.ok = false;  // 强制重新抓取天气
      if (currentMode == MODE_WEATHER) {
        renderWeather();  // 立即刷新天气显示
      }
      SEND_PAGE(
                  "城市已切换到 <b>" + cityLabel + "</b> (" + cityCode + ")  <a href='/'>返回</a>");
      return;
    } else {
      server.send(400, "text/html; charset=utf-8",
                  "城市代码格式错误（应为6-12位数字）  <a href='/'>返回</a>");
      return;
    }
  }
  // 设股票视图(分时/日K)
  if (server.hasArg("stockview")) {
    int sv = server.arg("stockview").toInt();
    if (sv >= 0 && sv <= 1) {
      stockView = sv;
      if (currentMode == MODE_STOCK) drawQuote(stockCurIdx, true);   // 重绘当前股
      SEND_PAGE(
                  "股票视图已切换  <a href='/'>返回</a>");
      return;
    }
  }
  // 设置亮度
  if (server.hasArg("brightness")) {
    int br = server.arg("brightness").toInt();
    if (br >= 0 && br <= 100) {
      autoBrightness = false;  // 手动调节时关闭自动亮度
      prefs.putBool("autoBrightness", false);
      setBrightness(br);
      prefs.putInt("brightness", br);
      SEND_PAGE(
                  "亮度已设置为 <b>" + String(br) + "%</b> (自动亮度已关闭)  <a href='/'>返回</a>");
      return;
    }
  }
  // 自动亮度开关
  if (server.hasArg("auto_brightness")) {
    int ab = server.arg("auto_brightness").toInt();
    autoBrightness = (ab == 1);
    prefs.putBool("autoBrightness", autoBrightness);
    if (autoBrightness) {
      autoAdjustBrightness();  // 立即调整一次
      SEND_PAGE(
                  "自动亮度已<b>开启</b>  <a href='/'>返回</a>");
    } else {
      SEND_PAGE(
                  "自动亮度已<b>关闭</b>  <a href='/'>返回</a>");
    }
    return;
  }
  // 股票刷新间隔 —— 可直接在网页设定（早期只能进配网门户改）。
  if (server.hasArg("refresh")) {
    int ri = server.arg("refresh").toInt();
    if (ri >= 5 && ri <= 600) {
      refreshInterval = ri;
      prefs.putInt("refreshInterval", ri);
      char b2[192];
      snprintf(b2, sizeof(b2),
               "行情刷新间隔已设为 <b>%d 秒</b>（每 %.2f 秒轮询一只标的）  <a href='/'>返回</a>",
               ri, ri / (float)SYMBOL_COUNT);
      SEND_PAGE( b2);
    } else {
      server.send(400, "text/html; charset=utf-8",
                  "刷新间隔应为 5~600 秒  <a href='/'>返回</a>");
    }
    return;
  }
  server.send(400, "text/plain", "参数错误");
}

// 静态壁纸选图页(3张图总览)
void handleWallpaperSelect() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>选择静态壁纸</title></head><body>";
  html += "<h3>选择一张作为静态壁纸</h3><p>";
  for (int i = 0; i < 3; i++) {
    String mark = (wallpaperMode == 1 && wallpaperIndex == i) ? " ✓" : "";
    html += "<a href='/set?wp=1&idx=" + String(i) + "'><button style='font-size:20px;margin:4px'>"
            "图片" + String(i + 1) + mark + "</button></a> ";
  }
  html += "</p><p><a href='/'>返回</a></p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

// 网页壁纸图片服务(SPIFFS 里的 m.jpg)
void handleImage() {
  if (SPIFFS.exists("/m.jpg")) {
    fs::File f = SPIFFS.open("/m.jpg", "r");
    if (f) {
      server.streamFile(f, "image/jpeg");
      f.close();
      return;
    }
  }
  server.send(404, "text/plain", "not found");
}

// 城市数据JSON服务(SPIFFS 里的 city_data.json)
void handleCityDataJSON() {
  if (SPIFFS.exists("/city_data.json")) {
    fs::File f = SPIFFS.open("/city_data.json", "r");
    if (f) {
      server.streamFile(f, "application/json");
      f.close();
      return;
    }
  }
  server.send(404, "application/json", "{\"error\":\"city_data.json not found\"}");
}

// 网页上传图片页(分区：网页壁纸直传 + 相册前端裁剪240x240)
void handleUploadPage() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>上传图片</title>";
  html += "<link rel='stylesheet' href='https://cdn.jsdelivr.net/npm/cropperjs@1.6.1/dist/cropper.min.css'>";
  html += "<style>body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#1a1a2e;color:#eee;}";
  html += "h3{margin:4px 0;} .card{background:rgba(22,33,62,.9);border-radius:12px;padding:12px;margin:10px 0;}";
  html += "label{font-size:14px;color:#9ab;display:block;margin-bottom:6px;}";
  html += "input[type=file]{color:#eee;font-size:14px;}";
  html += "button{font-size:16px;padding:8px 20px;border:none;border-radius:8px;background:#0f3460;color:#eee;cursor:pointer;margin-top:8px;}";
  html += ".btn-cancel{background:#555;margin-left:6px;}";
  html += "a{color:#9ab;} #cropModal{display:none;position:fixed;top:0;left:0;width:100%;height:100%;background:rgba(0,0,0,.85);z-index:999;padding:20px;box-sizing:border-box;}";
  html += "#cropContainer{max-width:400px;margin:0 auto;background:#16213e;border-radius:12px;padding:16px;} #cropPreview{max-width:100%;margin-bottom:12px;display:block;}</style></head><body>";
  html += "<h3>上传图片</h3>";
  html += "<p style='font-size:13px;color:#9ab'>上传后设备自动重启生效</p>";

  // 网页壁纸区(直传)
  html += "<div class='card'><b>网页壁纸</b><p style='font-size:12px;color:#9ab;margin:4px 0'>根据设备尺寸自选，无需裁剪</p>";
  html += "<form method='POST' action='/do_upload' enctype='multipart/form-data'>";
  html += "<input type='hidden' name='fname' value='m.jpg'>";
  html += "<label>选择图片（将保存为 m.jpg）</label>";
  html += "<input type='file' name='file' accept='image/*'><br>";
  html += "<button type='submit'>上传并重启</button></form></div>";

  // 城市数据区(JSON直传)
  html += "<div class='card'><b>城市数据</b><p style='font-size:12px;color:#9ab;margin:4px 0'>上传城市代码JSON文件，用于城市查询功能</p>";
  html += "<form method='POST' action='/do_upload' enctype='multipart/form-data'>";
  html += "<input type='hidden' name='fname' value='city_data.json'>";
  html += "<label>选择JSON文件（将保存为 city_data.json）</label>";
  html += "<input type='file' name='file' accept='.json,application/json'><br>";
  html += "<button type='submit'>上传并重启</button></form></div>";

  // 相册区(前端裁剪240x240)
  html += "<div class='card'><b>相册图片</b><p style='font-size:12px;color:#9ab;margin:4px 0'>屏幕尺寸 240×240，选图后可裁剪</p>";
  for (int i = 1; i <= 3; i++) {
    html += "<div style='margin-top:10px;border-top:1px solid #334;padding-top:8px'><label>图片" + String(i) + "（" + String(i) + ".jpg）</label>";
    html += "<input type='file' id='f" + String(i) + "' accept='image/*' style='margin-bottom:8px'>";
    html += "<button onclick='startCrop(" + String(i) + ")'>选图并裁剪</button></div>";
  }
  html += "</div>";

  // 裁剪弹窗
  html += "<div id='cropModal'><div id='cropContainer'>";
  html += "<h4 style='margin-top:0;color:#eee'>调整裁剪区域</h4>";
  html += "<img id='cropPreview'>";
  html += "<div style='margin-top:12px'><button onclick='uploadCropped()'>确认并上传</button>";
  html += "<button class='btn-cancel' onclick='cancelCrop()'>取消</button></div></div></div>";

  html += "<p><a href='/'>返回</a></p>";
  html += "<script src='https://cdn.jsdelivr.net/npm/cropperjs@1.6.1/dist/cropper.min.js'></script>";
  html += "<script>let cropper,curIdx;";
  html += "function startCrop(i){curIdx=i;const inp=document.getElementById('f'+i);const f=inp.files[0];";
  html += "if(!f){alert('请先选择图片');return;}const r=new FileReader();r.onload=e=>{";
  html += "const img=document.getElementById('cropPreview');img.src=e.target.result;";
  html += "document.getElementById('cropModal').style.display='block';";
  html += "if(cropper)cropper.destroy();setTimeout(()=>{cropper=new Cropper(img,{aspectRatio:1,viewMode:1,autoCropArea:1});},100);};r.readAsDataURL(f);}";
  html += "function cancelCrop(){document.getElementById('cropModal').style.display='none';if(cropper)cropper.destroy();}";
  html += "function uploadCropped(){if(!cropper){alert('裁剪未就绪');return;}cropper.getCroppedCanvas({width:240,height:240}).toBlob(blob=>{";
  html += "const fd=new FormData();fd.append('fname',curIdx+'.jpg');fd.append('file',blob,curIdx+'.jpg');";
  html += "document.getElementById('cropModal').style.display='none';showUploadMsg('上传中...','#eee');";
  // ★ 必须把响应显示出来：上传失败时设备返回 400 + 具体原因（超限/不完整/替换失败…）。
  //   旧写法 .then(r=>r.text()).then(()=>{}) 把响应丢掉了 —— 一旦失败，
  //   页面会永远停在"上传中..."，用户看不到任何提示。
  html += "fetch('/do_upload',{method:'POST',body:fd})";
  html += ".then(r=>r.text()).then(t=>{document.body.innerHTML=t;})";
  html += ".catch(e=>{showUploadMsg('上传中断: '+(e&&e.message?e.message:e),'#f87171');});";
  html += "},'image/jpeg',0.9);}";
  html += "function showUploadMsg(t,c){document.body.innerHTML='<div style=\"text-align:center;padding:60px;background:#1a1a2e;color:#eee;min-height:100vh\"><h3 style=\"color:'+c+'\">'+t+'</h3><p><a href=\"/upload\" style=\"color:#38bdf8\">返回重试</a></p></div>';}";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

// 股票编辑页
void handleStockEdit() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>更换股票</title>";
  html += "<style>body{font-family:sans-serif;max-width:480px;margin:0 auto;padding:16px;background:#1a1a2e;color:#eee;}";
  html += "input{font-size:16px;padding:8px;border:1px solid #555;border-radius:6px;background:#222;color:#eee;}";
  html += ".code{width:140px;margin-right:8px;} .label{width:80px;}";
  html += "button{font-size:18px;padding:10px 24px;border:none;border-radius:8px;background:#0f3460;color:#eee;cursor:pointer;margin-top:12px;}";
  html += ".hint{font-size:13px;color:#9ab;margin-top:4px;}</style></head><body>";
  html += "<h3>更换股票代码</h3>";
  html += "<p style='font-size:14px;color:#9ab'>前缀说明：<b>sh</b>=上海A股，<b>sz</b>=深圳A股，<b>hk</b>=香港股票<br>";
  html += "名称用拼音首字母，如：GZMT(贵州茅台)，ZJXC(中际旭创)</p>";
  html += "<form method='GET' action='/stock_save'>";
  for (int i = 0; i < SYMBOL_COUNT; i++) {
    html += "<p>股票" + String(i + 1) + ": ";
    html += "<input class='code' name='c" + String(i) + "' value='" + SYMBOLS[i].code + "' placeholder='代码' maxlength='12'> ";
    html += "<input class='label' name='l" + String(i) + "' value='" + SYMBOLS[i].label + "' placeholder='拼音' maxlength='8'>";
    html += "<div class='hint'>当前: " + String(SYMBOLS[i].code) + " / " + String(SYMBOLS[i].label) + "</div></p>";
  }
  html += "<button type='submit'>保存并重启</button>";
  html += "</form><p><a href='/' style='color:#9ab'>返回</a></p></body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

// 保存股票代码+名称到 NVS，重启生效
void handleStockSave() {
  Serial.println("=== 收到股票保存请求 ===");
  for (int i = 0; i < SYMBOL_COUNT; i++) {
    String argC = "c" + String(i);
    String argL = "l" + String(i);
    Serial.printf("检查参数 %s=%s, %s=%s\n", argC.c_str(), server.arg(argC).c_str(), argL.c_str(), server.arg(argL).c_str());
    if (server.hasArg(argC) && server.hasArg(argL)) {
      String code = server.arg(argC);
      String label = server.arg(argL);
      code.trim();
      label.trim();
      if (code.length() > 0 && label.length() > 0) {
        prefs.putString(("stock" + String(i)).c_str(), code);
        prefs.putString(("label" + String(i)).c_str(), label);
        Serial.printf("已保存 stock%d=%s, label%d=%s\n", i, code.c_str(), i, label.c_str());
      } else {
        Serial.printf("跳过 %d：code 或 label 为空\n", i);
      }
    } else {
      Serial.printf("跳过 %d：参数不存在\n", i);
    }
  }
  server.send(200, "text/html; charset=utf-8",
    "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px'>"
    "<h3>已保存，设备将在 2 秒后重启</h3><p>重启后新股票生效</p></body></html>");
  delay(2000);
  ESP.restart();
}

// WiFi 重置
void handleResetWiFi() {
  server.send(200, "text/html; charset=utf-8",
    "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px'>"
    "<h3>WiFi 配置已清除</h3>"
    "<p>设备将在 2 秒后重启并进入配网模式（AP 热点）</p>"
    "<p style='color:#fbbf24;margin-top:20px'>请在设备重启后（约 5 秒），手动连接热点<br><strong>SDD小电视</strong><br>再打开 <strong>192.168.4.1</strong> 重新配网</p>"
    "</body></html>");
  delay(2000);
  prefs.clear();                // 清除本固件的 Preferences 命名空间（自选股/城市/亮度等）
  WiFi.disconnect(true, true);  // ⚠️ WiFi 凭据不在 Preferences 里，由这一行擦除（第2个 true = eraseap）
  delay(100);
  ESP.restart();
}

// WiFi 更换页面(表单)
void handleChangeWiFi() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>更换 WiFi</title>";
  html += "<style>";
  html += "body{font-family:sans-serif;background:#1a1a2e;color:#eee;max-width:400px;margin:40px auto;padding:20px;}";
  html += "h3{text-align:center;margin-bottom:24px;}";
  html += "label{display:block;margin-top:16px;font-size:14px;color:#9ab;}";
  html += "input{width:100%;padding:10px;margin-top:4px;border:1px solid #456;border-radius:6px;background:#0f3460;color:#eee;font-size:16px;box-sizing:border-box;}";
  html += "button{width:100%;margin-top:24px;padding:12px;border:none;border-radius:8px;background:#16a34a;color:#fff;font-size:18px;cursor:pointer;}";
  html += "button:active{background:#15803d;}";
  html += ".back{margin-top:12px;background:#475569;}";
  html += ".back:active{background:#334155;}";
  html += "</style></head><body>";
  html += "<h3>更换 WiFi</h3>";
  html += "<form action='/do_change_wifi' method='GET'>";
  html += "<label>新 WiFi 名称 (SSID)</label>";
  html += "<input type='text' name='ssid' placeholder='输入WiFi名称' required>";
  html += "<label>新 WiFi 密码</label>";
  html += "<input type='password' name='pass' placeholder='输入密码' required>";
  html += "<button type='submit'>连接并保存</button>";
  html += "</form>";
  html += "<button class='back' onclick=\"location.href='/'\">返回</button>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

// WiFi 更换执行(安全连接测试)
void handleDoChangeWiFi() {
  if (!server.hasArg("ssid") || !server.hasArg("pass")) {
    server.send(400, "text/html; charset=utf-8",
      "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px'>"
      "<h3>参数错误</h3><p><a href='/change_wifi' style='color:#38bdf8'>返回重试</a></p></body></html>");
    return;
  }

  String newSSID = server.arg("ssid");
  String newPass = server.arg("pass");
  newSSID.trim();
  newPass.trim();

  // 先返回页面(告知正在连接)
  server.send(200, "text/html; charset=utf-8",
    "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px'>"
    "<h3>正在连接新 WiFi...</h3>"
    "<p style='color:#fbbf24'>请等待 30 秒<br>成功后设备将自动重启</p>"
    "<p style='font-size:14px;color:#9ab;margin-top:20px'>如果连接失败，旧 WiFi 保持不变</p>"
    "</body></html>");

  delay(500);  // 确保页面发送完成

  // 测试连接新 WiFi
  WiFi.disconnect();
  WiFi.begin(newSSID.c_str(), newPass.c_str());
  Serial.printf("尝试连接新 WiFi: %s\n", newSSID.c_str());

  int timeout = 30;
  while (WiFi.status() != WL_CONNECTED && timeout > 0) {
    delay(1000);
    Serial.print(".");
    timeout--;
  }

  if (WiFi.status() == WL_CONNECTED) {
    Serial.printf("\n新 WiFi 连接成功: %s\n", WiFi.localIP().toString().c_str());
    // 保存新 WiFi(WiFiManager 会在下次 autoConnect 时读取)
    // 注意: WiFi.begin 成功后 ESP32 会自动保存凭据到 NVS
    delay(1000);
    ESP.restart();  // 重启生效
  } else {
    Serial.println("\n新 WiFi 连接失败，恢复旧连接");
    // 恢复失败，重新连接旧 WiFi
    WiFi.disconnect();
    setupWifi();  // 重新走 WiFiManager 连接已保存的旧 WiFi
  }
}

// 城市代码查询页面(带搜索功能，从SPIFFS加载city_data.json)
void handleCityList() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>城市代码查询</title>";
  html += "<style>";
  html += "body{font-family:sans-serif;background:#1a1a2e;color:#eee;max-width:600px;margin:20px auto;padding:20px;}";
  html += "h3{text-align:center;margin-bottom:20px;}";
  html += "#search{width:100%;padding:12px;margin-bottom:16px;border:1px solid #456;border-radius:8px;background:#0f3460;color:#eee;font-size:16px;box-sizing:border-box;}";
  html += "#count{text-align:center;color:#9ab;margin-bottom:12px;font-size:14px;}";
  html += "table{width:100%;border-collapse:collapse;background:rgba(22,33,62,.9);border-radius:8px;overflow:hidden;}";
  html += "th,td{padding:10px;text-align:left;border-bottom:1px solid #456;}";
  html += "th{background:#0f3460;color:#9ab;font-weight:bold;position:sticky;top:0;}";
  html += "tr:last-child td{border-bottom:none;}";
  html += "tr.hidden{display:none;}";
  html += "a{color:#38bdf8;text-decoration:none;}";
  html += ".back{text-align:center;margin-top:20px;}";
  html += ".loading{text-align:center;padding:40px;color:#9ab;}";
  html += "</style></head><body>";
  html += "<h3>城市代码查询</h3>";
  html += "<input type='text' id='search' placeholder='输入城市名称或代码搜索...'>";
  html += "<div id='count'></div>";
  html += "<div id='loading' class='loading'>加载中...</div>";
  html += "<table id='cityTable' style='display:none'>";
  html += "<thead><tr><th>城市</th><th>代码</th></tr></thead>";
  html += "<tbody id='cityBody'></tbody>";
  html += "</table>";
  html += "<p class='back'><a href='/'>返回控制台</a></p>";
  html += "<script>";
  html += "let cities={};let total=0;";
  html += "fetch('/city_data.json').then(r=>r.json()).then(data=>{";
  html += "cities=data;total=Object.keys(data).length;";
  html += "const tbody=document.getElementById('cityBody');";
  html += "for(const[city,code]of Object.entries(data)){";
  html += "const tr=document.createElement('tr');tr.dataset.city=city;tr.dataset.code=code;";
  html += "tr.innerHTML=`<td>${city}</td><td>${code}</td>`;tbody.appendChild(tr);}";
  html += "document.getElementById('loading').style.display='none';";
  html += "document.getElementById('cityTable').style.display='table';";
  html += "updateCount();";
  html += "}).catch(()=>{document.getElementById('loading').innerHTML='<span style=\"color:#f87171\">加载失败，请确保已上传 city_data.json</span>';});";
  html += "document.getElementById('search').addEventListener('input',e=>{";
  html += "const q=e.target.value.toLowerCase();";
  html += "const rows=document.querySelectorAll('#cityBody tr');";
  html += "let visible=0;";
  html += "rows.forEach(row=>{";
  html += "const city=row.dataset.city.toLowerCase();const code=row.dataset.code;";
  html += "if(city.includes(q)||code.includes(q)){row.classList.remove('hidden');visible++;}";
  html += "else{row.classList.add('hidden');}});";
  html += "updateCount(visible);});";
  html += "function updateCount(v){const c=v===undefined?total:v;";
  html += "document.getElementById('count').textContent=`显示 ${c} / ${total} 个城市`;}";
  html += "</script>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

// SPIFFS 存储容量统计（只有一个容量页，不列出文件名）
void handleSPIFFSList() {
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>SPIFFS 存储信息</title>";
  html += "<style>";
  html += "body{font-family:sans-serif;background:#1a1a2e;color:#eee;max-width:600px;margin:20px auto;padding:20px;}";
  html += "h3{text-align:center;margin-bottom:30px;}";
  html += ".info{background:rgba(22,33,62,.9);border-radius:12px;padding:20px;text-align:center;}";
  html += ".stat{font-size:18px;margin:12px 0;color:#9ab;}";
  html += ".stat b{color:#eee;font-size:24px;}";
  html += "a{color:#38bdf8;text-decoration:none;}";
  html += ".back{text-align:center;margin-top:30px;}";
  html += "</style></head><body>";
  html += "<h3>SPIFFS 存储信息</h3>";

  // 统计信息
  size_t totalBytes = SPIFFS.totalBytes();
  size_t usedBytes = SPIFFS.usedBytes();
  size_t freeBytes = totalBytes - usedBytes;
  int usedPercent = usedBytes * 100 / totalBytes;

  html += "<div class='info'>";
  html += "<div class='stat'>总容量: <b>" + String(totalBytes / 1024) + " KB</b></div>";
  html += "<div class='stat'>已使用: <b>" + String(usedBytes / 1024) + " KB</b> (" + String(usedPercent) + "%)</div>";
  html += "<div class='stat'>剩余空间: <b>" + String(freeBytes / 1024) + " KB</b></div>";
  html += "</div>";

  html += "<p class='back'><a href='/'>返回控制台</a></p>";
  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

// ============================================================
//  时钟模式
// ============================================================
int lastH = -1, lastM = -1, lastS = -1, lastD = -1;   // 节流(全局, setMode重置强制重绘)

void drawColon() {
  tft.fillCircle(114, 74, 4, COL_TEXT);
  tft.fillCircle(114, 102, 4, COL_TEXT);
}

void drawDate() {
  char buf[16];
  snprintf(buf, sizeof(buf), "%d.%d.%d", timeinfo.tm_year + 1900, timeinfo.tm_mon + 1, timeinfo.tm_mday);
  dateSpr.fillSprite(TRANSPARENT);
  dateSpr.setTextColor(COL_TEXT);
  dateSpr.setTextSize(2);
  int w = dateSpr.textWidth(buf);
  dateSpr.setCursor((dateSpr.width() - w) / 2, 0);
  dateSpr.print(buf);
  dateSpr.pushSprite((240 - w) / 2, 166, TRANSPARENT);   // 透明叠加到壁纸
}

// alpha 数字渲染(透明叠加到壁纸, alpha=0->露壁纸, 255->纯色)
void showDigit(int dx, int dy, const uint8_t* alphaMap, uint16_t pure, bool small) {
  TFT_eSprite* spr = small ? &numSprSmall : &numSpr;
  int w = spr->width(), h = spr->height();
  uint16_t* dp = (uint16_t*)spr->getPointer();
  uint16_t* wp = (uint16_t*)wallpaperSpr.getPointer();
  uint16_t pureSwapped = (pure >> 8) | (pure << 8);
  for (int y = 0; y < h; y++) {
    for (int x = 0; x < w; x++) {
      uint8_t a = pgm_read_byte(&alphaMap[y * w + x]);
      dp[y * w + x] = (a == 255) ? pureSwapped : wp[(dy + y) * 240 + (dx + x)];
    }
  }
  spr->pushSprite(dx, dy);
}

void showClockDigit(int x, int y, int n, char style) {
  if (style == 'W')      showDigit(x, y, A_W3660[n], COL_TEXT,   false);
  else if (style == 'O') showDigit(x, y, A_O3660[n], COL_ORANGE, false);
  else                   showDigit(x, y, A_W1830[n], COL_TEXT,   true);
}

void digitalClockDisplay() {
  getLocalTime(&timeinfo, 0);
  int h = timeinfo.tm_hour, m = timeinfo.tm_min, s = timeinfo.tm_sec, d = timeinfo.tm_mday;
  if (h != lastH) { showClockDigit(34, 58, h/10, 'W'); showClockDigit(70, 58, h%10, 'W'); lastH = h; }
  if (m != lastM) { showClockDigit(122, 58, m/10, 'O'); showClockDigit(158, 58, m%10, 'O'); lastM = m; }
  if (s != lastS) { showClockDigit(102, 128, s/10, 'w'); showClockDigit(120, 128, s%10, 'w'); lastS = s; }
  if (d != lastD) {
    wallpaperSpr.pushSprite(40, 166, 40, 166, 160, 24);   // 恢复日期区壁纸
    drawDate();
    lastD = d;
  }
}

void renderClock() {
  digitalClockDisplay();
}

// ============================================================
//  天气模式
// ============================================================
uint16_t tempColor(float t) {
  if (t < 0)   return 0x001F;
  if (t < 18)  return 0x07FF;
  if (t < 24)  return 0x07E0;
  if (t < 31)  return 0xFFE0;
  return 0xF800;
}
uint16_t aqiColor(int aqi) {
  if (aqi <= 50)  return 0x07E0;
  if (aqi <= 100) return 0xFFE0;
  if (aqi <= 150) return 0xFD20;
  if (aqi <= 200) return 0xF81F;
  return 0xF800;
}

bool fetchWeather() {
  char url[96];
  snprintf(url, sizeof(url), "http://d1.weather.com.cn/weather_index/%s.html?_=%ld",
           cityCode.c_str(), (long)millis());
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(url);
  http.addHeader("Referer", "http://www.weather.com.cn/");
  http.setUserAgent("Mozilla/5.0 (iPhone; CPU iPhone OS 11_0 like Mac OS X)");
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String str = http.getString();
  http.end();
  int s = str.indexOf("dataSK =");
  int e = str.indexOf(";var dataZS");
  if (s < 0 || e < 0) return false;
  String jsonSK = str.substring(s + 8, e);
  StaticJsonDocument<1024> doc;
  if (deserializeJson(doc, jsonSK)) return false;
  w.temp  = doc["temp"].as<float>();
  w.humi  = atoi(doc["SD"].as<String>().c_str());
  w.press = doc["qy"].as<int>();
  w.aqi   = doc["aqi"].as<int>();
  String wc = doc["weathercode"].as<String>();
  w.code  = atoi(wc.substring(1, 3).c_str());
  w.ok = true;
  return true;
}

// 提取 ~ 分隔的第 n 个字段
String getField(String& data, int n) {
  int start = 0;
  for (int i = 0; i < n; i++) {
    int sep = data.indexOf('~', start);
    if (sep < 0) return "";
    start = sep + 1;
  }
  int sep = data.indexOf('~', start);
  if (sep < 0) return data.substring(start);
  return data.substring(start, sep);
}

// 抓行情(腾讯文本接口)
bool fetchQuote(const char* symbol, Quote& q) {
  char url[96];
  snprintf(url, sizeof(url), "http://qt.gtimg.cn/q=%s", symbol);
  HTTPClient http;
  http.setTimeout(5000);
  http.begin(url);
  http.setUserAgent("Mozilla/5.0");
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String str = http.getString();
  http.end();

  int s = str.indexOf('"');
  int e = str.lastIndexOf('"');
  if (s < 0 || e <= s) return false;
  String data = str.substring(s + 1, e);   // 1~贵州茅台~600519~1341.99~...

  q.price     = getField(data, 3).toFloat();
  q.prevClose = getField(data, 4).toFloat();
  q.change    = getField(data, 31).toFloat();
  q.changePct = getField(data, 32).toFloat();
  q.high      = getField(data, 33).toFloat();
  q.low       = getField(data, 34).toFloat();
  q.ok = true;
  return true;
}

// 抓日K(HTTPS, 前复权) -> kl[30][4]
bool fetchKline(const char* symbol, Quote& q) {
  char url[128];
  snprintf(url, sizeof(url), "https://web.ifzq.gtimg.cn/appstock/app/fqkline/get?param=%s,day,,,%d,qfq",
           symbol, KLINE_COUNT);

  WiFiClientSecure client;
  client.setInsecure();

  HTTPClient http;
  http.setTimeout(8000);
  http.begin(client, url);
  http.addHeader("Referer", "http://gu.qq.com/");
  http.setUserAgent("Mozilla/5.0");
  int code = http.GET();
  if (code != 200) { http.end(); return false; }
  String str = http.getString();
  http.end();
  client.stop();   // 释放 TLS 内存

  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, str);
  if (err) return false;

  JsonArray arr = doc["data"][symbol]["qfqday"];        // 股票: 前复权
  if (arr.isNull()) arr = doc["data"][symbol]["day"];   // 指数: 无复权用 day
  if (arr.isNull()) return false;

  q.klCount = 0;
  for (JsonVariant v : arr) {
    if (q.klCount >= KLINE_COUNT) break;
    q.kl[q.klCount][0] = v[1].as<float>();   // 开
    q.kl[q.klCount][1] = v[2].as<float>();   // 收
    q.kl[q.klCount][2] = v[3].as<float>();   // 高
    q.kl[q.klCount][3] = v[4].as<float>();   // 低
    q.klCount++;
  }
  q.klOk = q.klCount > 0;
  return q.klOk;
}

// 手动 HTTPS GET (HTTP/1.0 + Connection: close, 读到断开为止)
// HTTPClient 发 HTTP/1.1, 服务器回 chunked 分块; 其对 HTTPS+chunked 大响应(>4KB)会截断,
// 导致分时 JSON 不完整。改发 HTTP/1.0, 服务器回 Connection: close 不分块, 直接读到断开即可
bool httpsGet10(const String& url, String& out) {
  int hs = url.indexOf("://") + 3;
  int ps = url.indexOf('/', hs);
  String host = (ps < 0) ? url.substring(hs) : url.substring(hs, ps);
  String path = (ps < 0) ? "/" : url.substring(ps);

  WiFiClientSecure client;
  client.setInsecure();
  client.setTimeout(8000);
  if (!client.connect(host.c_str(), 443)) return false;

  client.print("GET " + path + " HTTP/1.0\r\n");
  client.print("Host: " + host + "\r\n");
  client.print("User-Agent: Mozilla/5.0\r\n");
  client.print("Referer: http://gu.qq.com/\r\n");
  client.print("\r\n");

  // 跳过响应头(读到空行)
  while (client.connected()) {
    String line = client.readStringUntil('\n');
    if (line.length() <= 2) break;
  }

  // 读 body: 服务器 Connection: close, 读到断开; 关闭后多等片刻确认无残留 SSL 记录
  out = "";
  out.reserve(13000);
  uint8_t buf[256];
  unsigned long lastData = millis();
  while (millis() - lastData < 8000) {
    int avail = client.available();
    if (avail > 0) {
      int n = client.read(buf, (avail < (int)sizeof(buf)) ? avail : (int)sizeof(buf));
      if (n > 0) {
        out.concat((char*)buf, n);
        lastData = millis();
      }
    } else if (!client.connected()) {
      delay(20);                        // 等最后一段 SSL 记录解密
      if (client.available() == 0) break;
    }
    delay(1);
  }
  client.stop();

  // ── 找 JSON 起点，裁掉前面的垃圾 ──────────────────────────────────────────
  //   现象：上面那个"跳过响应头"的循环【并不可靠】—— 实测字符串里仍带着 HTTP 头，
  //         于是 deserializeJson 直接报 InvalidInput。
  //   为什么不去追根因：要追得烧一轮固件去试，而**这个兜底本来就该存在** ——
  //         找 JSON 起点这个判据【与原因无关】，两条路（头没跳净 / readStringUntil
  //         超时返回空串）都能被它救。所以是有意的"不追根因，只兜底"。
  int brace = out.indexOf('{');
  if (brace > 0) {
    Serial.printf("[https10] 头部未跳净，裁掉前 %d 字节\n", brace);
    out.remove(0, brace);
  }
  return out.length() > 0;
}

// 抓分时(HTTPS, HTTP/1.0 不分块) -> 均匀采样 spark[48]
bool fetchSpark(const char* symbol, Quote& q) {
  char url[128];
  snprintf(url, sizeof(url), "https://web.ifzq.gtimg.cn/appstock/app/minute/query?code=%s", symbol);

  String str;
  if (!httpsGet10(url, str)) {
    Serial.printf("[spark] %s httpGet fail len=%d\n", symbol, (int)str.length());
    return false;
  }

  DynamicJsonDocument doc(24576);
  DeserializationError err = deserializeJson(doc, str);
  if (err) {
    Serial.printf("[spark] %s json err=%s strLen=%d\n", symbol, err.c_str(), (int)str.length());
    return false;
  }

  JsonArray arr = doc["data"][symbol]["data"]["data"];
  if (arr.isNull()) {
    Serial.printf("[spark] %s arr null strLen=%d\n", symbol, (int)str.length());
    return false;
  }

  static float prices[241];   // 静态, 避免栈溢出
  int cnt = 0;
  for (JsonVariant v : arr) {
    if (cnt >= 241) break;
    String line = v.as<String>();   // "0930 1355.00 227 30758500.00"
    int sp1 = line.indexOf(' ');
    int sp2 = line.indexOf(' ', sp1 + 1);
    if (sp1 < 0 || sp2 < 0) continue;
    prices[cnt++] = line.substring(sp1 + 1, sp2).toFloat();
  }
  if (cnt < 2) {
    Serial.printf("[spark] %s cnt=%d\n", symbol, cnt);
    return false;
  }

  q.sparkLen = SPARK_POINTS;
  for (int i = 0; i < SPARK_POINTS; i++) {
    q.spark[i] = prices[(int)((long)i * cnt / SPARK_POINTS)];
  }
  q.sparkOk = true;
  return true;
}

void drawWeather() {
  char buf[24];
  if (!w.ok) {
    textSpr.fillSprite(TRANSPARENT);
    textSpr.setTextColor(COL_DIM);
    textSpr.setTextSize(2);
    textSpr.drawCentreString("No Weather Data", 80, 4, 2);
    textSpr.pushSprite(40, 100, TRANSPARENT);
    return;
  }

  // 天气图标(色键: 黑底透明)
  TJpgDec.setCallback(tft_output_icon);
  wrat.printfweather(90, 12, w.code);
  TJpgDec.setCallback(tft_output);

  // 温度大字(透明)
  snprintf(buf, sizeof(buf), "%.1f\140C", w.temp);
  tempSpr.fillSprite(TRANSPARENT);
  tempSpr.setTextFont(2);
  tempSpr.setTextSize(2);
  tempSpr.setTextColor(tempColor(w.temp));
  tempSpr.setCursor(0, 0);
  tempSpr.print(buf);
  int tw = tempSpr.textWidth(buf);
  tempSpr.pushSprite((240 - tw) / 2, 78, TRANSPARENT);

  // 城市名(透明)
  textSpr.fillSprite(TRANSPARENT);
  textSpr.setTextFont(1);
  textSpr.setTextSize(3);
  textSpr.setTextColor(COL_TEXT);
  textSpr.setCursor(0, 0);
  textSpr.print(cityLabel);
  int cw = textSpr.textWidth(cityLabel.c_str());
  textSpr.pushSprite((240 - cw) / 2, 116, TRANSPARENT);

  // 湿度图标(色键透明)
  TJpgDec.setCallback(tft_output_icon);
  TJpgDec.drawJpg(51, 168, humidity, sizeof(humidity));
  TJpgDec.setCallback(tft_output);

  // 湿度%(透明)
  snprintf(buf, sizeof(buf), "%d%%", w.humi);
  textSpr.fillSprite(TRANSPARENT);
  textSpr.setTextFont(2);
  textSpr.setTextSize(1);
  textSpr.setTextColor(COL_TEXT);
  textSpr.setCursor(0, 4);
  textSpr.print(buf);
  textSpr.pushSprite(79, 171, TRANSPARENT);

  // 气压(透明)
  snprintf(buf, sizeof(buf), "%dhPa", w.press);
  textSpr.fillSprite(TRANSPARENT);
  textSpr.setTextFont(2);
  textSpr.setTextSize(1);
  textSpr.setTextColor(COL_TEXT);
  textSpr.setCursor(0, 4);
  textSpr.print(buf);
  textSpr.pushSprite(129, 171, TRANSPARENT);

  // AQI 点(纯色圆点直接画) + 数值(透明)
  tft.fillCircle(95, 212, 8, aqiColor(w.aqi));
  snprintf(buf, sizeof(buf), "AQI %d", w.aqi);
  textSpr.fillSprite(TRANSPARENT);
  textSpr.setTextFont(2);
  textSpr.setTextSize(1);
  textSpr.setTextColor(COL_TEXT);
  textSpr.setCursor(0, 4);
  textSpr.print(buf);
  textSpr.pushSprite(108, 200, TRANSPARENT);
}

void renderWeather() {
  static unsigned long lastFetch = 0;
  if (!w.ok || millis() - lastFetch >= 600000UL) {   // 首次或10分钟刷新
    // 天气取数逐次耗时（这里要返回值，T_FETCH 那个宏是 do/while 不能当表达式 ⇒ 展开写）
    unsigned long _tw  = millis();
    bool          _wok = fetchWeather();
    Serial.printf("[T] weather - %lums ok=%d\n", millis() - _tw, (int)_wok);
    if (_wok) {
      wallpaperSpr.pushSprite(0, 0);   // 恢复壁纸(清掉旧天气信息)
      drawWeather();
      lastFetch = millis();
    }
  }
}

// ============================================================
//  相册模式
// ============================================================
void drawPhoto(int idx) {
  if (SPIFFS.exists(PHOTOS[idx])) {
    TJpgDec.drawFsJpg(0, 0, PHOTOS[idx]);
  } else {
    tft.fillScreen(COL_BG);
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.drawCentreString("No Photo", 120, 110, 2);
  }
}

void renderPhoto() {
  if (millis() - lastRotate >= 5000UL) {
    int next = (curIdx + 1) % 3;
    drawPhoto(next);
    curIdx = next;
    lastRotate = millis();
  }
}

// ============================================================
//  股票模式(第一版布局: 多股轮动 + 分时/日K双视图)
// ============================================================

// 分时折线 sparkline
void drawSpark(const Quote& q, int x, int y, int w, int h, uint16_t col) {
  if (q.sparkLen < 2) return;
  float minV = q.spark[0], maxV = q.spark[0];
  for (int i = 1; i < q.sparkLen; i++) {
    if (q.spark[i] < minV) minV = q.spark[i];
    if (q.spark[i] > maxV) maxV = q.spark[i];
  }
  float range = maxV - minV;
  if (range < 0.001) range = 1;
  for (int i = 1; i < q.sparkLen; i++) {
    int x1 = x + (i - 1) * w / (q.sparkLen - 1);
    int y1 = y + h - (int)((q.spark[i - 1] - minV) / range * h);
    int x2 = x + i * w / (q.sparkLen - 1);
    int y2 = y + h - (int)((q.spark[i] - minV) / range * h);
    chartSpr.drawLine(x1, y1, x2, y2, col);
  }
}

// 日K蜡烛图
void drawKline(const Quote& q, int x, int y, int w, int h) {
  if (q.klCount < 2) return;
  float maxP = q.kl[0][2], minP = q.kl[0][3];
  for (int i = 1; i < q.klCount; i++) {
    if (q.kl[i][2] > maxP) maxP = q.kl[i][2];
    if (q.kl[i][3] < minP) minP = q.kl[i][3];
  }
  float range = maxP - minP;
  if (range < 0.001) range = 1;

  float barW = (float)w / q.klCount;
  int bodyW = (int)(barW * 0.7);
  if (bodyW < 1) bodyW = 1;

  for (int i = 0; i < q.klCount; i++) {
    uint16_t col = (q.kl[i][1] >= q.kl[i][0]) ? 0xF800 : 0x07E0;   // 红涨绿跌
    int yHigh  = y + (int)((maxP - q.kl[i][2]) / range * h);
    int yLow   = y + (int)((maxP - q.kl[i][3]) / range * h);
    int yOpen  = y + (int)((maxP - q.kl[i][0]) / range * h);
    int yClose = y + (int)((maxP - q.kl[i][1]) / range * h);
    int cx = x + (int)(i * barW + barW / 2);

    chartSpr.drawFastVLine(cx, yHigh, yLow - yHigh + 1, col);   // 影线
    int yTop = (yOpen < yClose) ? yOpen : yClose;          // 实体
    int yBot = (yOpen > yClose) ? yOpen : yClose;
    int bodyH = yBot - yTop;
    if (bodyH < 1) bodyH = 1;
    chartSpr.fillRect(cx - bodyW / 2, yTop, bodyW, bodyH, col);
  }
}

// 画单只股票(第一版布局)
// full=true:  全量重绘(切股/切视图/进入模式/数据可用状态变化)
// full=false: 增量刷新(只刷价格/涨跌幅/走势图, 文字不透明背景自覆盖, 走势图离屏原子推, 避免全屏闪烁)
void drawQuote(int idx, bool full) {
  const Sym&   s = SYMBOLS[idx];
  const Quote& q = quotes[idx];
  static bool lastOk[SYMBOL_COUNT] = {false, false, false, false};

  bool okChanged = (q.ok != lastOk[idx]);
  if (full || okChanged) tft.fillScreen(COL_BG);
  lastOk[idx] = q.ok;

  if (!q.ok) {
    if (full || okChanged) {
      tft.setTextColor(COL_DIM, COL_BG);
      tft.setTextSize(2);
      tft.setCursor(8, 100);
      tft.print(s.label);
      tft.setCursor(8, 130);
      tft.print("No Data");
    }
    return;
  }

  uint16_t col = (q.changePct > 0.01) ? 0xF800 :
                 (q.changePct < -0.01) ? 0x07E0 : 0xBDF7;

  // 名称 + 代码(静态, 仅全量时画)
  if (full || okChanged) {
    tft.setTextColor(COL_TEXT, COL_BG);
    tft.setTextSize(2);
    tft.setCursor(8, 10);
    tft.print(s.label);

    tft.setTextColor(COL_DIM, COL_BG);
    tft.setTextSize(1);
    tft.setCursor(8, 215);
    tft.print(s.code);
  }

  // 价格(不透明背景自覆盖; 长度稳定, 无需清区)
  tft.setTextColor(col, COL_BG);
  tft.setTextSize(4);
  tft.setCursor(8, 45);
  tft.print(q.price, 2);

  // 涨跌幅(带符号, 正负等长自覆盖, 无需清区)
  tft.setTextSize(2);
  tft.setCursor(8, 95);
  if (q.changePct >= 0) tft.print("+");
  tft.print(q.changePct, 2);
  tft.print("%");

  // 走势图(离屏 sprite 原子推, 无闪烁)
  chartSpr.fillSprite(COL_BG);
  if (stockView == 0) drawSpark(q, 0, 0, 224, 80, col);
  else                drawKline(q, 0, 0, 224, 80);
  chartSpr.pushSprite(8, 125);
}

// 股票模式主循环(轮播 + 4股轮询刷新)
void renderStock() {
  unsigned long now = millis();

  // ⚠️⚠️ 【本固件的硬约束】一轮 loop() 最多 1 次取数。
  //   依据（实测）：明文 HTTP 行情 ~0.2s，TLS 分时/日K ~1.5~2s。
  //   未节流时在**同一轮**里 quote+spark(+kline) 连拉 2~3 次 ⇒ 最坏单轮 4s+
  //   —— 这正是"快速切换模式时卡顿"的来源。
  //   ⇒ 任何新增的取数都必须先检查 didFetch，绝不能直接往轮播块里加。
  bool didFetch = false;

  // 行情轮转索引 —— 提到函数级只为可读性（分时②有自己的独立索引 sparkIdx，两者【不共用】）。
  //   ⚠️ 它由 ③ 在每个闸门周期【无条件】推进（在 !didFetch 互斥之外），否则行情会被饿死。
  static int refreshIdx = 0;

  // 轮播: 5 秒翻页
  // ⚠️ 这里【只画，不取数】—— 往这里加取数 = 一轮两次 = 卡顿回归。
  if (now - stockLastRotate >= 5000UL) {
    stockCurIdx = (stockCurIdx + 1) % SYMBOL_COUNT;
    drawQuote(stockCurIdx, true);   // 切股: 全量
    stockLastRotate = now;
  }

  // ── ① 日K按需（独立闸门）──────────────────────────────────────────────────
  //   ① 只给【当前显示的那一只】拉 —— 正是马上要画的那只
  //   ② 自带 TTL（每股一份戳）：日K一天才出一根 ⇒ TTL 给长毫无损失
  //   ③ 受 didFetch 互斥 ⇒ 一轮最多一次取数
  //   【历史】原判据是 `stockView == 1 && refreshIdx == stockCurIdx` ——
  //     两个索引【各自走各自的周期】（15s vs 5s），绝大多数时候对不上
  //     ⇒ 日K 极少被拉 ⇒ 日K图长期空白。改成"跟当前显示股 + TTL"才是对的。
  if (stockView == 1) {
    int i = stockCurIdx;
    if (klineStamp[i] == 0 || now - klineStamp[i] >= KLINE_TTL_MS) {
      if (!didFetch) {
        T_FETCH("kline", SYMBOLS[i].code, fetchKline(SYMBOLS[i].code, quotes[i]));
        klineStamp[i] = now;   // ★尝试即推进：失败也等 TTL，否则失败⇒每轮重试⇒重试风暴
        didFetch = true;
      }
    }
  }

  // ── ② 分时图（独立闸门）──────────────────────────────────────────────────
  //   本版无中转主机 ⇒ 分时【只能】设备自己走 TLS 拉
  //   ⇒ 用 TTL 把它压下来。
  //
  //   ⚠️⚠️ 【两条错路都记在这，别再走】
  //   错法①（实测否定）：跟 `stockCurIdx`（当前显示股）走 —— 它每 5 秒一跳，
  //       跟 120s 的 TTL 不同源 ⇒ 相位不可控 ⇒ 4 笔 spark 挤在 25 秒内。
  //   错法②（实测否定）：改跟 `refreshIdx` 走 —— 但那个闸门【15 秒一格、60 秒走完 4 只】，
  //       4 只股的相位间隔只有 15s，而 TTL=120s ⇒ 到达点全落在 45 秒的窗口里，照样成批。
  //       实测：spark 相邻间隔中位 15s、平均 30.6s，**31~60s 的间隔 0 次** ⇒ 就是"批量+空窗"。
  //
  //   ★正解★：闸门周期必须 = `SPARK_TTL_MS / SYMBOL_COUNT`，并配【只属于分时的】独立索引。
  //     ⇒ TTL 恰好 = SYMBOL_COUNT 个闸门周期 ⇒ 每只股每 4 个周期才到点，相位天然均匀错开。
  //       推演：闸门 t=0/30/60/90/120…，索引 0/1/2/3/0… ⇒ 首轮盖满 4 只，
  //       之后每只每 120s 到点一次，落点 0/30/60/90 ⇒ **均匀每 30 秒一笔**。
  //     ⚠️ 这是"周期共振"的【正用】：让 TTL 恰好是【整数个】闸门周期。
  //        反用（要避免的）是让闸门周期与目标个数锁相。
  {
    static unsigned long lastSparkGate = 0;
    static int           sparkIdx       = 0;
    if (now - lastSparkGate >= SPARK_TTL_MS / SYMBOL_COUNT) {
      if (!didFetch) {
        int i = sparkIdx;
        if (sparkStamp[i] == 0 || now - sparkStamp[i] >= SPARK_TTL_MS) {
          T_FETCH("spark", SYMBOLS[i].code, fetchSpark(SYMBOLS[i].code, quotes[i]));
          sparkStamp[i] = now;   // ★尝试即推进（同上）
          didFetch = true;
        }
        // ⚠️ 索引只在【真正轮过】时推进：若这批被 didFetch 挡下，下次仍轮同一只，
        //    否则那只股会被永久跳过（"看起来完全正常"的饿死）。
        sparkIdx = (sparkIdx + 1) % SYMBOL_COUNT;
      }
      // ⚠️ 闸门【无条件】推进（在 didFetch 互斥之外）：否则被挡下的次数不计入周期，
      //    闸门会被无限推迟 ⇒ 该股永远轮不到。
      lastSparkGate = now;
    }
  }

  // ── ③ 行情轮询（4股轮流）──────────────────────────────────────────────────
  // 每 refreshInterval/SYMBOL_COUNT 秒刷一只（60s/4 ⇒ 15s 一只）
  // ⚠️ 加 !didFetch 互斥：本轮若已为日K/分时取过数，这次就跳过（保"一轮一次"）。
  //    代价是那一格行情晚 1 个周期 —— 与"切换卡顿"相比不值一提。
  //    ⚠️ lastRefresh 的推进【在互斥之外】：否则被跳过的次数不计入周期，
  //       行情会被永久饿死（闸门与目标同源 = 锁相）。
  static unsigned long lastRefresh = 0;
  if (now - lastRefresh >= (unsigned long)refreshInterval * 1000 / SYMBOL_COUNT) {
    if (!didFetch) {
      T_FETCH("quote", SYMBOLS[refreshIdx].code, fetchQuote(SYMBOLS[refreshIdx].code, quotes[refreshIdx]));
      didFetch = true;
    }
    if (refreshIdx == stockCurIdx) drawQuote(stockCurIdx, false);   // 刷当前股: 增量
    refreshIdx = (refreshIdx + 1) % SYMBOL_COUNT;
    lastRefresh = now;
  }
}

// ============================================================
//  模式分发
// ============================================================
void renderCurrentMode() {
  switch (currentMode) {
    case MODE_CLOCK:   renderClock();   break;
    case MODE_WEATHER: renderWeather(); break;
    case MODE_PHOTO:   renderPhoto();   break;
    case MODE_STOCK:   renderStock();   break;
  }
}

void setMode(Mode m) {
  currentMode = m;
  prefs.putInt("defaultMode", m);
  tft.fillScreen(TFT_BLACK);
  // 模式进入初始化
  switch (m) {
    case MODE_CLOCK:
      lastH = lastM = lastS = lastD = -1;   // 重置节流, 强制全量重绘
      if (!chartSprFreed) { chartSpr.deleteSprite(); chartSprFreed = true; }   // 腾 35KB
      freeSmallSprites();                   // 腾 32KB(小 sprite 可能没释放)
      ensureWallpaper();                    // 先重建大块壁纸(此时堆最充裕)
      ensureSmallSprites();                 // 再重建小 sprite
      showWallpaper();                      // 根据壁纸模式显示背景
      drawColon();
      digitalClockDisplay();
      break;
    case MODE_WEATHER:
      w.ok = false;              // 强制下次抓取
      if (!chartSprFreed) { chartSpr.deleteSprite(); chartSprFreed = true; }
      freeSmallSprites();
      ensureWallpaper();         // 先重建大块壁纸
      ensureSmallSprites();      // 再重建小 sprite
      showWallpaper();           // 根据壁纸模式显示背景
      renderWeather();
      break;
    case MODE_PHOTO:
      drawPhoto(curIdx);
      lastRotate = millis();
      break;
    case MODE_STOCK:
      stockCurIdx = 0;
      stockLastRotate = millis();
      if (!wallpaperFreed) { wallpaperSpr.deleteSprite(); wallpaperFreed = true; }   // 腾 115KB 给 HTTPS
      freeSmallSprites();        // 股票不用小 sprite, 再腾 32KB
      ensureChart();             // 重建图表缓冲(可能被时钟/天气释放)
      drawQuote(0, true);
      break;
  }
}

// ============================================================
void setup() {
  Serial.begin(115200);
  delay(500);
  Serial.println("\n=== SDD 小电视 整合版 (Step2) ===");

  setBacklight();
  tft.init();
  tft.setRotation(0);
  tft.invertDisplay(true);
  tft.fillScreen(TFT_BLACK);

  prefs.begin("sdd", false);
  loadConfig();
  setBrightness(brightness);  // 应用从NVS加载的亮度

  // 打印【实际生效】的 refreshInterval —— 烧录后当场核对
  // ⚠️ 必须放在 loadConfig() 之后：refreshInterval 是那里从 NVS 读出来的。
  Serial.println("=====================================================");
#if CMP_BUILD
  Serial.printf("[FW] %s  (= 诊断构建)\n", FW_TAG);
#else
  Serial.printf("[FW] %s  (= 发布构建)\n", FW_TAG);
#endif
  Serial.printf("[FW] 取数节流: 一轮一次 + sparkTTL=%lus + klineTTL=%lus\n",
                SPARK_TTL_MS / 1000UL, KLINE_TTL_MS / 1000UL);
#if CMP_BUILD
  Serial.println("[FW] 心跳新字段: loopPeakMs = 本窗口【单轮最长】耗时（loopMs 是平均值，看不见卡顿）");
  Serial.println("[FW] 新增逐次取数耗时: [T] <类型> <代码> <ms>ms ok=<0|1>  （≥200ms 的 handleClient/亮度/壁纸也打）");
#endif
  Serial.printf("[FW] refreshInterval=%d 秒 ⇒ 每只股票 %.2f 秒取一次 (SYMBOL_COUNT=%d)\n",
                refreshInterval, refreshInterval / (float)SYMBOL_COUNT, SYMBOL_COUNT);
  Serial.println("[FW] 提示: 刷新间隔可在网页改，或直接访问 http://<设备IP>/set?refresh=N");
  Serial.println("=====================================================");

  // 配网
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("配网中...", 120, 100, 2);
  setupWifi();

  // SPIFFS(相册)
  if (!SPIFFS.begin(true)) Serial.println("SPIFFS 挂载失败");
  cleanupSPIFFS();  // 清理无用文件 + 恢复上次中断的文件替换，保持存储干净

  // sprite
  numSpr.setColorDepth(16);
  numSpr.createSprite(36, 60);
  numSprSmall.setColorDepth(16);
  numSprSmall.createSprite(18, 30);
  wallpaperSpr.setColorDepth(16);
  wallpaperSpr.createSprite(240, 240);
  dateSpr.setColorDepth(16);
  dateSpr.createSprite(120, 24);
  tempSpr.setColorDepth(16);
  tempSpr.createSprite(160, 40);
  textSpr.setColorDepth(16);
  textSpr.createSprite(160, 24);
  chartSpr.setColorDepth(16);
  chartSpr.createSprite(224, 80);

  // JPEG 解码器
  TJpgDec.setSwapBytes(true);
  TJpgDec.setCallback(tft_output);

  // NTP(时钟)
  configTime(8 * 3600, 0, "ntp.aliyun.com", "ntp6.aliyun.com");
  getLocalTime(&timeinfo, 10000);

  // 开机自动亮度检测：若已开启自动调节，立即按当前时段刷新亮度
  // 避免重启后停留在旧亮度直到下次60秒轮询才生效
  if (autoBrightness) {
    autoAdjustBrightness();
  }

  // WebServer(切模式+壁纸设置+WiFi管理+城市切换)
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/wp_select", handleWallpaperSelect);
  server.on("/m.jpg", HTTP_GET, handleImage);
  server.on("/city_data.json", handleCityDataJSON);
  server.on("/spiffs_list", handleSPIFFSList);  // SPIFFS 存储容量统计（不是文件列表）
  server.on("/upload", handleUploadPage);
  server.on("/stock_edit", handleStockEdit);
  server.on("/stock_save", HTTP_GET, handleStockSave);
  server.on("/change_wifi", handleChangeWiFi);
  server.on("/do_change_wifi", handleDoChangeWiFi);
  server.on("/reset_wifi", handleResetWiFi);
  server.on("/city_list", handleCityList);
  server.on("/do_upload", HTTP_POST, []() {
    // 上传完成回调：检查全局标志，成功才重启
    if (uploadSuccess) {
      server.send(200, "text/html; charset=utf-8",
        "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px'>"
        "<h3>上传完成，设备将在 2 秒后重启</h3></body></html>");
      delay(2000);
      ESP.restart();
    } else {
      server.send(400, "text/html; charset=utf-8",
        "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px'>"
        "<h3 style='color:#f87171'>上传失败: " + uploadError + "</h3>"
        "<p><a href='/upload' style='color:#38bdf8'>返回重试</a></p></body></html>");
    }
  }, []() {
    // 上传处理回调：白名单验证 + 大小限制
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      uploadSuccess = false;
      uploadError = "";

      String targetName = server.arg("fname");
      if (targetName.length() == 0) targetName = upload.filename;

      // 白名单验证（只允许指定文件名）
      const char* ALLOWED_FILES[] = {
        "m.jpg",           // 网页壁纸
        "1.jpg", "2.jpg", "3.jpg",  // 相册
        "city_data.json"   // 城市数据
      };
      bool valid = false;
      for (int i = 0; i < sizeof(ALLOWED_FILES) / sizeof(ALLOWED_FILES[0]); i++) {
        if (targetName == ALLOWED_FILES[i]) {
          valid = true;
          break;
        }
      }

      if (!valid) {
        uploadError = "文件名不在白名单: " + targetName;
        Serial.println("拒绝上传: " + uploadError);
        return;
      }

      // ⚠️ 这里【不能】靠 upload.totalSize 判大小：核心在发出 FILE_START **之前**
      //    就把它显式置 0 了（见核心 WebServer/src/Parsing.cpp：FILE_START 前
      //    `_currentUpload->totalSize = 0;`，之后才在缓冲区刷写时累加）。
      //    ⇒ 在这一点上它必然是 0，那种写法是【死代码】。
      //    所以限额一律由下面 WRITE 分支的【累计字节数 uploadProgress】强制 ——
      //    那才是真实落盘量，与浏览器是否给出长度无关。

      // ★ 写入临时文件（不是目标文件）—— 收到一半失败时，设备上原有的文件毫发无损。
      uploadTargetName = targetName;
      uploadProgress = 0;
      if (fsUploadFile) fsUploadFile.close();
      SPIFFS.remove(UPLOAD_TMP);          // 清掉上一次可能残留的临时文件
      fsUploadFile = SPIFFS.open(UPLOAD_TMP, "w");
      if (!fsUploadFile) {
        uploadError = "无法创建临时文件";
        Serial.println("上传失败: " + uploadError);
        return;
      }
      Serial.printf("开始上传: %s（先写临时文件 %s）\n", targetName.c_str(), UPLOAD_TMP);

    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (uploadError.length() > 0) return;  // 已拒绝，跳过写入

      if (fsUploadFile) {
        // ★ 真正的限额：边收边累计。超过就立刻中止并丢掉临时文件 ——
        //   即使 upload.totalSize 缺失（=0），单文件接收量也超不过 UPLOAD_MAX_BYTES。
        //   ⚠️ 这不等于"永远写不爆存储"：替换时旧文件与临时文件要同时占位，
        //      空间不足时写会失败（走上面的失败分支，目标文件不受影响）。
        if (uploadProgress + upload.currentSize > UPLOAD_MAX_BYTES) {
          uploadError = "文件过大(最大500KB)";
          Serial.println("上传失败: " + uploadError);
          fsUploadFile.close();
          SPIFFS.remove(UPLOAD_TMP);
          return;
        }

        size_t written = fsUploadFile.write(upload.buf, upload.currentSize);
        if (written != upload.currentSize) {
          uploadError = "写入失败";
          Serial.println("上传失败: " + uploadError);
          fsUploadFile.close();
          SPIFFS.remove(UPLOAD_TMP);       // 丢弃残片，目标文件未被碰过
          return;
        }
        uploadProgress += written;
      }

    } else if (upload.status == UPLOAD_FILE_END) {
      if (uploadError.length() > 0) {
        if (fsUploadFile) fsUploadFile.close();
        SPIFFS.remove(UPLOAD_TMP);         // 失败路径：不留残片
        return;
      }

      if (!fsUploadFile) {
        uploadError = "文件句柄无效";
        return;
      }
      fsUploadFile.close();

      // 完整性校验：落盘字节数必须等于收到的字节数，否则不替换
      fs::File chk = SPIFFS.open(UPLOAD_TMP, "r");
      size_t onDisk = chk ? chk.size() : 0;
      if (chk) chk.close();
      if (uploadProgress == 0 || onDisk != uploadProgress) {
        uploadError = "文件不完整，已放弃";
        Serial.printf("上传失败: %s (落盘 %u / 收到 %u)\n",
                      uploadError.c_str(), (unsigned)onDisk, (unsigned)uploadProgress);
        SPIFFS.remove(UPLOAD_TMP);
        return;
      }

      // ★ 替换目标文件。本 SPIFFS 的 rename【不覆盖】已存在文件（实测），
      //   所以顺序是：  目标 → <目标>.bak  ⇒  tmp → 目标  ⇒  删 <目标>.bak
      //   —— 任何时候都【不】先删掉目标：
      //     · 备份失败 ⇒ 目标原样未动，直接放弃
      //     · 改名失败 ⇒ 立刻把 .bak 改回来（同步回滚）
      //     · 中途掉电 ⇒ 下次开机 cleanupSPIFFS() 认领 .bak 自动恢复
      String targetPath = "/" + uploadTargetName;
      String backupPath = targetPath + ".bak";
      bool ok = false;

      if (SPIFFS.exists(targetPath)) {
        // 清掉上次可能留下的残备份（加 exists 守卫：本 SPIFFS 的 remove 对
        // 不存在的文件会往串口打 ERROR，属正常路径上的噪音）
        if (SPIFFS.exists(backupPath)) SPIFFS.remove(backupPath);
        if (!SPIFFS.rename(targetPath, backupPath)) {
          uploadError = "备份旧文件失败，已放弃上传";
          Serial.println("上传失败: " + uploadError + "（旧文件未改动）");
          SPIFFS.remove(UPLOAD_TMP);
          return;
        }
      }

      // 此时目标名一定不存在（要么本来就没有，要么已改名为 .bak）
      ok = SPIFFS.rename(UPLOAD_TMP, targetPath);

      if (ok) {
        if (SPIFFS.exists(backupPath)) SPIFFS.remove(backupPath);  // 成功才清备份
        Serial.printf("上传成功: %s (%u 字节)\n",
                      uploadTargetName.c_str(), (unsigned)uploadProgress);
        uploadSuccess = true;
      } else {
        uploadError = "替换目标文件失败";
        if (SPIFFS.exists(backupPath)) {
          // 回滚：目标名此刻不存在，所以这一步应当能成
          bool rb = SPIFFS.rename(backupPath, targetPath);
          Serial.printf("上传失败: %s；旧文件回滚%s（备份 %s）\n", uploadError.c_str(),
                        rb ? "成功" : "失败，备份留在设备上，下次开机会自动恢复",
                        backupPath.c_str());
        } else {
          Serial.println("上传失败: " + uploadError + "（本次上传的是新文件，无旧文件需回滚）");
        }
        SPIFFS.remove(UPLOAD_TMP);
      }

    } else if (upload.status == UPLOAD_FILE_ABORTED) {
      // ★ 客户端中途断开时，核心【会】回调这个状态（Parsing.cpp 的
      //   _parseFormUploadAborted）。此时临时文件要丢掉 ——
      //   目标文件从头到尾没被碰过，所以设备上原有的东西是安全的。
      if (fsUploadFile) fsUploadFile.close();
      SPIFFS.remove(UPLOAD_TMP);
      Serial.printf("上传中止: 连接断开（已收 %u 字节），临时文件已丢弃；目标文件未改动\n",
                    (unsigned)uploadProgress);
    }
  });

  // /status —— 只读 JSON，不产生任何取数。
  // ⚠️ 用内联 lambda 而不是新增顶层函数：本项目踩过 Arduino .ino 自动原型陷阱
  //    （在类型定义前新增函数 ⇒ IDE 把自动原型插到那 ⇒ 满屏 "has not been declared"）。
  //    不新增顶层函数 = 完全绕开这个坑。
  // ⚠️ 本 build 没有 RTC 面包屑 ⇒ 故意【不】提供 boots/panicBoots（避免假指标）。
  server.on("/status", []() {
    char buf[288];
    snprintf(buf, sizeof(buf),
      "{\"fw\":\"%s\",\"cmp\":\"new-nopi\","
      "\"ip\":\"%s\","
      "\"uptime\":%lu,"
      "\"free\":%u,\"maxAlloc\":%u,"
      "\"rssi\":%d,\"mode\":%d,\"refresh\":%d,\"stockView\":%d,"
      "\"loopTaskStackFree\":%u}",
      FW_TAG,
      WiFi.localIP().toString().c_str(),
      (unsigned long)(millis() / 1000UL),
      (unsigned)ESP.getFreeHeap(),
      (unsigned)ESP.getMaxAllocHeap(),
      WiFi.RSSI(), (int)currentMode, refreshInterval, stockView,
      (unsigned)uxTaskGetStackHighWaterMark(NULL));
    server.send(200, "application/json", buf);
  });

  server.begin();

  // 从 NVS 加载股票代码+名称（没有就用默认值）
  for (int i = 0; i < SYMBOL_COUNT; i++) {
    String keyC = "stock" + String(i);
    String keyL = "label" + String(i);
    String savedC = prefs.getString(keyC.c_str(), "");
    String savedL = prefs.getString(keyL.c_str(), "");
    if (savedC.length() > 0) {
      static char bufC[4][16];  // 静态缓冲区（全局生命周期），否则指针失效
      static char bufL[4][16];
      savedC.toCharArray(bufC[i], 16);
      SYMBOLS[i].code = bufC[i];
      if (savedL.length() > 0) {
        savedL.toCharArray(bufL[i], 16);
        SYMBOLS[i].label = bufL[i];
      }
      Serial.printf("股票%d: %s / %s (从 NVS 加载)\n", i + 1, bufC[i], SYMBOLS[i].label);
    }
  }

  // 进入默认模式
  currentMode = (Mode)defaultMode;
  setMode(currentMode);

  Serial.println("WebServer: http://" + WiFi.localIP().toString());
}

void loop() {
  // 计数本轮次 —— 供末尾的 [HB] 心跳算 loopMs
  static uint32_t loopCount = 0;
  loopCount++;

  // ── 单轮峰值打点 ──────────────────────────────────────────────────────────
  //   ⚠️ 起因：loopMs 是【60s 窗口平均值】，对"卡顿"是【瞎的】。
  //      实测：有一轮卡了约 17 秒（uptime 1211s → 1288s，本该 +60s），
  //      而摊进窗口平均值只让 loopMs 从 ~60 变成 81 —— **看不出来**。
  //      但用户能感知的恰恰是这个尖峰。⇒ 必须单独记【本窗口内单轮最长耗时】。
  //
  //   口径：只量 loop() 【函数体】（不含末尾 delay(50)）⇒ 健康值应接近 0；
  //        数值 = 该轮被阻塞的毫秒数。取本窗口最大值，心跳后清零。
  static unsigned long loopPeakMs = 0;
  unsigned long tBody = millis();

  // 把 loop 体的各阶段也打上点 —— 否则 loopPeakMs 的尖峰归不到任何一笔上，
  //   就说不清"是取数卡的"还是"别的地方卡的"。只打 >=200ms 的。
  {
    unsigned long _t = millis();
    server.handleClient();
    T_SLOW_PRINT("handleClient", millis() - _t);
  }

  // 自动亮度调节(每分钟检查一次)
  static unsigned long lastBrightnessCheck = 0;
  if (millis() - lastBrightnessCheck >= 60000UL) {
    unsigned long _t = millis();
    autoAdjustBrightness();
    T_SLOW_PRINT("autoBrightness", millis() - _t);
    lastBrightnessCheck = millis();
  }

  // 动态壁纸轮播(时钟/天气模式 + 动态壁纸开启)
  if (wallpaperMode == 2 && (currentMode == MODE_CLOCK || currentMode == MODE_WEATHER)) {
    static unsigned long lastWallRotate = 0;
    if (millis() - lastWallRotate >= 5000UL) {
      unsigned long _t = millis();   // 动态壁纸要解 JPEG，可能是隐藏大户
      wallpaperIndex = (wallpaperIndex + 1) % 3;
      showWallpaper();
      if (currentMode == MODE_CLOCK) {          // 重叠加时钟
        lastH = lastM = lastS = lastD = -1;
        drawColon();
        digitalClockDisplay();
      } else if (w.ok) {                         // 重叠加天气
        drawWeather();
      }
      T_SLOW_PRINT("showWallpaper+overlay", millis() - _t);
      lastWallRotate = millis();
    }
  }

  {
    unsigned long _t = millis();   // 整个模式渲染
    renderCurrentMode();
    T_SLOW_PRINT("renderCurrentMode", millis() - _t);
  }

  // 收口：累加本窗口的单轮峰值（不含末尾 delay(50)）
  {
    unsigned long bodyMs = millis() - tBody;
    if (bodyMs > loopPeakMs) loopPeakMs = bodyMs;
  }

  // ── 长跑观测心跳（每 60s 一条）—— 只读埋点，不改变任何行为 ──────────
  // 字段顺序固定，便于长期观测脚本逐字段解析：
  //   [HB] uptime=%lus loopMs=%u free=%u maxAlloc=%u asyncStackFree=%uB rssi=%d
  //
  // loopMs 读法：loop() 末尾有 delay(50) ⇒ 【健康值 ≈ 50 ms】
  //   ≈ 50   健康，无额外阻塞       数百  每轮被额外占用（差值 = 阻塞代价）
  //   上千   有重量级阻塞（TLS 握手，单核 C3 上 1~3 秒/次）
  //
  // ⚠️ asyncStackFree 这一列装的是【loopTask 的栈余量】，不是 AsyncTCP 回调栈
  //    （本固件用同步 WebServer，根本没有 AsyncTCP 任务）。字段名是历史沿用，
  //    保留只为让长期观测脚本的正则匹配得上。
#if CMP_BUILD
  static unsigned long lastHeartbeat = 0;
  static uint32_t      lastLoopCount = 0;
  if (millis() - lastHeartbeat >= 60000UL) {
    unsigned long elapsed = millis() - lastHeartbeat;   // 首次即 60s，与 lastLoopCount=0 对齐
    uint32_t delta  = loopCount - lastLoopCount;
    uint32_t loopMs = delta ? (uint32_t)(elapsed / delta) : 0;   // 0 = 一轮都没跑，异常
    lastHeartbeat = millis();
    lastLoopCount = loopCount;
    // ⚠️ 字段【必须追加在末尾】：[HB] 的解析正则是逐字段
    //    前缀匹配（无 $ 锚），追加在后面不影响它匹配；
    //    但插在中间或改字段名会让它【静默失配】。
    Serial.printf("[HB] uptime=%lus loopMs=%u free=%u maxAlloc=%u asyncStackFree=%uB rssi=%d loopPeakMs=%lu\n",
                  millis() / 1000UL, loopMs,
                  (unsigned)ESP.getFreeHeap(),
                  (unsigned)ESP.getMaxAllocHeap(),
                  (unsigned)uxTaskGetStackHighWaterMark(NULL),
                  WiFi.RSSI(),
                  loopPeakMs);
    loopPeakMs = 0;   // 本窗口清零，下一个窗口重新取峰值
  }
#else
  // 发布构建：心跳【整段不编译】⇒ 串口每分钟不再刷 [HB] 行。
  // ⚠️ 上面 loopCount / loopPeakMs 的记账仍在跑（约 2μs/轮，可忽略），
  //    这里只是让它们"被读一次"以消掉 -Wunused-but-set-variable 警告。
  (void)loopCount;
  (void)loopPeakMs;
#endif

  delay(50);
}
