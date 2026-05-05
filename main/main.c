// 主程序入口
// 节点 3：在白底上画分辨率诊断图样
//   - 四个角各一块 16×16 实心黑块（验证四边都贴齐 152×152 边界）
//   - 中心十字线（水平 y=76 + 垂直 x=76，验证 X/Y 没互换、没偏移）

#include "epaper_154.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

#define BLOCK_PX 16

// 152×152 是 GDEW0154T8 的"有效像素区"，物理像素阵列更大、外圈一圈白边
// 是屏物理特性（黑相能扫到、稳定显示扫不到）。本图样的几何只在 152×152
// 范围内有意义，验收时角块应贴到 152×152 区域的四角而非屏物理玻璃边。
void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());

    epaper_clear(0xFF);   // 白底

    // 四角实心 16×16 黑块（贴 152×152 有效区四角）
    const int corners[4][2] = {
        { 0,                  0                  },
        { EPD_W - BLOCK_PX,   0                  },
        { 0,                  EPD_H - BLOCK_PX   },
        { EPD_W - BLOCK_PX,   EPD_H - BLOCK_PX   },
    };
    for (int c = 0; c < 4; c++) {
        for (int dy = 0; dy < BLOCK_PX; dy++) {
            epaper_draw_hline(corners[c][0], corners[c][1] + dy, BLOCK_PX, true);
        }
    }

    // 中心十字（水平整行 + 垂直整列）
    epaper_draw_hline(0, EPD_H / 2, EPD_W, true);
    for (int y = 0; y < EPD_H; y++) {
        epaper_draw_pixel(EPD_W / 2, y, true);
    }

    ESP_ERROR_CHECK(epaper_display_full());
    ESP_ERROR_CHECK(epaper_sleep());

    ESP_LOGI(TAG, "节点 3 完成：四角块 + 十字线（确认 %dx%d 有效区排布）", EPD_W, EPD_H);
}
