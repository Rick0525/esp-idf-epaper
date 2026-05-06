// 1.54 寸 GDEW0154T8 单色墨水屏（IL0373 控制器）驱动实现
//
// 完全照搬 Arduino GxEPD2 库 GxEPD2_154_T8 的 init 序列、LUT 数据、
// 刷新流程到 ESP-IDF C 代码。源码参照：
//   /Users/rick/Documents/Arduino/libraries/GxEPD2/src/epd/GxEPD2_154_T8.cpp
//
// 关键差异（相对此前误以为的 SSD1681 实现）：
//   - BUSY 极性反转：IL0373 BUSY=LOW 表示忙、BUSY=HIGH 表示空闲
//   - 命令集完全不同：写 RAM 用 0x10/0x13，刷新用 0x12，深睡用 0x07 0xA5
//   - 必须手动下发 5 张 LUT（vcomDC/ww/bw/wb/bb），OTP 在该屏上不可用
//   - 屏分辨率为 152×152（不是 200×200）

#include "epaper_154.h"
#include "font8x8_basic.h"
#include "il0373_cmd.h"

#include <string.h>

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
#define EPD_SPI_CLK_HZ (4 * 1000 * 1000)   // GxEPD2 默认 4MHz

// IL0373 BUSY 极性：LOW=忙，HIGH=空闲（与 SSD1681 相反！）
#define EPD_BUSY_TIMEOUT_MS 5000

// 帧缓冲：每字节 8 个像素（MSB 对应较小 X）。1=白 0=黑
#define EPD_FB_BYTES ((EPD_W / 8) * EPD_H)
static uint8_t s_framebuf[EPD_FB_BYTES];

static spi_device_handle_t s_spi = NULL;
// 首次刷新需要把 previous frame buffer (0x10) 也填白，否则差分 LUT 会按未知基线刷新
static bool s_initial_refresh = true;
// 旋转：0/1/2/3，纯软件层坐标变换，不下发屏命令
static uint8_t s_rotation = 0;
// 当前是否处于 partial 模式（VCOM 间隔 + LUT 与 full 不同，切换才需重发 init）
static bool s_using_partial_mode = false;

// ---- 全刷 LUT，5 张表，直接复制自 GxEPD2_154_T8.cpp lut_20_vcomDC ~ lut_24_bb ----

