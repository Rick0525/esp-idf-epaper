// 主程序入口
// 节点 0：仅打印启动日志，验证项目骨架可构建、可烧录、可运行

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "MAIN";

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");
}
