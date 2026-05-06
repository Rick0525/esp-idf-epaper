// 主程序入口
// 节点 8：partial refresh 演示
//   - 先全刷画静态布局（标题 + 数字框）
//   - 5 次 partial 刷新数字框内的计数 0..4，串口打印每次耗时
//   - 最后全刷清屏作对比

#include "epaper_154.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "MAIN";

// partial 窗口：x=64, w=80（均 8 对齐），覆盖中央数字区
#define COUNTER_X 64
#define COUNTER_Y 28
#define COUNTER_W 80
#define COUNTER_H 16

static void redraw_counter(int n)
{
    // 清掉数字框内壁（保留 1px 边框）
    epaper_fill_rect(COUNTER_X + 1, COUNTER_Y + 1,
                     COUNTER_W - 2, COUNTER_H - 2, false);
    // 在框内画数字（8x8 字体，居中）
    char buf[16];
    snprintf(buf, sizeof(buf), "%d", n);
    epaper_draw_string_8x8(COUNTER_X + 8, COUNTER_Y + 4, buf, true);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());
    epaper_set_rotation(0);

    // ---- 1. 初始全刷：标题 + 数字框 + 计数 0 ----
    epaper_clear(0xFF);
    epaper_draw_string_8x8(8, 8, "Partial demo", true);
    epaper_draw_hline(0, 22, EPD_W, true);
    epaper_draw_string_8x8(8, 32, "Count:", true);
    epaper_draw_rect(COUNTER_X, COUNTER_Y, COUNTER_W, COUNTER_H, true);
    redraw_counter(0);
    epaper_draw_string_8x8(8, 60, "watching partial...", true);

    int64_t t0 = esp_timer_get_time();
    ESP_ERROR_CHECK(epaper_display_full());
    int64_t t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "全刷耗时 %lld ms", (t1 - t0) / 1000);

    vTaskDelay(pdMS_TO_TICKS(1000));

    // ---- 2. 5 次 partial：数字 1..5 ----
    for (int i = 1; i <= 5; i++) {
        redraw_counter(i);

        t0 = esp_timer_get_time();
        ESP_ERROR_CHECK(epaper_display_partial(COUNTER_X, COUNTER_Y,
                                                COUNTER_W, COUNTER_H));
        t1 = esp_timer_get_time();
        ESP_LOGI(TAG, "partial %d 耗时 %lld ms", i, (t1 - t0) / 1000);

        vTaskDelay(pdMS_TO_TICKS(800));
    }

    // ---- 3. 全刷清屏对比 ----
    vTaskDelay(pdMS_TO_TICKS(1500));
    epaper_clear(0xFF);
    epaper_draw_string_8x8(8, 60, "all done (full)", true);

    t0 = esp_timer_get_time();
    ESP_ERROR_CHECK(epaper_display_full());
    t1 = esp_timer_get_time();
    ESP_LOGI(TAG, "末尾全刷耗时 %lld ms", (t1 - t0) / 1000);

    ESP_ERROR_CHECK(epaper_sleep());
    ESP_LOGI(TAG, "节点 8 完成");
}