static const uint8_t s_lut_vcomDC[] = {
    0x00, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x60, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x00, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

static const uint8_t s_lut_ww[] = {
    0x40, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x40, 0x14, 0x00, 0x00, 0x00, 0x01,
    0xA0, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_lut_bw[] = {
    0x40, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x40, 0x14, 0x00, 0x00, 0x00, 0x01,
    0xA0, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_lut_wb[] = {
    0x80, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x80, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x50, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_lut_bb[] = {
    0x80, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x80, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x50, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// ---- partial LUT，5 张表，直接复制自 GxEPD2_154_T8.cpp lut_*_partial ----
// 关键点：第一行 phase length = Tx19 = 0x20（GxEPD2 注释说原值 0x19=25 太短，0x20=32 更稳）
//        其它 6 行全 0，单步刷新，耗时 ~350ms

#define EPD_PARTIAL_TX19  0x20

static const uint8_t s_lut_vcomDC_partial[] = {
    0x00, EPD_PARTIAL_TX19, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};

static const uint8_t s_lut_ww_partial[] = {
    0x00, EPD_PARTIAL_TX19, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_lut_bw_partial[] = {
    0x80, EPD_PARTIAL_TX19, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_lut_wb_partial[] = {
    0x40, EPD_PARTIAL_TX19, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

static const uint8_t s_lut_bb_partial[] = {
    0x00, EPD_PARTIAL_TX19, 0x01, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};

// 等 BUSY 拉到空闲（IL0373: HIGH=空闲）。超时返回 ESP_ERR_TIMEOUT
static esp_err_t wait_busy_idle(int timeout_ms, const char *hint)
{
    // GxEPD2 _waitWhileBusy 开头有 delay(1) 给屏一点时间进入 busy
    vTaskDelay(pdMS_TO_TICKS(1));
    int waited = 0;
    while (gpio_get_level(EPD_PIN_BUSY) == 0) {       // BUSY=0 表示忙
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
        if (waited >= timeout_ms) {
            ESP_LOGE(TAG, "[%s] BUSY 等待超时 (%d ms)", hint ? hint : "?", timeout_ms);
            return ESP_ERR_TIMEOUT;
        }
    }
    if (hint) ESP_LOGI(TAG, "[%s] 空闲（耗时约 %d ms）", hint, waited);
    return ESP_OK;
}

// 发送一字节命令（DC=0）
static esp_err_t send_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    return spi_device_polling_transmit(s_spi, &t);
}

// 发送 N 字节数据（DC=1）
static esp_err_t send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return ESP_OK;
    gpio_set_level(EPD_PIN_DC, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    return spi_device_polling_transmit(s_spi, &t);
}

// 命令 + N 字节数据
static esp_err_t cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    esp_err_t err = send_cmd(cmd);
    if (err != ESP_OK) return err;
    return send_data(data, len);
}

// 命令 + 单字节数据
static esp_err_t cmd_p1(uint8_t cmd, uint8_t p)
{
    return cmd_data(cmd, &p, 1);
}

// 硬件复位（GxEPD2 默认时序：HIGH 10ms → LOW 10ms → HIGH 10ms）
static void hw_reset(void)
{
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// 基础 init（对应 GxEPD2_154_T8::_InitDisplay）
static esp_err_t il0373_init_display(void)
{
    esp_err_t err;

    // 0x01 POWER SETTING
    {
        const uint8_t p[5] = { 0x03, 0x00, 0x2b, 0x2b, 0x03 };
        if ((err = cmd_data(IL0373_POWER_SETTING, p, sizeof(p))) != ESP_OK) return err;
    }
    // 0x06 BOOSTER SOFT START
    {
        const uint8_t p[3] = { 0x17, 0x17, 0x17 };
        if ((err = cmd_data(IL0373_BOOSTER_SOFT_START, p, sizeof(p))) != ESP_OK) return err;
    }
    // 0x00 PANEL SETTING
    //   0xbf = LUT from register（OTP 在该屏上不可用，必须用 register LUT）
    //   0x0d = VCOM to 0V fast
    {
        const uint8_t p[2] = { 0xbf, 0x0d };
        if ((err = cmd_data(IL0373_PANEL_SETTING, p, sizeof(p))) != ESP_OK) return err;
    }
    // 0x30 PLL: 0x3a = 100Hz
    if ((err = cmd_p1(IL0373_PLL_CONTROL, 0x3a)) != ESP_OK) return err;

    // 0x61 RESOLUTION: WIDTH(8 bits), HEIGHT(16 bits 大端)
    {
        const uint8_t p[3] = { EPD_W, (EPD_H >> 8) & 0xFF, EPD_H & 0xFF };
        if ((err = cmd_data(IL0373_RESOLUTION_SETTING, p, sizeof(p))) != ESP_OK) return err;
    }
    return ESP_OK;
}

// 全刷 init（_InitDisplay + VCOM_DC + 间隔 + 5 张 LUT + Power On）
static esp_err_t il0373_init_full(void)
{
    esp_err_t err;
    if ((err = il0373_init_display()) != ESP_OK) return err;

    // 0x82 VCOM_DC: 0x08
    if ((err = cmd_p1(IL0373_VCOM_DC, 0x08)) != ESP_OK) return err;

    // 0x50 VCOM/DATA INTERVAL: 0x97（全刷模式，白色边框）
    if ((err = cmd_p1(IL0373_VCOM_DATA_INTERVAL, 0x97)) != ESP_OK) return err;

    // 5 张 LUT
    if ((err = cmd_data(IL0373_LUT_VCOM, s_lut_vcomDC, sizeof(s_lut_vcomDC))) != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_WW,   s_lut_ww,     sizeof(s_lut_ww)))     != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_BW,   s_lut_bw,     sizeof(s_lut_bw)))     != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_WB,   s_lut_wb,     sizeof(s_lut_wb)))     != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_BB,   s_lut_bb,     sizeof(s_lut_bb)))     != ESP_OK) return err;

    // 0x04 Power On + 等 BUSY 空闲（典型 ~60ms）
    if ((err = send_cmd(IL0373_POWER_ON)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_MS, "PowerOn")) != ESP_OK) return err;
    s_using_partial_mode = false;
    return ESP_OK;
}

