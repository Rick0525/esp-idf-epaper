// 1.54 寸 GDEW0154T8 单色墨水屏（IL0373 控制器）公开 API
//
// 硬件接线（ESP32-PICO-KIT v4.1，固定不变）：
//   CS  = GPIO5    MOSI = GPIO23   SCK = GPIO18   MISO 不接
//   DC  = GPIO27   RST  = GPIO33   BUSY = GPIO14
//   VCC = 3.3V，共地

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

// 屏幕分辨率（GDEW0154T8 实际为 152×152，对照 GxEPD2_154_T8::WIDTH/HEIGHT）
#define EPD_W 152
#define EPD_H 152

// 初始化：GPIO + SPI 总线 + 硬件复位。屏命令序列推迟到 epaper_display_full 内执行
esp_err_t epaper_init(void);

// 用单一颜色填充帧缓冲。color=0xFF 全白，0x00 全黑
void epaper_clear(uint8_t color);

// 在帧缓冲上画单像素。坐标越界静默忽略；black=true 写黑、false 写白
void epaper_draw_pixel(int x, int y, bool black);

// 在帧缓冲上画水平线（从 (x,y) 起向右 len 像素）。越界部分自动裁剪
void epaper_draw_hline(int x, int y, int len, bool black);

// 把帧缓冲写入显存并触发一次全刷新（约 1.6 秒，等 BUSY 拉到空闲后返回）
// 内部含完整 IL0373 init 序列、5 张 LUT 下发、Power On
esp_err_t epaper_display_full(void);

// Power Off + Deep Sleep。下次使用前需重新 epaper_init
esp_err_t epaper_sleep(void);
