// 主程序入口
// 节点 2：完整 init → 清白 → 全刷 → 深睡

#include "epaper_154.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());

    epaper_clear(0xFF);                    // 帧缓冲填白
    ESP_ERROR_CHECK(epaper_display_full()); // 触发全刷
    ESP_ERROR_CHECK(epaper_sleep());       // 进入深睡，省电 + 防残影

    ESP_LOGI(TAG, "节点 2 完成：屏应为全白，已进入 Deep Sleep");
}