// partial init（_InitDisplay + VCOM_DC + 0x17 间隔 + 5 张 partial LUT + Power On）
// 与 init_full 仅两处不同：
//   - VCOM_DATA_INTERVAL 从 0x97 → 0x17（VBDF 17/D7 vs 全刷 VBDW 97）
//   - LUT 用 partial 系列（phase length 0x20 单步）
static esp_err_t il0373_init_partial(void)
{
    esp_err_t err;
    if ((err = il0373_init_display()) != ESP_OK) return err;

    if ((err = cmd_p1(IL0373_VCOM_DC, 0x08)) != ESP_OK) return err;
    if ((err = cmd_p1(IL0373_VCOM_DATA_INTERVAL, 0x17)) != ESP_OK) return err;

    if ((err = cmd_data(IL0373_LUT_VCOM, s_lut_vcomDC_partial, sizeof(s_lut_vcomDC_partial))) != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_WW,   s_lut_ww_partial,     sizeof(s_lut_ww_partial)))     != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_BW,   s_lut_bw_partial,     sizeof(s_lut_bw_partial)))     != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_WB,   s_lut_wb_partial,     sizeof(s_lut_wb_partial)))     != ESP_OK) return err;
    if ((err = cmd_data(IL0373_LUT_BB,   s_lut_bb_partial,     sizeof(s_lut_bb_partial)))     != ESP_OK) return err;

    if ((err = send_cmd(IL0373_POWER_ON)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_MS, "PowerOn(P)")) != ESP_OK) return err;
    s_using_partial_mode = true;
    return ESP_OK;
}

// 0x90 PARTIAL_WINDOW，发 7 字节：x_lo, xe_lo, y_hi, y_lo, ye_hi, ye_lo, 0x01
// 注意：本屏宽 152 < 256，x 用单字节足够（与 GxEPD2 _setPartialRamArea 一致）
// 调用前 x、w 必须已经 8 对齐（8 bit/byte 寻址）
static esp_err_t il0373_set_partial_window(int x, int y, int w, int h)
{
    int xe = (x + w - 1) | 0x07;   // x_end 包到所在字节末尾
    int ye = y + h - 1;
    const uint8_t p[7] = {
        (uint8_t)(x  & 0xFF),
        (uint8_t)(xe & 0xFF),
        (uint8_t)((y  >> 8) & 0xFF), (uint8_t)(y  & 0xFF),
        (uint8_t)((ye >> 8) & 0xFF), (uint8_t)(ye & 0xFF),
        0x01,
    };
    return cmd_data(IL0373_PARTIAL_WINDOW, p, sizeof(p));
}

esp_err_t epaper_init(void)
{
    ESP_LOGI(TAG, "epaper_init 开始（IL0373 / GDEW0154T8 / %dx%d）", EPD_W, EPD_H);

    // ---- 1. GPIO（DC、RST 输出；BUSY 输入 + 上拉） ----
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
        .pull_up_en = GPIO_PULLUP_ENABLE,   // v6.0 GPIO 驱动不再隐式上拉
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    ESP_ERROR_CHECK(gpio_config(&busy_io));

    gpio_set_level(EPD_PIN_DC, 0);
    gpio_set_level(EPD_PIN_RST, 1);
    ESP_LOGI(TAG, "GPIO 配置完成：DC=%d RST=%d BUSY=%d", EPD_PIN_DC, EPD_PIN_RST, EPD_PIN_BUSY);

    // ---- 2. SPI 总线（仅首次初始化） ----
    if (s_spi == NULL) {
        spi_bus_config_t buscfg = {
            .mosi_io_num = EPD_PIN_MOSI,
            .miso_io_num = -1,
            .sclk_io_num = EPD_PIN_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 4096,   // 152x152/8 = 2888 < 4096
        };
        ESP_ERROR_CHECK(spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = EPD_SPI_CLK_HZ,
            .mode = 0,                          // CPOL=0 CPHA=0
            .spics_io_num = EPD_PIN_CS,         // 硬件 CS（与 GxEPD2 行为等价）
            .queue_size = 7,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &devcfg, &s_spi));
        ESP_LOGI(TAG, "SPI3_HOST 已初始化：CS=%d MOSI=%d SCK=%d 时钟=%d Hz",
                 EPD_PIN_CS, EPD_PIN_MOSI, EPD_PIN_SCK, EPD_SPI_CLK_HZ);
    }

    // ---- 3. 硬件复位（屏从 hibernate 唤醒只能靠硬件 RST） ----
    hw_reset();
    ESP_LOGI(TAG, "硬件 RST 完成，BUSY 当前电平=%d (1=空闲, 0=忙)",
             gpio_get_level(EPD_PIN_BUSY));

    s_initial_refresh = true;
    s_using_partial_mode = false;   // hw_reset 之后控制器内部状态全清
    ESP_LOGI(TAG, "epaper_init 完成");
    return ESP_OK;
}

