#pragma once
// ============================================================================
//  ESP32-C3：修正 ESP-IDF 的 REG_SPI_BASE 宏（TFT_eSPI 2.5.43 与较新 core 的冲突）
// ----------------------------------------------------------------------------
//  本文件由 firmware/platformio.ini 的 build_flags 强制包含：
//      -include include/fix_idf_c3_spi_base.h
//
//  【症状】用较新的 Arduino core（实测 2.0.17 = IDF v4.4.7）编译本工程，
//  烧进真机后启动即崩、反复重启：
//      Guru Meditation Error: Core 0 panic'ed (Store access fault)
//      符号化 → TFT_eSPI::begin_tft_write()（TFT_eSPI.cpp:81，被 writecommand 内联）
//      即 `SET_BUS_WRITE_MODE` 解引用 `*_spi_user` 时越界。
//
//  【根因】TFT_eSPI 2.5.43 的 C3 处理器头里有兜底：
//      #ifndef REG_SPI_BASE
//        #define REG_SPI_BASE(i) DR_REG_SPI2_BASE
//      #endif
//      但较新的 ESP-IDF 已在 soc.h 里【无守卫】地定义：
//      #define REG_SPI_BASE(i) (((i)==2) ? (DR_REG_SPI2_BASE) : (0))
//      ⇒ 那句 #ifndef 被挡掉，走 SDK 版。
//      而 TFT_eSPI 传进来的下标是 SPI_PORT = SPI2_HOST，
//      本版 IDF 的 hal/spi_types.h 里 `SPI2_HOST = 1`（不是 2）
//      ⇒ 条件 ((1)==2) 恒假 ⇒ 整个宏塌成 0
//      ⇒ _spi_user = 0 + 0x10 = 0x10 —— 这只是"寄存器偏移"，不是地址；
//        解引用它必然 Store access fault。
//      （TFT_eSPI 2.5.43 发布于 2023-12，该 IDF 改动晚于它，属版本错配，非库的疏忽。）
//
//  【修法】利用 soc.h 自带 #pragma once：先把 soc.h 整个吃掉，
//      之后 TFT_eSPI 再 include 它时会被 pragma once 挡掉、不再展开那个宏，
//      此时再把它改成 TFT_eSPI 原本想要的样子。
//
//  ⚠️ 【为什么必须用 `-include` 抢在最前面，而不能只放在 tft_setup.h 里】
//      实测（探针验证）：TFT_eSPI.cpp 那个编译单元里，`TFT_eSPI.h` 是**第一个**被
//      include 的 —— shim 跑完之后，处理器头才在 TFT_eSPI.h:99 经 hal/gpio_ll.h
//      把 soc.h 拉进来，**宏会被 SDK 版盖回去**，`_spi_user` 仍是 0x10。
//      而 `-include` 在任何头文件之前执行，等于抢在 soc.h 之前，
//      所以那个顺序下定义才能存活。
//      （对照：主程序 .ino 的第一个 include 是 <Arduino.h>，它已经先把 soc.h 拉进来了，
//        所以主程序侧把同一段修正放在 User_Setup.h 里就能生效 ——
//        见根目录 TFT_eSPI_Setup.h。两处是同一个修正、适配两侧不同的 include 顺序。）
//
//  【验证】修好与否可直接从固件里读出来，不用靠猜：
//      修正前 _spi_user = 0x00000010；修正后应为 0x60024010
//      （= DR_REG_SPI2_BASE(0x60024000) + SPI_USER 偏移 0x10）
// ============================================================================

#include <soc/soc.h>   // 顺带带进 sdkconfig.h，供下面的目标判断使用

#if defined(CONFIG_IDF_TARGET_ESP32C3)
  #ifdef REG_SPI_BASE
    #undef REG_SPI_BASE
  #endif
  #define REG_SPI_BASE(i) DR_REG_SPI2_BASE
#endif
