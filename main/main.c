// 主程序入口
// 节点 6：图形基元演示（vline / rect / fill_rect）
//   - 整屏外框（验证可视区边界 = 152×152）
//   - 三个矩形：边框 / 填充 / 同心嵌套
//   - 一组等距 vline 与 fill_rect 横条
//   - 顶部标题、底部标签使用既有 8×8 字体

#include "epaper_154.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());

    epaper_clear(0xFF);

    // 1. 整屏外框（紧贴 152×152 有效区四边）
    epaper_draw_rect(0, 0, EPD_W, EPD_H, true);

    // 2. 顶部标题 + 分隔线
    epaper_draw_string_8x8(8, 4, "Node 6: Shapes", true);
    epaper_draw_hline(0, 16, EPD_W, true);

    // 3. 三个矩形演示：边框 / 填充 / 同心嵌套
    epaper_draw_rect(8,  24, 40, 28, true);
    epaper_fill_rect(56, 24, 40, 28, true);
    epaper_draw_rect(104, 24, 40, 28, true);
    epaper_draw_rect(108, 28, 32, 20, true);
    epaper_draw_rect(112, 32, 24, 12, true);

    // 4. vline 演示：5 条等距竖线
    epaper_draw_string_8x8(4, 60, "vlines:", true);
    for (int i = 0; i < 5; i++) {
        epaper_draw_vline(8 + i * 16, 76, 30, true);
    }

    // 5. fill_rect 横条（左右各留 8px）
    epaper_fill_rect(8, 116, EPD_W - 16, 6, true);

    // 6. 底部标签
    epaper_draw_string_8x8(8, 134, "shapes ok", true);

    ESP_ERROR_CHECK(epaper_display_full());
    ESP_ERROR_CHECK(epaper_sleep());

    ESP_LOGI(TAG, "节点 6 完成：图形基元演示已上屏");
}
