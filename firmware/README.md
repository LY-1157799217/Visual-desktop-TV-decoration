# `firmware/` — 底层屏显自检工程（PlatformIO）

> **这个目录是给谁看的？**
> - **只想让股票屏正常工作** → 不用管这里，回 [根目录 README](../README.md) 就够了。
> - **屏幕黑屏 / 主工程烧完启动就崩**，想判断"到底是硬件接线还是软件配置" → 看下面「怎么用」。
> - **你只用 Arduino IDE、也不想装 PlatformIO** → 走更省事的那条：
>   [`test_display/test_display.ino`](../test_display/test_display.ino)（纯色轮播）。
>   代价是它**只测纯色、测不出"缺字体"**，所以本工程仍有存在价值。
> - **打算自己改引脚、改字体、大改固件** → 看「显示配置在哪」和「改引脚 / 改字体」。
>
> 它**不含任何业务功能**，只是一个最小屏显验证工程。日常使用完全不需要它。

---

## 它是什么

初始化 TFT → 刷红绿蓝三色 → 打三行字 → 结束。就这些。

烧进去后屏幕**常驻**显示：

```
Stock TV
ESP32-C3
Display OK!
```

**这三行就是判据：**

| 你看到 | 结论 |
|---|---|
| 三行字（红绿蓝闪一下是正常的） | **屏、背光、SPI 接线、引脚配置全都是对的** ⇒ 问题一定在软件/配置侧 |
| 只有背光亮、纯黑 | 硬件或接线问题 ⇒ 回[根 README「第零步」](../README.md)核对引脚 |
| 连背光都不亮 | 供电/背光脚问题 |

### ⚠️ 它不会动你的设备数据

**不联网、不写 NVS、不碰 SPIFFS。**

实测：烧进去跑一轮，前后读取设备 NVS 分区，**sha256 逐字节相同**。
⇒ 拿它排障**不会弄丢你已配好的 WiFi，也不会动已上传的图片**，用完直接烧回主工程即可。

> **这是刻意的设计要求，请保持。** 不要往 [src/main.cpp](src/main.cpp) 里加
> `WiFi.begin()` / `Preferences.put*()` / 任何 SPIFFS 写入。
>
> **踩过的坑（真事）**：早期版本这里有一句 `WiFi.begin(占位符账号, 占位符密码)`。
> 就算它连不上，**ESP32 的 WiFi 驱动也会把这对凭据写进 NVS** ——
> 结果是"排障一次，使用者就得重新配网一次"。相关警告也写在 [include/config.h](include/config.h) 里。

---

## 怎么用（PlatformIO）

本工程用 **PlatformIO** 构建，**不是** Arduino IDE。
（主工程 [arduino/integrated](../arduino/integrated) 反过来 —— 它用 Arduino IDE。）

先装 PlatformIO Core：<https://docs.platformio.org/en/latest/core/installation/>
然后在**本目录**执行：

```bash
pio run                     # 只编译
pio run -t upload           # 编译并烧录
pio device list             # 不确定串口号时：列出设备
pio run -t upload -p COM8   # 指定串口
pio device monitor          # 看串口输出（115200）
```

本机实测：设备端口为 `COM8`（Espressif 原生 USB-Serial/JTAG，`VID:PID=303A:1001`）。
编译产物在 `.pio/build/stocktv-c3/firmware.bin`。

板子认不出来时：按住 BOOT 再上电（同[根 README「疑难排查」](../README.md)最后一行）。

---

## 显示配置在哪（3 处，内容相同，改一处要同步改其余）

| 用在哪 | 位置 | 当前状态 |
|---|---|---|
| **主工程**（Arduino IDE） | 根目录 `TFT_eSPI_Setup.h` → 覆盖到库目录 `User_Setup.h` | ✅ 日常只需管这一处 |
| **本工程**（PlatformIO） | [platformio.ini](platformio.ini) 里 `build_flags` 的 `-D` 那一串 | ✅ **当前真正生效的是这一处** |
| 上一条的副本 | [include/User_Setup.h](include/User_Setup.h) | ⚪️ **当前不生效**（因为定义了 `USER_SETUP_LOADED`，TFT_eSPI 会跳过它），留作兜底 |

> **⚠️ 为什么不做成一份？试过，没成。**
> TFT_eSPI 2.5.43 本身支持"随 sketch 再放一份配置"（`tft_setup.h`），本来可以把这几处并成一处；
> 但那样编出来的固件**编译能过、宏也逐项核对无误，烧进真机却在启动时崩溃**（根因未查明）。
> 所以最终**保持各存一份，没有合并** —— 这是**刻意**的，不是遗漏。
> **后续也不建议再尝试合并**，完整记录见 [platformio.ini](platformio.ini) 末尾。

