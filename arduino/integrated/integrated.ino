// ============================================================
// integrated.ino — SDD 小电视 整合版 (Step2)
// 四模式: 时钟/天气/相册(真实渲染) + 股票(占位, Step3)
// 配网: WiFiManager | 切模式: WebServer | 配置: Preferences
// 引脚(已在 User_Setup.h 配好): SCL=IO3 SDA=IO5 DC=IO2 RST=IO6 BL=IO1 CS=GND
// ============================================================
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
fs::File fsUploadFile;             // 网页上传文件句柄

#define COL_BG   0x0000
#define COL_TEXT 0xFFFF
#define COL_DIM  0x7BEF

// ---------------- 模式 ----------------
enum Mode { MODE_CLOCK = 0, MODE_WEATHER = 1, MODE_PHOTO = 2, MODE_STOCK = 3 };
const char* MODE_NAMES[]    = {"时钟", "天气", "相册", "股票"};
const char* MODE_NAMES_EN[] = {"CLOCK", "WEATHER", "PHOTO", "STOCK"};
Mode currentMode = MODE_CLOCK;

// ---------------- 配置 ----------------
String cityCode        = "101120301";
String stockCode       = "sh600519";
int    brightness      = 50;
int    defaultMode     = 0;
int    refreshInterval = 5;
int    wallpaperMode   = 1;   // 壁纸模式: 0无 1静态 2动态
int    wallpaperIndex  = 0;   // 静态壁纸索引(0-2)
const char* CITY_NAME  = "Zi Bo";   // 城市拼音(暂固定淄博)

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
  stockCode       = prefs.getString("stockCode", "sh600519");
  brightness      = prefs.getInt("brightness", 50);
  defaultMode     = prefs.getInt("defaultMode", 0);
  refreshInterval = prefs.getInt("refreshInterval", 5);
  wallpaperMode   = prefs.getInt("wallpaperMode", 1);
  wallpaperIndex  = prefs.getInt("wallpaperIndex", 0);
}