void epaper_clear(uint8_t color)
{
    memset(s_framebuf, color, sizeof(s_framebuf));
}

void epaper_set_rotation(uint8_t rotation)
{
    s_rotation = rotation & 3;
}

int epaper_width(void)
{
    return (s_rotation & 1) ? EPD_H : EPD_W;
}

int epaper_height(void)
{
    return (s_rotation & 1) ? EPD_W : EPD_H;
}

void epaper_draw_pixel(int x, int y, bool black)
{
    // 边界按当前旋转下的逻辑尺寸判断
    if ((unsigned)x >= (unsigned)epaper_width() ||
        (unsigned)y >= (unsigned)epaper_height()) return;

    // 坐标变换到物理像素（与 Adafruit_GFX/GxEPD2 标准等价）
    int px, py;
    switch (s_rotation) {
        default:
        case 0: px = x;             py = y;             break;
        case 1: px = EPD_W - 1 - y; py = x;             break;
        case 2: px = EPD_W - 1 - x; py = EPD_H - 1 - y; break;
        case 3: px = y;             py = EPD_H - 1 - x; break;
    }

    size_t idx = (size_t)(px >> 3) + (size_t)py * (EPD_W >> 3);
    uint8_t bit = 0x80 >> (px & 7);    // MSB 对应较小物理 X
    if (black) {
        s_framebuf[idx] &= ~bit;       // 0 = 黑
    } else {
        s_framebuf[idx] |= bit;        // 1 = 白
    }
}

void epaper_draw_hline(int x, int y, int len, bool black)
{
    int W = epaper_width(), H = epaper_height();
    if (y < 0 || y >= H) return;
    if (x < 0) { len += x; x = 0; }
    if (x + len > W) len = W - x;
    if (len <= 0) return;
    for (int i = 0; i < len; i++) {
        epaper_draw_pixel(x + i, y, black);
    }
}

void epaper_draw_vline(int x, int y, int len, bool black)
{
    int W = epaper_width(), H = epaper_height();
    if (x < 0 || x >= W) return;
    if (y < 0) { len += y; y = 0; }
    if (y + len > H) len = H - y;
    if (len <= 0) return;
    for (int i = 0; i < len; i++) {
        epaper_draw_pixel(x, y + i, black);
    }
}

void epaper_draw_rect(int x, int y, int w, int h, bool black)
{
    if (w <= 0 || h <= 0) return;
    epaper_draw_hline(x,         y,         w, black);
    epaper_draw_hline(x,         y + h - 1, w, black);
    epaper_draw_vline(x,         y,         h, black);
    epaper_draw_vline(x + w - 1, y,         h, black);
}

void epaper_fill_rect(int x, int y, int w, int h, bool black)
{
    int W = epaper_width(), H = epaper_height();
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > W) w = W - x;
    if (y + h > H) h = H - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        epaper_draw_hline(x, y + row, w, black);
    }
}

void epaper_draw_string_8x8(int x, int y, const char *s, bool black)
{
    if (s == NULL) return;
    int cx = x;
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch >= 128) { cx += 8; continue; }      // 仅支持 ASCII 0-127
        const uint8_t *glyph = font8x8_basic[ch];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            // font8x8 每字节 LSB(bit0) 对应较小 X，与画点 MSB 顺序相反
            for (int col = 0; col < 8; col++) {
                if (bits & (1u << col)) {
                    epaper_draw_pixel(cx + col, y + row, black);
                }
            }
        }
        cx += 8;
    }
}

// ---- GFX 字体渲染（Adafruit_GFX 兼容格式） ----
//
// 字体数据：bitmap[] 是连续 bit stream，glyph[c-first] 给出该字符的元数据：
//   - bitmapOffset：bitmap 中的字节偏移（按 bit 流读 width*height 个 bit）
//   - width / height：glyph 像素尺寸（不含字符间距）
//   - xOffset / yOffset：从光标 (x, baseline) 到 glyph 左上的偏移（通常 yOffset 负）
//   - xAdvance：写完后光标 x 推进多少
// bit 顺序 MSB 先（与帧缓冲一致），按 height 行 × width 列 raster scan

