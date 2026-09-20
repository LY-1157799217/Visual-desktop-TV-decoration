// User_Setup.h for stock-tv ESP32-C3 + ST7789 240x240
// ⚠️ 本文件是【调试工程】的显示配置**副本**（README「📍 显示配置在哪」有同一份说明）。
//    这份配置在仓库里共存 3 处、内容必须一致，改引脚时 3 处一起改：
//      ① 主工程 根目录 TFT_eSPI_Setup.h（覆盖到库目录的 User_Setup.h，日常只需管它）
//      ② 本工程 platformio.ini 的 build_flags —— **当前真正生效的是这一处**
//      ③ 就是本文件 —— 因定义了 USER_SETUP_LOADED，TFT_eSPI 会跳过它 ⇒ **当前不生效**，留作兜底
//    为什么不做成"单一来源"：试过（让本文件 #include 主工程那份），**没成** —— 那样编出的
//    固件编译能过、宏也逐项核对无误，烧进真机却在第一次 Serial.println 处崩（根因未查明）。
//    ⇒ 保持各存一份，**刻意如此，不建议再尝试合并**；完整记录见 platformio.ini 末尾。
#define USER_SETUP_INFO "stock-tv"

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ESP32-C3 pins (以通电实测为准，详见 README「第零步」)
#define TFT_MOSI 5   // SDA → IO5
#define TFT_SCLK 3   // SCL → IO3
#define TFT_CS   -1  // CS tied to GND
#define TFT_DC   2   // DC/RS → IO2
#define TFT_RST  6   // Reset → IO6
#define TFT_BL   1   // Backlight → IO1

#define TFT_BACKLIGHT_ON LOW  // P-MOS: LOW to turn on
#define TFT_INVERSION_ON

// SPI settings
#define SPI_FREQUENCY  10000000
#define SPI_READ_FREQUENCY  5000000

// Font loading — ⚠️ 必须加载，否则 drawChar() 整个函数体被 #ifdef 编译掉，
//    tft.println() 成了空操作：屏上只有红绿蓝三色、一个字都不出。
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_FONT8
#define SMOOTH_FONT
