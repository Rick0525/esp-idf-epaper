// 主程序入口
// 节点 7：旋转支持（rotation 0/1/2/3）演示
//   - 4 次全刷依次展示 0°/90°/180°/270°
//   - 每屏左上角 "rot N deg" 标识当前角度
//   - 顶部 + 左侧均有图形标识，用来区分上下/左右

#include "epaper_154.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <stdio.h>

static const char *TAG = "MAIN";

// 在当前 rotation 下绘制一个带方向标识的内容：
// - 整屏外框
// - 左上角角度标签（"rot N deg"）+ 一条横向分隔线
// - 顶边一行实心矩形（标识"上方"）
// - 左边一列等距 vline（标识"左方"）
// - 中央 "Hello, IDF!" 文本
static void draw_rotated_demo(int angle_deg)
{
    int W = epaper_width(), H = epaper_height();
    char title[16];
    snprintf(title, sizeof(title), "rot %d", angle_deg);

    epaper_clear(0xFF);

    // 整屏外框
    epaper_draw_rect(0, 0, W, H, true);

    // 左上角文字（紧贴左上、便于辨认旋转方向）
    epaper_draw_string_8x8(4, 4, title, true);
    epaper_draw_hline(0, 16, W, true);

    // "顶边"标识：紧贴上沿的实心横条（用户视角下永远在顶部）
    epaper_fill_rect(0, 18, W, 4, true);

    // "左边"标识：靠左的 5 条等距短 vline（用户视角下永远在左侧）
    for (int i = 0; i < 5; i++) {
        epaper_draw_vline(4 + i * 3, 30, 40, true);
    }

    // 中央文本
    epaper_draw_string_8x8(W / 2 - 44, H / 2 - 4, "Hello, IDF!", true);

    // 底部标签（用户视角下永远在底部）
    epaper_draw_string_8x8(4, H - 12, "bottom", true);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());

    for (int r = 0; r < 4; r++) {
        epaper_set_rotation(r);
        ESP_LOGI(TAG, "rotation=%d 逻辑尺寸=%dx%d", r, epaper_width(), epaper_height());

        draw_rotated_demo(r * 90);

        ESP_ERROR_CHECK(epaper_display_full());
        ESP_LOGI(TAG, "rotation=%d 全刷完成", r);

        // 留 2 秒给肉眼观察
        vTaskDelay(pdMS_TO_TICKS(2000));
    }

    ESP_ERROR_CHECK(epaper_sleep());
    ESP_LOGI(TAG, "节点 7 完成：4 个旋转角度演示已上屏");
}
