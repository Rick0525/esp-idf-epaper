// 4.2 寸 BWR（黑白红）墨水屏控制器命令字节宏
// 适用 UC8176 / UC8276 / IL0398 系列（命令集兼容）
// 数值参照 Good Display GDEW042Z15 / GDEH042Z21 官方 Arduino demo
//   /Users/rick/Documents/ESP32Projects/4.2arduino_BWR/4.2/4.2/4.2.ino
// 与 IL0373 的 0x10/0x12/0x13/0x02/0x04/0x07 命令编号一致，但本系列：
//   - DTM1 = 黑层（B/W）；DTM2 = 红层
//   - LUT 走 OTP，不写 0x20-0x24
//   - 不支持局部刷新

#pragma once

#define UC8276_PANEL_SETTING            0x00
#define UC8276_POWER_OFF                0x02
#define UC8276_POWER_ON                 0x04
#define UC8276_BOOSTER_SOFT_START       0x06
#define UC8276_DEEP_SLEEP               0x07   // 参数 0xA5 才会生效
#define UC8276_DTM1                     0x10   // 黑层 framebuffer (B/W layer)
#define UC8276_DISPLAY_REFRESH          0x12   // 触发刷新（不带参数）
#define UC8276_DTM2                     0x13   // 红层 framebuffer (Red layer)
#define UC8276_VCOM_DATA_INTERVAL       0x50
#define UC8276_RESOLUTION_SETTING       0x61   // 4 字节：W_hi W_lo H_hi H_lo