static void draw_char_gfx(int x_baseline, int y_baseline, char c,
                          const GFXfont *font, bool black)
{
    if ((uint8_t)c < font->first || (uint8_t)c > font->last) return;
    const GFXglyph *g = &font->glyph[(uint8_t)c - font->first];
    const uint8_t  *bitmap = font->bitmap;

    uint16_t bo = g->bitmapOffset;
    uint8_t  bits = 0, bit = 0;

    int gx = x_baseline + g->xOffset;
    int gy = y_baseline + g->yOffset;

    for (int yy = 0; yy < g->height; yy++) {
        for (int xx = 0; xx < g->width; xx++) {
            if ((bit++ & 7) == 0) {
                bits = bitmap[bo++];
            }
            if (bits & 0x80) {
                epaper_draw_pixel(gx + xx, gy + yy, black);
            }
            bits <<= 1;
        }
    }
}

void epaper_draw_string_gfx(int x, int y, const char *s, const GFXfont *font, bool black)
{
    if (s == NULL || font == NULL) return;
    int cx = x;
    int cy = y;
    while (*s) {
        char ch = *s++;
        if (ch == '\n') {
            cx = x;
            cy += font->yAdvance;
            continue;
        }
        if ((uint8_t)ch < font->first || (uint8_t)ch > font->last) {
            // 跳过不支持的字符，但保留它的间距（用 first 字符的 xAdvance 兜底）
            cx += font->glyph[0].xAdvance;
            continue;
        }
        draw_char_gfx(cx, cy, ch, font, black);
        cx += font->glyph[(uint8_t)ch - font->first].xAdvance;
    }
}

void epaper_draw_bitmap(int x, int y, const uint8_t *bmp, int w, int h, bool black)
{
    if (bmp == NULL || w <= 0 || h <= 0) return;
    int row_bytes = (w + 7) / 8;
    for (int row = 0; row < h; row++) {
        const uint8_t *line = &bmp[row * row_bytes];
        for (int col = 0; col < w; col++) {
            uint8_t byte = line[col >> 3];
            uint8_t bit  = 0x80u >> (col & 7);   // MSB-first，与帧缓冲一致
            if (byte & bit) {
                // bit=1 涂 black 指定色；bit=0 不动（透明语义）
                epaper_draw_pixel(x + col, y + row, black);
            }
        }
    }
}

void epaper_get_text_bounds_gfx(const char *s, const GFXfont *font, int *out_w, int *out_h)
{
    if (s == NULL || font == NULL) {
        if (out_w) *out_w = 0;
        if (out_h) *out_h = 0;
        return;
    }
    int total_w = 0;
    while (*s) {
        char ch = *s++;
        if (ch == '\n') continue;
        if ((uint8_t)ch < font->first || (uint8_t)ch > font->last) {
            total_w += font->glyph[0].xAdvance;
            continue;
        }
        total_w += font->glyph[(uint8_t)ch - font->first].xAdvance;
    }
    // 字体高度近似 = yAdvance；上层用 baseline + yAdvance 定位时这是对的近似
    if (out_w) *out_w = total_w;
    if (out_h) *out_h = font->yAdvance;
}

