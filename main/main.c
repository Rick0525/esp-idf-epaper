// 主程序入口
// 综合演示：仪表盘风格 demo，覆盖驱动库的全部能力域
//
// 阶段 1（首次 full，~1.6s）：画静态布局
//   - 顶部：16×16 上箭头 bitmap + GFX FreeSansBold 标题
//   - 中段：4 行 8×8 字体的特性列表（每行前一个实心方块 bullet）
//   - 下段：计数器框 + 进度条框（占位，等 partial 填）
//
// 阶段 2（5 次 partial，每次 ~360ms）：刷新动态区
//   - 计数器数字（GFX 字体）每秒 +20
//   - 进度条按 1/5 推进
//   - "step N/5" 标签同步
//   - 一次 partial 同时刷新这三个元素（共用一个矩形窗口）
//
// 阶段 3（末次 full）：切换到结束页
//   - 居中 GFX 大字 "Demo done!"
//   - 8×8 小字统计本次刷新次数

#include "epaper_154.h"
#include "FreeSansBold9pt7b.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "MAIN";

// ---- 16×16 上箭头 bitmap ----
static const uint8_t arrow_up_16x16[] = {
    0x01, 0x80, 0x03, 0xC0, 0x07, 0xE0, 0x0F, 0xF0,
    0x1F, 0xF8, 0x3F, 0xFC, 0x7F, 0xFE, 0xFF, 0xFF,
    0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0,
    0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0, 0x03, 0xC0,
};

// ---- 8×8 实心圆点 bullet ----
static const uint8_t bullet_8x8[] = {
    0x00,
    0x00,
    0x3C,  // ..####..
    0x7E,  // .######.
    0x7E,  // .######.
    0x3C,  // ..####..
    0x00,
    0x00,
};

// ---- 动态区窗口（partial 刷新范围）----
// 包含：计数器（y≈80..100）、进度条（y≈108..120）、step 标签（y≈124..132）
// partial 要求 x、w 8 对齐：取整屏宽 0..152，刷的全部宽度（库会自动对齐到 0..152）
#define DYN_X 0
#define DYN_Y 76
#define DYN_W EPD_W
#define DYN_H 60

#define COUNTER_MAX 5

static void draw_static_layout(void)
{
    epaper_clear(0xFF);

    // 顶部：bitmap + GFX 标题
    epaper_draw_bitmap(4, 4, arrow_up_16x16, 16, 16, true);
    epaper_draw_string_gfx(26, 18, "Demo!", &FreeSansBold9pt7b, true);
    epaper_draw_hline(0, 24, EPD_W, true);

    // 特性列表
    epaper_draw_string_8x8(4, 30, "Features:", true);
    const char *items[] = {
        "8x8 + GFX font",
        "draw bitmap",
        "rect + fill",
        "partial refresh",
    };
    for (int i = 0; i < 4; i++) {
        int y = 42 + i * 9;
        epaper_draw_bitmap(6, y, bullet_8x8, 8, 8, true);
        epaper_draw_string_8x8(18, y, items[i], true);
    }

    // 横线分隔
    epaper_draw_hline(0, 78, EPD_W, true);

    // 动态区：计数器框（左）+ 进度条框（下）+ step 标签占位
    // 计数器："Count:" 标签 + 数字框
    epaper_draw_string_8x8(4, 84, "Count:", true);
    epaper_draw_rect(56, 80, 88, 16, true);

    // 进度条边框
    epaper_draw_rect(8, 108, EPD_W - 16, 10, true);

    // step 标签（partial 填）
    // 在末尾画一个分隔小线表示 step 区下方
}

static void update_dynamic(int step)
{
    // 1. 计数器数字
    epaper_fill_rect(57, 81, 86, 14, false);   // 清白计数框内壁
    char num[8];
    snprintf(num, sizeof(num), "%d", step * 20);
    int w, h;
    epaper_get_text_bounds_gfx(num, &FreeSansBold9pt7b, &w, &h);
    int cx = 56 + (88 - w) / 2;
    epaper_draw_string_gfx(cx, 80 + 13, num, &FreeSansBold9pt7b, true);

    // 2. 进度条填充
    int bar_inner_w = (EPD_W - 16) - 4;     // 内宽 132
    int filled = bar_inner_w * step / COUNTER_MAX;
    epaper_fill_rect(10, 110, bar_inner_w, 6, false);     // 先清
    epaper_fill_rect(10, 110, filled,       6, true);     // 再填

    // 3. step 标签
    epaper_fill_rect(4, 124, 80, 8, false);   // 清旧
    char label[16];
    snprintf(label, sizeof(label), "step %d/%d", step, COUNTER_MAX);
    epaper_draw_string_8x8(4, 124, label, true);
}

static void draw_done_page(int partial_count)
{
    epaper_clear(0xFF);

    // 居中 GFX 大字
    int w, h;
    const char *msg = "Demo done!";
    epaper_get_text_bounds_gfx(msg, &FreeSansBold9pt7b, &w, &h);
    int cx = (EPD_W - w) / 2;
    epaper_draw_string_gfx(cx, 60, msg, &FreeSansBold9pt7b, true);

    // 装饰横线
    epaper_draw_hline(20, 70, EPD_W - 40, true);

    // 统计 8x8
    char stat1[32], stat2[32];
    snprintf(stat1, sizeof(stat1), "%d partial done", partial_count);
    snprintf(stat2, sizeof(stat2), "+ 2 full refresh");
    epaper_draw_string_8x8(20, 86, stat1, true);
    epaper_draw_string_8x8(20, 100, stat2, true);

    // 底部装饰：4 个上箭头点缀
    for (int i = 0; i < 4; i++) {
        epaper_draw_bitmap(4 + i * 36, 124, arrow_up_16x16, 16, 16, true);
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok - 综合 demo 启动");

    ESP_ERROR_CHECK(epaper_init());
    epaper_set_rotation(0);

    // ---- 阶段 1：首次 full ----
    draw_static_layout();
    update_dynamic(0);                         // step 0 占位
    ESP_ERROR_CHECK(epaper_display_full());
    ESP_LOGI(TAG, "阶段 1：静态布局已上屏（full）");

    vTaskDelay(pdMS_TO_TICKS(800));

    // ---- 阶段 2：partial 循环 ----
    for (int i = 1; i <= COUNTER_MAX; i++) {
        update_dynamic(i);
        ESP_ERROR_CHECK(epaper_display_partial(DYN_X, DYN_Y, DYN_W, DYN_H));
        ESP_LOGI(TAG, "阶段 2：partial step %d/%d 完成", i, COUNTER_MAX);
        vTaskDelay(pdMS_TO_TICKS(700));
    }

    vTaskDelay(pdMS_TO_TICKS(1200));

    // ---- 阶段 3：末次 full 切换结束页 ----
    draw_done_page(COUNTER_MAX);
    ESP_ERROR_CHECK(epaper_display_full());
    ESP_LOGI(TAG, "阶段 3：结束页已上屏（full）");

    ESP_ERROR_CHECK(epaper_sleep());
    ESP_LOGI(TAG, "综合 demo 完成");
}
