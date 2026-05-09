// 1.54 寸 GDEW0154T8 单色墨水屏（IL0373 控制器）公开 API
//
// 默认硬件接线（在 epaper_154.c 顶部以宏定义，可改）：
//   CS  = GPIO5    MOSI = GPIO23   SCK = GPIO18   MISO 不接
//   DC  = GPIO27   RST  = GPIO33   BUSY = GPIO14
//   VCC = 3.3V，共地

#pragma once

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"
#include "gfxfont.h"

// 屏幕物理分辨率（GDEW0154T8 实际为 152×152，对照 GxEPD2_154_T8::WIDTH/HEIGHT）
// 注意：rotation 1/3 时逻辑宽高互换，请改用 epaper_width()/epaper_height()
#define EPD_W 152
#define EPD_H 152

// 初始化：GPIO + SPI 总线 + 硬件复位。屏命令序列推迟到 epaper_display_full 内执行
esp_err_t epaper_init(void);

// 设置旋转方向：0=正向、1=顺时针 90°、2=180°、3=顺时针 270°
// 影响所有 draw_* 的逻辑坐标系；clear/display_full/sleep 不受影响
// 仅修改本地状态，不下发屏命令——旋转纯靠帧缓冲层坐标变换实现
void epaper_set_rotation(uint8_t rotation);

// 当前旋转下的逻辑宽/高（rotation 1/3 时为 EPD_H/EPD_W）
int  epaper_width(void);
int  epaper_height(void);

// 用单一颜色填充帧缓冲。color=0xFF 全白，0x00 全黑
void epaper_clear(uint8_t color);

// 在帧缓冲上画单像素。坐标越界静默忽略；black=true 写黑、false 写白
void epaper_draw_pixel(int x, int y, bool black);

// 在帧缓冲上画水平线（从 (x,y) 起向右 len 像素）。越界部分自动裁剪
void epaper_draw_hline(int x, int y, int len, bool black);

// 在帧缓冲上画垂直线（从 (x,y) 起向下 len 像素）。越界部分自动裁剪
void epaper_draw_vline(int x, int y, int len, bool black);

// 矩形边框（左上角 (x,y)、宽 w、高 h）。越界部分自动裁剪；w/h<=0 静默忽略
void epaper_draw_rect(int x, int y, int w, int h, bool black);

// 填充矩形（左上角 (x,y)、宽 w、高 h）。越界部分自动裁剪；w/h<=0 静默忽略
void epaper_fill_rect(int x, int y, int w, int h, bool black);

// 用 8×8 单色字体在帧缓冲上画字符串（左上角 (x,y)，每字符占 8 像素宽）
// 仅渲染 ASCII 0-127；遇到不在范围的字符或 NUL 终止
void epaper_draw_string_8x8(int x, int y, const char *s, bool black);

// 用 Adafruit_GFX 比例字体在帧缓冲上画字符串
//   - (x, y) 是**基线坐标**（baseline），即字母 "A" 底端的 y；"j/g/p/q" 等下伸字符
//     会延伸到 y 之下。需要按"字符串左上角"定位，传 y = top + font->yAdvance - 4 之类
//   - '\n' 换行，cursor x 回到入参 x、y 增加 font->yAdvance
//   - 字符不在 [font->first, font->last] 范围内则跳过该字符位置
//   - 字体 .h 文件放 components/epaper_154/fonts/，#include "FreeSansBold9pt7b.h" 等
void epaper_draw_string_gfx(int x, int y, const char *s, const GFXfont *font, bool black);

// 计算字符串在 GFX 字体下的渲染包围盒（用于居中对齐等）
//   - 返回 *out_w / *out_h（如非 NULL）
//   - 不考虑 '\n' 换行（多行调用方自己分行算）
void epaper_get_text_bounds_gfx(const char *s, const GFXfont *font, int *out_w, int *out_h);

// 把 1bit 位图贴到帧缓冲（透明覆盖语义）
//   - bmp 按行 raster scan：每行 (w+7)/8 字节，行内 MSB 在左
//   - bit=1 时画 black 参数指定的颜色（true=黑、false=白）
//   - bit=0 时不动（保留底层像素）—— 类似"透明贴纸"
//   - 越界部分自动裁剪；跟随 epaper_set_rotation 的旋转
//   - 数据通常用 PIL/Pillow 由 PNG 转 1bit 生成：img.convert('1').tobytes()
void epaper_draw_bitmap(int x, int y, const uint8_t *bmp, int w, int h, bool black);

// 把帧缓冲写入显存并触发一次全刷新（约 1.6 秒，等 BUSY 拉到空闲后返回）
// 内部含完整 IL0373 init 序列、5 张 LUT 下发、Power On
esp_err_t epaper_display_full(void);

// 局部刷新：把帧缓冲 (x,y,w,h) 区域用 partial LUT 单步刷到屏（约 350ms）
//   - 坐标是物理屏坐标系（rotation=0 视角）；x、w 内部强制 8 对齐（向外扩张）
//   - 调用前必须先 epaper_display_full() 至少一次（partial 是差分，需基线）
//     首次调用会自动转调 epaper_display_full()
//   - 内部双写（write+refresh+write）保证下次 partial 不残影
//   - 频繁 partial 后建议每 N 次做一次 epaper_display_full() 清屏
esp_err_t epaper_display_partial(int x, int y, int w, int h);

// 仅关 DC-DC（POWER_OFF 0x02），寄存器/LUT/上一帧 RAM 全保留
//   - 用途：CPU light sleep 等"短暂停"前调用，避免屏被持续 DC 偏置导致黑边累积
//   - 唤醒：epaper_power_on() 重启 DC-DC（~60ms），之后可直接 partial/full
//   - 和 epaper_sleep() 区别：sleep 是深睡丢状态，power_off 仅断电保状态
esp_err_t epaper_power_off(void);

// 重启 DC-DC（POWER_ON 0x04），等 BUSY 拉到空闲（典型 ~60ms）
// 必须配合 epaper_power_off() 使用；deep sleep 后无效（应走 epaper_init 重新初始化）
esp_err_t epaper_power_on(void);

// Power Off + Deep Sleep。下次使用前需重新 epaper_init
esp_err_t epaper_sleep(void);
