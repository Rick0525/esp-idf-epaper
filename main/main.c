// 主程序入口
// 节点 1：调用 epaper_init() 完成 SPI/GPIO 初始化与 SW Reset 时序验证

#include "epaper_154.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    esp_err_t err = epaper_init();
    if (err == ESP_OK) {
        ESP_LOGI(TAG, "epaper_init 成功");
    } else {
        ESP_LOGE(TAG, "epaper_init 失败: %d", err);
    }
}