esp_err_t epaper_display_full(void)
{
    esp_err_t err;

    // ---- 1. 完整 init + LUT + Power On ----
    if ((err = il0373_init_full()) != ESP_OK) {
        ESP_LOGE(TAG, "il0373_init_full 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "init_full 完成（含 5 张 LUT 与 Power On）");

    // ---- 2. 0x13 写当前帧 ----
    if ((err = send_cmd(IL0373_DTM2)) != ESP_OK) return err;
    if ((err = send_data(s_framebuf, sizeof(s_framebuf))) != ESP_OK) {
        ESP_LOGE(TAG, "写当前帧失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "已写当前帧 (DTM2) %d 字节", (int)sizeof(s_framebuf));

    // ---- 3. 首次刷新还要写 previous 帧 ----
    //   GxEPD2 在 _initial_refresh 时会发 0x10 + 全 0xFF，给差分 LUT 一个"上次=白"的基线，
    //   否则首次刷新结果不可控。后续刷新由控制器自己内部翻页，不再需要。
    if (s_initial_refresh) {
        uint8_t white[64];
        memset(white, 0xFF, sizeof(white));
        if ((err = send_cmd(IL0373_DTM1)) != ESP_OK) return err;
        for (size_t sent = 0; sent < EPD_FB_BYTES; ) {
            size_t n = (EPD_FB_BYTES - sent) < sizeof(white) ? (EPD_FB_BYTES - sent) : sizeof(white);
            if ((err = send_data(white, n)) != ESP_OK) return err;
            sent += n;
        }
        ESP_LOGI(TAG, "已写 previous 帧 (DTM1=0xFF) %d 字节", EPD_FB_BYTES);
    }

    // ---- 4. 0x12 触发刷新（约 1.6 秒） ----
    if ((err = send_cmd(IL0373_DISPLAY_REFRESH)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_MS, "DisplayRefresh")) != ESP_OK) return err;
    ESP_LOGI(TAG, "全刷完成");

    s_initial_refresh = false;
    return ESP_OK;
}

esp_err_t epaper_display_partial(int x, int y, int w, int h)
{
    esp_err_t err;

    // 首次刷新必须 full（partial LUT 是差分，需要 full 建立基线）
    // GxEPD2 行为也是把 _initial_refresh 时的 partial 自动转为 full
    if (s_initial_refresh) {
        ESP_LOGW(TAG, "首次刷新自动转 full（partial 需要先建立基线）");
        return epaper_display_full();
    }

    // ---- 1. 物理屏裁剪 ----
    if (w <= 0 || h <= 0) return ESP_OK;
    int x1 = x, y1 = y, w1 = w, h1 = h;
    if (x1 < 0) { w1 += x1; x1 = 0; }
    if (y1 < 0) { h1 += y1; y1 = 0; }
    if (x1 + w1 > EPD_W) w1 = EPD_W - x1;
    if (y1 + h1 > EPD_H) h1 = EPD_H - y1;
    if (w1 <= 0 || h1 <= 0) return ESP_OK;

    // ---- 2. x、w 强制 8 对齐（IL0373 partial RAM 寻址按字节） ----
    //   - x 向下对齐到 8 边界
    //   - w 先把对齐过程吞掉的偏移补回去，再向上对齐到 8 边界
    int dx = x1 & 7;
    x1 -= dx;
    w1 += dx;
    if (w1 & 7) w1 = (w1 + 7) & ~7;
    if (x1 + w1 > EPD_W) w1 = EPD_W - x1;
    if (w1 <= 0) return ESP_OK;

    // ---- 3. 切到 partial 模式（仅在状态变更时才重发 init） ----
    if (!s_using_partial_mode) {
        if ((err = il0373_init_partial()) != ESP_OK) {
            ESP_LOGE(TAG, "il0373_init_partial 失败: %s", esp_err_to_name(err));
            return err;
        }
        ESP_LOGI(TAG, "已切到 partial 模式");
    }

    const int row_bytes = EPD_W / 8;
    const int win_bytes = w1 / 8;

    // ---- 4. 双写：write+refresh+write（避免下次 partial 翻页 ghost） ----
    //   IL0373 partial 刷新后控制器内部 current/previous 角色互换，
    //   不再写一次会导致下一次 partial 用过期数据做差分基线
    for (int pass = 0; pass < 2; pass++) {
        if ((err = send_cmd(IL0373_PARTIAL_IN)) != ESP_OK) return err;
        if ((err = il0373_set_partial_window(x1, y1, w1, h1)) != ESP_OK) return err;

        // 0x13 写当前帧的窗口数据，按行从 framebuf 提取
        if ((err = send_cmd(IL0373_DTM2)) != ESP_OK) return err;
        for (int row = 0; row < h1; row++) {
            const uint8_t *src = &s_framebuf[(x1 >> 3) + (y1 + row) * row_bytes];
            if ((err = send_data(src, win_bytes)) != ESP_OK) return err;
        }

        if (pass == 0) {
            // 仅第一次写完触发刷新
            if ((err = send_cmd(IL0373_DISPLAY_REFRESH)) != ESP_OK) return err;
            if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_MS, "PartialRefresh")) != ESP_OK) return err;
        }

        if ((err = send_cmd(IL0373_PARTIAL_OUT)) != ESP_OK) return err;
    }

    ESP_LOGI(TAG, "partial 完成 (x=%d y=%d w=%d h=%d)", x1, y1, w1, h1);
    return ESP_OK;
}

esp_err_t epaper_sleep(void)
{
    esp_err_t err;

    // 0x02 Power Off + 等 BUSY 空闲
    if ((err = send_cmd(IL0373_POWER_OFF)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_MS, "PowerOff")) != ESP_OK) return err;
    s_using_partial_mode = false;

    // 0x07 0xA5 进入 Deep Sleep（参数必须是 0xA5）
    if ((err = cmd_p1(IL0373_DEEP_SLEEP, 0xA5)) != ESP_OK) {
        ESP_LOGE(TAG, "Deep Sleep 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "已进入 Deep Sleep");
    return ESP_OK;
}