void setBacklight() {
  pinMode(TFT_BL, OUTPUT);
  digitalWrite(TFT_BL, LOW);
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
  String html = "<!DOCTYPE html><html><head><meta charset='utf-8'>";
  html += "<meta name='viewport' content='width=device-width,initial-scale=1'>";
  html += "<title>SDD 小电视</title>";
  html += "<style>";
  html += "body{font-family:-apple-system,sans-serif;max-width:480px;margin:0 auto;padding:14px;"
          "background:#1a1a2e url('/m.jpg') no-repeat center center fixed;background-size:cover;color:#eee;min-height:100vh;box-sizing:border-box;}";
  html += ".card{background:rgba(22,33,62,.9);border-radius:12px;padding:12px;margin:10px 0;}";
  html += "h3{text-align:center;margin:4px 0;}";
  html += "h4{margin:4px 0;}";
  html += ".grid{display:flex;flex-wrap:wrap;gap:8px;}";
  html += "button{font-size:18px;padding:10px 16px;border:none;border-radius:8px;background:#0f3460;color:#eee;cursor:pointer;flex:1;min-width:80px;}";
  html += "button:active{background:#16537e;}";
  html += "a{text-decoration:none;}";
  html += "details.card>summary{font-size:18px;font-weight:bold;cursor:pointer;padding:6px;list-style:none;}";
  html += "details.card>summary::before{content:'\\25B8 ';}";
  html += "details.card[open]>summary::before{content:'\\25BE ';}";
  html += ".sub{font-size:14px;color:#9ab;margin-top:8px;}";
  html += ".center{text-align:center;}";
  html += "</style></head><body>";
  html += "<h3>SDD 小电视控制台</h3>";
  html += "<div class='card center'>当前模式: <b>" + String(MODE_NAMES[currentMode]) + "</b> &middot; IP: " + WiFi.localIP().toString() + "</div>";

  // 模式切换
  html += "<div class='card'><h4>模式切换</h4><div class='grid'>";
  for (int i = 0; i < 4; i++) {
    html += "<a href='/set?mode=" + String(i) + "'><button>" + MODE_NAMES[i] + "</button></a>";
  }
  html += "</div></div>";

  // 壁纸复合按键(点击展开)
  html += "<details class='card'><summary>壁纸</summary><div class='grid'>";
  html += "<a href='/wp_select'><button>静态壁纸</button></a>";
  html += "<a href='/set?wp=2'><button>动态壁纸</button></a>";
  html += "<a href='/set?wp=0'><button>取消壁纸</button></a>";
  html += "</div><div class='sub'>当前壁纸: " + String(wallpaperMode == 0 ? "无" : (wallpaperMode == 1 ? "静态" : "动态")) + "</div></details>";

  // 股票视图复合按键(点击展开)
  html += "<details class='card'><summary>股票视图</summary><div class='grid'>";
  html += "<a href='/set?stockview=0'><button>分时图</button></a>";
  html += "<a href='/set?stockview=1'><button>日K图</button></a>";
  html += "</div><div class='sub'>当前视图: " + String(stockView == 0 ? "分时图" : "日K图") + "</div></details>";

  html += "<p class='center' style='margin-top:16px'><a href='/upload' style='color:#9ab'>上传图片…</a> &middot; <a href='/stock_edit' style='color:#9ab'>更换股票…</a></p>";

  // WiFi 管理折叠区（警告色）
  html += "<details class='card' style='background:rgba(120,30,30,.85);border:1px solid #f87171;margin-top:20px'>";
  html += "<summary style='color:#fca5a5'>⚠ WiFi 管理</summary>";
  html += "<div class='grid' style='margin-top:12px'>";
  html += "<a href='/change_wifi'><button style='background:#16a34a;color:#fff'>更换 WiFi</button></a>";
  html += "<button onclick=\"if(confirm('确认重置 WiFi？\\n\\n设备将重启并进入配网模式（AP 热点）。\\n\\n请在设备重启后（约 5 秒），手动连接热点 SmallTV-XXXXXX，再打开 192.168.4.1 重新配网。')){location.href='/reset_wifi'}\" style='background:#dc2626;color:#fff'>重置 WiFi</button>";
  html += "</div>";
  html += "<div class='sub' style='margin-top:8px'>更换: 输入新 WiFi 快速切换<br>重置: 清空配置进 AP 配网模式（保底）</div>";
  html += "</details>";

  html += "</body></html>";
  server.send(200, "text/html; charset=utf-8", html);
}

