// User_Setup.h for stock-tv ESP32-C3 + ST7789 240x240
#define USER_SETUP_INFO "stock-tv"

#define ST7789_DRIVER
#define TFT_WIDTH  240
#define TFT_HEIGHT 240

// ESP32-C3 pins
#define TFT_MOSI 4
#define TFT_SCLK 3
#define TFT_CS   -1  // CS tied to GND
#define TFT_DC   2
#define TFT_RST  5
#define TFT_BL   1

#define TFT_BACKLIGHT_ON LOW  // P-MOS: LOW to turn on
#define TFT_INVERSION_ON

// SPI settings
#define SPI_FREQUENCY  10000000
#define SPI_READ_FREQUENCY  5000000

// Font loading
#define LOAD_GLCD
#define LOAD_FONT2
#define LOAD_FONT4
#define LOAD_FONT6
#define LOAD_FONT7
#define LOAD_GFXFF
#define SMOOTH_FONT
