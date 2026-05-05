// 主程序入口
// 节点 4：嵌入 8×8 字体 + helloWorld 演示
//   - 白底
//   - "Hello, IDF!" 在左上角（10, 10）
//   - 一条水平线分隔（y=30）
//   - 之下再放一行说明文字

#include "epaper_154.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());

    epaper_clear(0xFF);

    epaper_draw_string_8x8(10, 10, "Hello, IDF!", true);
    epaper_draw_hline(0, 30, EPD_W, true);
    epaper_draw_string_8x8(10, 40, "GDEW0154T8 OK", true);

    ESP_ERROR_CHECK(epaper_display_full());
    ESP_ERROR_CHECK(epaper_sleep());

    ESP_LOGI(TAG, "节点 4 完成：helloWorld 已上屏");
}
