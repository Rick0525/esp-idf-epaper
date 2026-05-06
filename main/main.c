// 主程序入口
// 节点 9：GFX 字体渲染器演示
//   - 上半屏：8×8 复古点阵字体（节点 4 起）作为对照
//   - 下半屏：FreeSansBold9pt7b（GFX 比例字体）— 基础库默认字
//     · "Hello, IDF!" 普通显示
//     · 一行居中（用 epaper_get_text_bounds_gfx 算宽度）
//     · '\n' 多行换行

#include "epaper_154.h"
#include "FreeSansBold9pt7b.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());
    epaper_set_rotation(0);

    epaper_clear(0xFF);

    // ---- 上半屏：8×8 字体对照（左上角 (x,y)） ----
    epaper_draw_string_8x8(8, 6, "8x8 font:", true);
    epaper_draw_string_8x8(8, 18, "Hello, IDF!", true);
    epaper_draw_hline(0, 32, EPD_W, true);

    // ---- 下半屏：GFX 字体（baseline (x,y)） ----
    epaper_draw_string_8x8(8, 38, "GFX FreeSansBold:", true);

    // FreeSansBold9pt7b 的 yAdvance = 22；baseline 取 top + ~17（下伸预留 5）
    const int y_baseline_1 = 38 + 14 + 17;   // = 69
    epaper_draw_string_gfx(8, y_baseline_1, "Hello, IDF!", &FreeSansBold9pt7b, true);

    // 居中显示一行
    int w, h;
    const char *centered = "centered";
    epaper_get_text_bounds_gfx(centered, &FreeSansBold9pt7b, &w, &h);
    int cx = (EPD_W - w) / 2;
    epaper_draw_string_gfx(cx, y_baseline_1 + 24, centered, &FreeSansBold9pt7b, true);
    ESP_LOGI(TAG, "centered: w=%d h=%d cx=%d", w, h, cx);

    // 多行（'\n' 换行测试）
    epaper_draw_string_gfx(8, y_baseline_1 + 50, "line A\nline B",
                            &FreeSansBold9pt7b, true);

    ESP_ERROR_CHECK(epaper_display_full());
    ESP_ERROR_CHECK(epaper_sleep());

    ESP_LOGI(TAG, "节点 9 完成");
}
