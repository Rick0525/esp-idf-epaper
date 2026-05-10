// 4.2 寸 黑/白/红 三色墨水屏（UC8276 系列控制器）公开 API
//
// 默认硬件接线（在 epaper_42bwr.c 顶部以宏定义，可改）：
//   CS  = GPIO5    MOSI = GPIO23   SCK = GPIO18   MISO 不接
//   DC  = GPIO27   RST  = GPIO33   BUSY = GPIO14
//   VCC = 3.3V，共地
//
// 与 1.54" IL0373 驱动并存：两者使用同样的引脚与 SPI3_HOST，物理上换屏即可。
// 不要在同一固件里同时 link 两个组件——SPI bus 会被初始化两次。

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "gfxfont.h"

// 屏幕物理分辨率（4.2" BWR 行业标准）
#define EPD42_W 400
#define EPD42_H 300

// 三色枚举。WHITE 是默认底色；RED 优先于 BLACK 显示
typedef enum {
    EPD42_WHITE = 0,
    EPD42_BLACK = 1,
    EPD42_RED   = 2,
} epd42_color_t;

// 初始化：GPIO + SPI 总线 + 硬件复位。屏命令序列推迟到 epaper_42_display_full 内执行
esp_err_t epaper_42_init(void);

// 用单一颜色填充帧缓冲（黑层 + 红层一起处理）
void epaper_42_clear(epd42_color_t color);

// 在帧缓冲上画单像素。坐标越界静默忽略
void epaper_42_draw_pixel(int x, int y, epd42_color_t c);

// 水平线（从 (x,y) 起向右 len 像素）。越界部分自动裁剪
void epaper_42_draw_hline(int x, int y, int len, epd42_color_t c);

// 垂直线（从 (x,y) 起向下 len 像素）。越界部分自动裁剪
void epaper_42_draw_vline(int x, int y, int len, epd42_color_t c);

// 矩形边框（左上角 (x,y)、宽 w、高 h）。越界部分自动裁剪
void epaper_42_draw_rect(int x, int y, int w, int h, epd42_color_t c);

// 填充矩形（左上角 (x,y)、宽 w、高 h）。越界部分自动裁剪
void epaper_42_fill_rect(int x, int y, int w, int h, epd42_color_t c);

// 8×8 单色字体串（仅 ASCII 0-127）
void epaper_42_draw_string_8x8(int x, int y, const char *s, epd42_color_t c);

// Adafruit_GFX 比例字体串
//   - (x, y) 是**基线坐标**（baseline）
//   - '\n' 换行；不在 [font->first, font->last] 范围内的字符跳过
void epaper_42_draw_string_gfx(int x, int y, const char *s, const GFXfont *font, epd42_color_t c);

// 计算 GFX 字体串的渲染包围盒（不含换行处理）
void epaper_42_get_text_bounds_gfx(const char *s, const GFXfont *font, int *out_w, int *out_h);

// 把帧缓冲写入显存并触发一次全刷（典型 ~5 秒）
//   - 内部含 init + Power On + 写黑层 + 写红层 + 触发 + 等 BUSY
//   - 不自动 sleep，调用方按需 epaper_42_sleep / power_off
esp_err_t epaper_42_display_full(void);

// 仅关 DC-DC（POWER_OFF 0x02），寄存器/RAM 全保留。和 sleep 区别：sleep 丢状态
esp_err_t epaper_42_power_off(void);

// 重启 DC-DC（POWER_ON 0x04），等 BUSY 拉到空闲
esp_err_t epaper_42_power_on(void);

// Power Off + 1 秒延时 + Deep Sleep。下次使用前需重新 epaper_42_init
esp_err_t epaper_42_sleep(void);