void handleSet() {
  // 切模式
  if (server.hasArg("mode")) {
    int m = server.arg("mode").toInt();
    if (m >= 0 && m < 4) {
      setMode((Mode)m);
      server.send(200, "text/html; charset=utf-8",
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
      server.send(200, "text/html; charset=utf-8",
                  "壁纸已设置  <a href='/'>返回</a>");
      return;
    }
  }
  // 设股票视图(分时/日K)
  if (server.hasArg("stockview")) {
    int sv = server.arg("stockview").toInt();
    if (sv >= 0 && sv <= 1) {
      stockView = sv;
      if (currentMode == MODE_STOCK) drawQuote(stockCurIdx, true);   // 重绘当前股
      server.send(200, "text/html; charset=utf-8",
                  "股票视图已切换  <a href='/'>返回</a>");
      return;
    }
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
  html += "document.getElementById('cropModal').style.display='none';document.body.innerHTML='<div style=\"text-align:center;padding:60px;color:#eee\"><h3>上传中...</h3></div>';";
  html += "fetch('/do_upload',{method:'POST',body:fd}).then(r=>r.text()).then(()=>{});";
  html += "},'image/jpeg',0.9);}</script>";
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
    "<p style='color:#fbbf24;margin-top:20px'>请在设备重启后（约 5 秒），手动连接热点<br><strong>SmallTV-XXXXXX</strong><br>再打开 <strong>192.168.4.1</strong> 重新配网</p>"
    "</body></html>");
  delay(2000);
  prefs.clear();                // 清除所有 NVS 配置（包括 WiFi）
  WiFi.disconnect(true, true);  // 断开并清除 WiFi 凭据
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
  textSpr.print(CITY_NAME);
  int cw = textSpr.textWidth(CITY_NAME);
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
    if (fetchWeather()) {
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

  // 轮播: 5 秒翻页
  if (now - stockLastRotate >= 5000UL) {
    stockCurIdx = (stockCurIdx + 1) % SYMBOL_COUNT;
    drawQuote(stockCurIdx, true);   // 切股: 全量
    stockLastRotate = now;
  }

  // 4股轮询刷新(股票延迟敏感，需要持续更新所有自选股)
  // 每 refreshInterval/SYMBOL_COUNT 秒刷一只: 行情+分时+日K(按需)
  static unsigned long lastRefresh = 0;
  if (now - lastRefresh >= (unsigned long)refreshInterval * 1000 / SYMBOL_COUNT) {
    static int refreshIdx = 0;
    fetchQuote(SYMBOLS[refreshIdx].code, quotes[refreshIdx]);
    fetchSpark(SYMBOLS[refreshIdx].code, quotes[refreshIdx]);
    // 日K按需: 仅当当前视图=日K 且 正在刷当前显示的股票 时才抓
    if (stockView == 1 && refreshIdx == stockCurIdx) {
      fetchKline(SYMBOLS[refreshIdx].code, quotes[refreshIdx]);
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

  // 配网
  tft.setTextColor(TFT_WHITE, TFT_BLACK);
  tft.setTextSize(2);
  tft.drawCentreString("配网中...", 120, 100, 2);
  setupWifi();

  // SPIFFS(相册)
  if (!SPIFFS.begin(true)) Serial.println("SPIFFS 挂载失败");

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

  // WebServer(切模式+壁纸设置+WiFi管理)
  server.on("/", handleRoot);
  server.on("/set", handleSet);
  server.on("/wp_select", handleWallpaperSelect);
  server.on("/m.jpg", HTTP_GET, handleImage);
  server.on("/upload", handleUploadPage);
  server.on("/stock_edit", handleStockEdit);
  server.on("/stock_save", HTTP_GET, handleStockSave);
  server.on("/change_wifi", handleChangeWiFi);
  server.on("/do_change_wifi", handleDoChangeWiFi);
  server.on("/reset_wifi", handleResetWiFi);
  server.on("/do_upload", HTTP_POST, []() {
    server.send(200, "text/html; charset=utf-8",
      "<html><body style='font-family:sans-serif;background:#1a1a2e;color:#eee;text-align:center;padding:40px'>"
      "<h3>上传完成，设备将在 2 秒后重启</h3></body></html>");
    delay(2000);
    ESP.restart();
  }, []() {
    HTTPUpload& upload = server.upload();
    if (upload.status == UPLOAD_FILE_START) {
      String targetName = server.arg("fname");  // 从隐藏字段读目标文件名
      if (targetName.length() == 0) targetName = upload.filename;  // 回退
      if (fsUploadFile) fsUploadFile.close();
      fsUploadFile = SPIFFS.open("/" + targetName, "w");
      Serial.printf("开始上传: %s\n", targetName.c_str());
    } else if (upload.status == UPLOAD_FILE_WRITE) {
      if (fsUploadFile) fsUploadFile.write(upload.buf, upload.currentSize);
    } else if (upload.status == UPLOAD_FILE_END) {
      if (fsUploadFile) {
        fsUploadFile.close();
        Serial.println("上传完成");
      }
    }
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
  server.handleClient();

  // 动态壁纸轮播(时钟/天气模式 + 动态壁纸开启)
  if (wallpaperMode == 2 && (currentMode == MODE_CLOCK || currentMode == MODE_WEATHER)) {
    static unsigned long lastWallRotate = 0;
    if (millis() - lastWallRotate >= 5000UL) {
      wallpaperIndex = (wallpaperIndex + 1) % 3;
      showWallpaper();
      if (currentMode == MODE_CLOCK) {          // 重叠加时钟
        lastH = lastM = lastS = lastD = -1;
        drawColon();
        digitalClockDisplay();
      } else if (w.ok) {                         // 重叠加天气
        drawWeather();
      }
      lastWallRotate = millis();
    }
  }

  renderCurrentMode();
  delay(50);
}
