// 4.2" BWR 墨水屏分阶段硬件验证
//
// 通过宏 BRINGUP_STAGE 切换不同测试场景，从最简单开始逐步加复杂度：
//   A 整屏白：验证 init / SPI / BUSY / 电源时序 / deep sleep
//   B 整屏黑：验证黑层 buffer 字节方向、0x10 命令、像素编码"黑"分支
//   C 整屏红：验证红层 buffer、0x13 命令、红是 bit=0 的反向编码
//   D 三色竖条 + GFX 字体：验证绘图原语 + 三色边界
//
// 修改 BRINGUP_STAGE 后 idf.py build flash monitor 看屏。

#include "epaper_42bwr.h"
#include "FreeSansBold9pt7b.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "MAIN";

#define BRINGUP_STAGE 'B'

static void stage_a_white(void)
{
    ESP_LOGI(TAG, "阶段 A：整屏白");
    epaper_42_clear(EPD42_WHITE);
}

static void stage_b_black(void)
{
    ESP_LOGI(TAG, "阶段 B：整屏黑");
    epaper_42_clear(EPD42_BLACK);
}

static void stage_c_red(void)
{
    ESP_LOGI(TAG, "阶段 C：整屏红");
    epaper_42_clear(EPD42_RED);
}

static void stage_d_demo(void)
{
    ESP_LOGI(TAG, "阶段 D：三色竖条 + GFX 字体 demo");
    epaper_42_clear(EPD42_WHITE);

    // 三色竖条：左 1/3 白（保持）、中 1/3 黑、右 1/3 红
    int third = EPD42_W / 3;
    epaper_42_fill_rect(third,     0, third, EPD42_H, EPD42_BLACK);
    epaper_42_fill_rect(third * 2, 0, EPD42_W - third * 2, EPD42_H, EPD42_RED);

    // 顶部黑色 GFX 标题（baseline y）
    epaper_42_draw_string_gfx(8, 24, "ESP-IDF 4.2\" BWR", &FreeSansBold9pt7b, EPD42_BLACK);

    // 中段黑边白底矩形 + 内嵌红字
    int box_x = 60, box_y = 120, box_w = EPD42_W - 120, box_h = 60;
    epaper_42_fill_rect(box_x, box_y, box_w, box_h, EPD42_WHITE);
    epaper_42_draw_rect(box_x, box_y, box_w, box_h, EPD42_BLACK);
    int tw, th;
    const char *msg = "Hello!";
    epaper_42_get_text_bounds_gfx(msg, &FreeSansBold9pt7b, &tw, &th);
    int tx = box_x + (box_w - tw) / 2;
    int ty = box_y + box_h / 2 + 6;
    epaper_42_draw_string_gfx(tx, ty, msg, &FreeSansBold9pt7b, EPD42_RED);

    // 底部红色 8x8 字体
    epaper_42_draw_string_8x8(8, EPD42_H - 16, "WeiFeng 4.2 BWR / UC8276 OTP-LUT", EPD42_RED);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok - 阶段 %c", BRINGUP_STAGE);

    ESP_ERROR_CHECK(epaper_42_init());

    switch (BRINGUP_STAGE) {
        case 'A': stage_a_white(); break;
        case 'B': stage_b_black(); break;
        case 'C': stage_c_red();   break;
        case 'D': stage_d_demo();  break;
        default:  stage_a_white(); break;
    }

    ESP_ERROR_CHECK(epaper_42_display_full());
    ESP_LOGI(TAG, "上屏完成，准备 deep sleep");

    ESP_ERROR_CHECK(epaper_42_sleep());
    ESP_LOGI(TAG, "demo 结束");
}