---

## 改引脚 / 改字体

1. 改上面那 **3 处**（内容必须一致）；
2. 回本目录 `pio run` 重新编译；
3. 烧进去看是否还显示 `Display OK!`。

**字体**同理：`LOAD_GLCD` / `LOAD_FONT2` … / `SMOOTH_FONT` 这些 `#define` 若缺失，
`drawChar()` 整个函数体会被 `#ifdef` 编译掉，`tft.println()` 变成空操作 ——
**屏上只出红绿蓝、一个字都没有**，看着像"屏还是坏的"。这三处都要带上字体定义。

**显示方向**：`tft.setRotation(0)` 在 [src/main.cpp](src/main.cpp)，
主工程在 [arduino/integrated](../arduino/integrated) 里也有一处，两边要一致（否则字会倒 180°）。

---

## 最保险的烧法：只写 app 区（绝对不碰 NVS / SPIFFS）

`pio run -t upload` 默认会**一并写 bootloader 与分区表**。分区表没变时这没影响，
但要**绝对保证**不动 NVS/SPIFFS，可以只写 app 区。

PlatformIO 自带的 esptool 是 **4.11.0**（注意 4.x 用**下划线**命令，见下文警告）。
它在 PlatformIO 的包目录里，Windows 下通常是：

```
C:\Users\<你的用户名>\.platformio\packages\tool-esptoolpy\esptool.py
```

下面用 `$ESPTOOL` 代指这个路径（PowerShell 里写 `$env:ESPTOOL`，或直接换成上面的完整路径）：

```bash
ESPTOOL="C:/Users/<你的用户名>/.platformio/packages/tool-esptoolpy/esptool.py"

python "$ESPTOOL" \
  --chip esp32c3 --port COM8 --baud 115200 \
  write_flash 0x10000 .pio/build/stocktv-c3/firmware.bin
```

分区表（[no_ota.csv](no_ota.csv)，已与**设备上实际的分区表逐项核对一致**）：

| 分区 | 偏移 | 大小 |
|---|---|---|
| `nvs` | `0x9000` | `0x5000` |
| `otadata` | `0xe000` | `0x2000` |
| `app0` | `0x10000` | `0x200000` |
| `spiffs` | `0x210000` | `0x1F0000` |

⇒ **只写 `0x10000` 就只碰 app0**，NVS 和 SPIFFS 一个字节都不会变。

### 动手前先备份（真出事时能救回来）

```bash
python "$ESPTOOL" \
  --chip esp32c3 --port COM8 --baud 115200 \
  read_flash 0x0 0x10000 backup.bin
```

本机实测：**65536 字节 5.7 秒**（91.4 kbit/s）。这一段含**分区表 + NVS + bootloader**，足够救砖。

> ⚠️ `backup.bin` **含你的 WiFi 凭据**（明文），别上传、别外传、别贴进 Issue。
> （本仓库 `.gitignore` 已含 `*.bin`，不会误提交；但"不会被提交"不等于"可以外传"。）

> ⚠️ **别用 esptool 5.x 的写法**：5.x 把子命令改成了连字符（`read-flash` / `write-flash`），
> 而 4.x 用下划线。**本机实测 esptool 5.3.0 在 Windows + USB-Serial/JTAG 下，走 stub 读
> 会在 24576 字节处稳定失败**（`Packet content transfer stopped`），加 `--no-stub` 才能读，
> 但速度掉到约 9.8 kbit/s（64KB 要近 1 分钟）。**用 PlatformIO 自带的 4.11.0 就好。**

### 万一 NVS 被写坏了（设备连不上自家 WiFi 了）

症状：串口报 `*wm:AutoConnect: FAILED`。这时**只写回 nvs 段**，
即从 `backup.bin` 里裁出偏移 `0x9000`、长度 `0x5000` 的那一段，然后：

```bash
python "$ESPTOOL" \
  --chip esp32c3 --port COM8 --baud 115200 \
  write_flash 0x9000 nvs_只这段.bin
```

**只为这一个分区，其余一律不碰。** 写回后设备会恢复 `AutoConnect: SUCCESS`。

---

## 相关

- 主工程与完整使用说明：[根目录 README](../README.md)
- 安全与隐私说明：[SECURITY.md](../SECURITY.md)
