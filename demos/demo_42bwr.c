// 4.2" BWR 三色 demo（demos/demo_42bwr.c）
//
// 全自包含示例，演示 epaper_uc8276_42bwr 驱动的全部能力域：
//   - 三色填充（fill_rect）
//   - 矩形边框（draw_rect）
//   - GFX 比例字体 + 8x8 点阵字体（黑/红两色）
//   - 三色像素编码与边界过渡
//
// 视觉布局：
//   - 左 1/3：白底
//   - 中 1/3：纯黑
//   - 右 1/3：纯红
//   - 顶部黑色 GFX 标题 "ESP-IDF 4.2\" BWR"
//   - 中段白底黑边矩形，里面红字 "Hello!"
//   - 底部红色 8x8 字符串
//
// 调用方式：main/main.c 通过 extern 声明并在 app_main 里调一次。
// REQUIRES 必须设为 epaper_uc8276_42bwr。

#include "epaper_42bwr.h"
#include "FreeSansBold9pt7b.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "DEMO42";

void demo_run(void)
{
    ESP_LOGI(TAG, "4.2\" BWR demo 启动");
    ESP_ERROR_CHECK(epaper_42_init());

    epaper_42_clear(EPD42_WHITE);

    // 三色竖条：左 1/3 白（保持）、中 1/3 黑、右 1/3 红
    int third = EPD42_W / 3;
    epaper_42_fill_rect(third,     0, third,                 EPD42_H, EPD42_BLACK);
    epaper_42_fill_rect(third * 2, 0, EPD42_W - third * 2,   EPD42_H, EPD42_RED);

    // 顶部黑色 GFX 标题（baseline y）
    epaper_42_draw_string_gfx(8, 24, "ESP-IDF 4.2\" BWR", &FreeSansBold9pt7b, EPD42_BLACK);

    // 中段白底黑边矩形 + 内嵌红字
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
    epaper_42_draw_string_8x8(8, EPD42_H - 16,
                              "WeiFeng 4.2 BWR / UC8276 OTP-LUT", EPD42_RED);

    ESP_ERROR_CHECK(epaper_42_display_full());
    ESP_LOGI(TAG, "上屏完成（约 18s），进入 deep sleep");
    ESP_ERROR_CHECK(epaper_42_sleep());
}
