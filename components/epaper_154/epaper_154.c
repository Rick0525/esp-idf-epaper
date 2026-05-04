// 1.54 寸单色墨水屏（SSD1681 兼容）驱动实现
//
// 节点 1 范围：
//   - GPIO 配置（DC、RST 输出；BUSY 输入 + 上拉）
//   - SPI3_HOST（VSPI，走 IOMUX，CS=5/MOSI=23/SCK=18）总线初始化与从设备添加
//   - 硬件复位时序（RST 拉低 10ms → 释放 10ms）
//   - 发送 0x12 SW Reset，并采样 BUSY 翻转作为 SPI 通路 + DC 切换 + RST 时序的整体验证

#include "epaper_154.h"
#include "ssd1681_cmd.h"

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_rom_sys.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EPD";

// ---- 引脚定义（用户接线，固定不变） ----
#define EPD_PIN_CS   5
#define EPD_PIN_MOSI 23
#define EPD_PIN_SCK  18
#define EPD_PIN_DC   27
#define EPD_PIN_RST  33
#define EPD_PIN_BUSY 14

#define EPD_SPI_HOST   SPI3_HOST
#define EPD_SPI_CLK_HZ (4 * 1000 * 1000)   // 起步 4MHz，稳定后可调高至 10~20MHz

// BUSY 等待最长时间（毫秒）。SSD1681 BUSY 高表示忙
#define EPD_BUSY_TIMEOUT_MS 5000

static spi_device_handle_t s_spi = NULL;

// 等 BUSY 拉低（屏空闲）。超时返回 ESP_ERR_TIMEOUT 并打错误日志
static esp_err_t wait_busy_low(int timeout_ms)
{
    int waited = 0;
    while (gpio_get_level(EPD_PIN_BUSY) == 1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
        if (waited >= timeout_ms) {
            ESP_LOGE(TAG, "BUSY 等待超时 (%d ms)", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
    }
    return ESP_OK;
}

// 发送一字节命令（DC=0）
static esp_err_t send_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_DC, 0);
    spi_transaction_t t = {
        .length = 8,
        .tx_buffer = &cmd,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

// 节点 1 暂未用到 send_data，节点 2 起启用
__attribute__((unused))
static esp_err_t send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return ESP_OK;
    gpio_set_level(EPD_PIN_DC, 1);
    spi_transaction_t t = {
        .length = len * 8,
        .tx_buffer = data,
    };
    return spi_device_polling_transmit(s_spi, &t);
}

// 硬件复位：RST 高 20ms → 低 20ms → 高 20ms（与 Arduino GxEPD2 默认时序一致）
static void hw_reset(void)
{
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(20));
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(20));
}

// 在 sample_ms 毫秒内每 1ms 采样 BUSY，返回采样到的最低电平
// （0 表示期间至少出现过一次低电平，1 表示全程为高）
static int sample_busy_min(int sample_ms)
{
    int min_lvl = 1;
    for (int i = 0; i < sample_ms; i++) {
        int lvl = gpio_get_level(EPD_PIN_BUSY);
        if (lvl == 0) min_lvl = 0;
        esp_rom_delay_us(1000);
    }
    return min_lvl;
}

esp_err_t epaper_init(void)
{
    ESP_LOGI(TAG, "epaper_init 开始");

    // ---- 1. 控制引脚（DC、RST 输出；BUSY 输入 + 上拉） ----
    gpio_config_t out_io = {
        .pin_bit_mask = (1ULL << EPD_PIN_DC) | (1ULL << EPD_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&out_io));

    gpio_config_t busy_io = {
        .pin_bit_mask = (1ULL << EPD_PIN_BUSY),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,   // v6.0 GPIO 驱动不再隐式上拉，必须显式启用
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&busy_io));

    gpio_set_level(EPD_PIN_DC, 0);
    gpio_set_level(EPD_PIN_RST, 1);
    ESP_LOGI(TAG, "GPIO 配置完成：DC=%d RST=%d BUSY=%d", EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY);

    // ---- 2. SPI 总线 ----
    spi_bus_config_t buscfg = {
        .mosi_io_num = EPD_PIN_MOSI,
        .miso_io_num = -1,
        .sclk_io_num = EPD_PIN_SCK,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    ESP_ERROR_CHECK(spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = EPD_SPI_CLK_HZ,
        .mode = 0,                          // CPOL=0 CPHA=0
        .spics_io_num = EPD_PIN_CS,         // CS 由 SPI 驱动硬件管理
        .queue_size = 7,
    };
    ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &devcfg, &s_spi));
    ESP_LOGI(TAG, "SPI3_HOST 已初始化：CS=%d MOSI=%d SCK=%d 时钟=%d Hz",
             EPD_PIN_CS, EPD_PIN_MOSI, EPD_PIN_SCK, EPD_SPI_CLK_HZ);

    // ---- 3. 硬件复位 ----
    hw_reset();
    int lvl_after_rst = gpio_get_level(EPD_PIN_BUSY);
    ESP_LOGI(TAG, "硬件 RST 完成，BUSY 当前电平=%d", lvl_after_rst);

    // 不强制要求 BUSY 立即变低（之前可能屏处于 hibernate，需要 SW Reset 才完全活）
    // 仅在 200ms 内采样一次最低电平做诊断输出
    int min_before = sample_busy_min(200);
    ESP_LOGI(TAG, "RST 后 200ms 内 BUSY 最低电平=%d", min_before);

    // ---- 4. SW Reset 时序验证：发送 0x12 后采样 BUSY 200ms ----
    ESP_LOGI(TAG, "发送 SW Reset (0x12)...");
    ESP_ERROR_CHECK(send_cmd(SSD1681_CMD_SW_RESET));

    int min_after = sample_busy_min(200);
    ESP_LOGI(TAG, "SW Reset 后 200ms 内 BUSY 最低电平=%d", min_after);

    // 等 BUSY 最终落到低（带超时）
    esp_err_t err = wait_busy_low(EPD_BUSY_TIMEOUT_MS);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "SW Reset 后 BUSY 仍未拉低，超时退出（可能屏未上电或 BUSY 线未接好）");
        return err;
    }

    ESP_LOGI(TAG, "BUSY 已拉低，SPI/DC/RST 时序验证通过");
    return ESP_OK;
}
