// IL0373 控制器命令字节宏（GDEW0154T8 1.54 寸黑白墨水屏）
//
// 命令字节直接对照 Arduino GxEPD2 库 GxEPD2_154_T8.cpp 的实现
// （/Users/rick/Documents/Arduino/libraries/GxEPD2/src/epd/GxEPD2_154_T8.cpp）
//
// 注意 IL0373 与 SSD1681 命令集不通用：
//   - SSD1681: 0x12=SW Reset, 0x24=write BW RAM, 0x22+0x20=update, 0x10=deep sleep
//   - IL0373:  0x12=display refresh, 0x10/0x13=write RAM, 0x07 0xA5=deep sleep
// 之前误以为是 SSD1681 写了一版完全不工作的驱动，已废弃。

#pragma once

#define IL0373_PANEL_SETTING            0x00
#define IL0373_POWER_SETTING            0x01
#define IL0373_POWER_OFF                0x02
#define IL0373_POWER_ON                 0x04
#define IL0373_BOOSTER_SOFT_START       0x06
#define IL0373_DEEP_SLEEP               0x07   // 参数 0xA5 才会生效
#define IL0373_DTM1                     0x10   // Data Transmission 1（previous / B 帧）
#define IL0373_DISPLAY_REFRESH          0x12   // 触发刷新（不带参数）
#define IL0373_DTM2                     0x13   // Data Transmission 2（current / W 帧）
#define IL0373_LUT_VCOM                 0x20
#define IL0373_LUT_WW                   0x21
#define IL0373_LUT_BW                   0x22
#define IL0373_LUT_WB                   0x23
#define IL0373_LUT_BB                   0x24
#define IL0373_PLL_CONTROL              0x30
#define IL0373_VCOM_DATA_INTERVAL       0x50
#define IL0373_RESOLUTION_SETTING       0x61
#define IL0373_VCOM_DC                  0x82

// partial refresh 相关
#define IL0373_PARTIAL_WINDOW           0x90   // 7 字节：x_lo, xe_lo, y_hi, y_lo, ye_hi, ye_lo, 0x01
#define IL0373_PARTIAL_IN               0x91   // 进入 partial 模式（无参数）
#define IL0373_PARTIAL_OUT              0x92   // 退出 partial 模式（无参数）
