// 4.2 寸 BWR（黑/白/红）墨水屏驱动实现
// 控制器 UC8176 / UC8276 / IL0398 系列（命令集兼容）
// init 序列、命令时序参照 Good Display GDEW042Z15 官方 Arduino demo：
//   /Users/rick/Documents/ESP32Projects/4.2arduino_BWR/4.2/4.2/4.2.ino
//
// 关键参数：
//   - 分辨率 400×300，每色 1bpp，黑层 + 红层共 30000 字节
//   - 波形走 OTP（PSR=0x0f），不需要写 LUT
//   - BUSY 极性：HIGH=空闲、LOW=忙
//   - 写 RAM：0x10=黑层、0x13=红层；触发：0x12；睡眠：0x07 0xA5
//   - 不支持局部刷新（硬件限制 + OTP 只给全刷波形）
//
// 像素编码（硬件实测：BW=0xFF + RED=0xFF 显示纯红，证明红层 bit=1 是"红色 ON"）：
//   颜色      黑层 (0x10) bit   红层 (0x13) bit
//   WHITE     1                 0       （BW byte 0xFF, RED byte 0x00）
//   BLACK     0                 0       （BW byte 0x00, RED byte 0x00）
//   RED       任意              1       （RED byte 0xFF；红色优先，BW 层被覆盖）
// 注意：Arduino demo 的 PIC_display_Clean 注释说 0xFF/0xFF 是"白屏"是错的，没人测过。

#include "epaper_42bwr.h"
#include "font8x8_basic.h"
#include "uc8276_cmd.h"

#include <string.h>

#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "EPD42";

// ---- 引脚定义（与 1.54" IL0373 共用同一组 GPIO，物理换屏即可） ----
#define EPD_PIN_CS   5
#define EPD_PIN_MOSI 23
#define EPD_PIN_SCK  18
#define EPD_PIN_DC   27
#define EPD_PIN_RST  33
#define EPD_PIN_BUSY 14

#define EPD_SPI_HOST   SPI3_HOST
#define EPD_SPI_CLK_HZ (10 * 1000 * 1000)   // Arduino demo 用 10MHz

// BUSY 等待超时（4.2" 比 1.54" 慢得多，全刷 ~5s）
#define EPD_BUSY_TIMEOUT_PON_MS  3000
#define EPD_BUSY_TIMEOUT_POF_MS  3000
#define EPD_BUSY_TIMEOUT_DRF_MS  30000

#define EPD_FB_BYTES ((EPD42_W / 8) * EPD42_H)   // 50 * 300 = 15000

// 双 framebuffer：黑层 (DTM1=0x10) + 红层 (DTM2=0x13)
static uint8_t s_fb_black[EPD_FB_BYTES];
static uint8_t s_fb_red[EPD_FB_BYTES];

static spi_device_handle_t s_spi = NULL;

// ---- BUSY 等待（HIGH=空闲、LOW=忙） ----
static esp_err_t wait_busy_idle(int timeout_ms, const char *hint)
{
    vTaskDelay(pdMS_TO_TICKS(1));   // 给屏一点时间进入 busy
    int waited = 0;
    while (gpio_get_level(EPD_PIN_BUSY) == 0) {
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

// ---- SPI 收发 ----
static esp_err_t send_cmd(uint8_t cmd)
{
    gpio_set_level(EPD_PIN_DC, 0);
    spi_transaction_t t = { .length = 8, .tx_buffer = &cmd };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t send_data(const uint8_t *data, size_t len)
{
    if (len == 0) return ESP_OK;
    gpio_set_level(EPD_PIN_DC, 1);
    spi_transaction_t t = { .length = len * 8, .tx_buffer = data };
    return spi_device_polling_transmit(s_spi, &t);
}

static esp_err_t cmd_data(uint8_t cmd, const uint8_t *data, size_t len)
{
    esp_err_t err = send_cmd(cmd);
    if (err != ESP_OK) return err;
    return send_data(data, len);
}

static esp_err_t cmd_p1(uint8_t cmd, uint8_t p)
{
    return cmd_data(cmd, &p, 1);
}

// ---- 硬件复位（Arduino demo 与 GxEPD2 同时序：HIGH 10ms → LOW 10ms → HIGH 10ms） ----
static void hw_reset(void)
{
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(10));
    gpio_set_level(EPD_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(10));
}

// ---- UC8276 init ----
// 极简版本，参照 GxEPD2 epd3c/GxEPD2_420c_Z21.cpp（控制器 UC8276，面板 GDEQ042Z21）：
//   只发 PSR=0x0f + PON。其余参数全部走 OTP 默认。
// 如果 PON 后 DRF 卡 BUSY，再尝试加 BTST/RES/CDI（GxEPD2_420c.cpp 的 IL0398/Z15 序列）。
static esp_err_t uc8276_init(void)
{
    esp_err_t err;

    // 0x00 PANEL SETTING：0x0f = LUT from OTP, 400x300, KWR 三色
    if ((err = cmd_p1(UC8276_PANEL_SETTING, 0x0f)) != ESP_OK) return err;

    // 0x04 POWER ON + 等 BUSY 空闲
    if ((err = send_cmd(UC8276_POWER_ON)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_PON_MS, "PowerOn")) != ESP_OK) return err;

    return ESP_OK;
}

esp_err_t epaper_42_init(void)
{
    ESP_LOGI(TAG, "epaper_42_init 开始（UC8276 / 4.2\" BWR / %dx%d）", EPD42_W, EPD42_H);

    // ---- 1. GPIO ----
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

    // ---- 2. SPI 总线（仅首次） ----
    if (s_spi == NULL) {
        spi_bus_config_t buscfg = {
            .mosi_io_num = EPD_PIN_MOSI,
            .miso_io_num = -1,
            .sclk_io_num = EPD_PIN_SCK,
            .quadwp_io_num = -1,
            .quadhd_io_num = -1,
            .max_transfer_sz = 16384,   // 单次最大写 15000 字节（一整层 framebuffer）
        };
        ESP_ERROR_CHECK(spi_bus_initialize(EPD_SPI_HOST, &buscfg, SPI_DMA_CH_AUTO));

        spi_device_interface_config_t devcfg = {
            .clock_speed_hz = EPD_SPI_CLK_HZ,
            .mode = 0,
            .spics_io_num = EPD_PIN_CS,
            .queue_size = 7,
        };
        ESP_ERROR_CHECK(spi_bus_add_device(EPD_SPI_HOST, &devcfg, &s_spi));
        ESP_LOGI(TAG, "SPI3_HOST 已初始化：CS=%d MOSI=%d SCK=%d 时钟=%d Hz",
                 EPD_PIN_CS, EPD_PIN_MOSI, EPD_PIN_SCK, EPD_SPI_CLK_HZ);
    }

    // ---- 3. 硬件复位 ----
    hw_reset();
    ESP_LOGI(TAG, "硬件 RST 完成，BUSY 当前电平=%d (1=空闲, 0=忙)",
             gpio_get_level(EPD_PIN_BUSY));

    // 帧缓冲默认填白：黑层 0xFF（白）+ 红层 0x00（无红）
    memset(s_fb_black, 0xFF, sizeof(s_fb_black));
    memset(s_fb_red,   0x00, sizeof(s_fb_red));

    ESP_LOGI(TAG, "epaper_42_init 完成");
    return ESP_OK;
}

// ---- 颜色到双层位的映射（硬件实测，红层 bit=1 才显红） ----
//   WHITE: 黑层=1, 红层=0 (BW byte 0xFF, RED byte 0x00)
//   BLACK: 黑层=0, 红层=0 (BW byte 0x00, RED byte 0x00)
//   RED  : 黑层=1, 红层=1 (BW byte 0xFF, RED byte 0xFF) —— BW 层置 1 是为了让单像素 draw 时不引入意外的黑
static inline void color_to_bytes(epd42_color_t c, uint8_t *bw, uint8_t *red)
{
    switch (c) {
        case EPD42_BLACK: *bw = 0x00; *red = 0x00; break;
        case EPD42_RED:   *bw = 0xFF; *red = 0xFF; break;
        case EPD42_WHITE:
        default:          *bw = 0xFF; *red = 0x00; break;
    }
}

void epaper_42_clear(epd42_color_t color)
{
    uint8_t bw, red;
    color_to_bytes(color, &bw, &red);
    memset(s_fb_black, bw,  sizeof(s_fb_black));
    memset(s_fb_red,   red, sizeof(s_fb_red));
}

void epaper_42_draw_pixel(int x, int y, epd42_color_t c)
{
    if ((unsigned)x >= EPD42_W || (unsigned)y >= EPD42_H) return;

    size_t  idx = (size_t)(x >> 3) + (size_t)y * (EPD42_W >> 3);
    uint8_t bit = 0x80u >> (x & 7);   // MSB 对应较小 X

    switch (c) {
        case EPD42_BLACK:
            s_fb_black[idx] &= ~bit;     // 黑层 bit 清 0（黑）
            s_fb_red[idx]   &= ~bit;     // 红层 bit 清 0（无红）
            break;
        case EPD42_RED:
            s_fb_black[idx] |=  bit;     // 黑层 bit 置 1（白底）
            s_fb_red[idx]   |=  bit;     // 红层 bit 置 1（红色 ON，覆盖黑层）
            break;
        case EPD42_WHITE:
        default:
            s_fb_black[idx] |=  bit;     // 黑层 bit 置 1（白）
            s_fb_red[idx]   &= ~bit;     // 红层 bit 清 0（无红）
            break;
    }
}

void epaper_42_draw_hline(int x, int y, int len, epd42_color_t c)
{
    if (y < 0 || y >= EPD42_H) return;
    if (x < 0) { len += x; x = 0; }
    if (x + len > EPD42_W) len = EPD42_W - x;
    if (len <= 0) return;
    for (int i = 0; i < len; i++) {
        epaper_42_draw_pixel(x + i, y, c);
    }
}

void epaper_42_draw_vline(int x, int y, int len, epd42_color_t c)
{
    if (x < 0 || x >= EPD42_W) return;
    if (y < 0) { len += y; y = 0; }
    if (y + len > EPD42_H) len = EPD42_H - y;
    if (len <= 0) return;
    for (int i = 0; i < len; i++) {
        epaper_42_draw_pixel(x, y + i, c);
    }
}

void epaper_42_draw_rect(int x, int y, int w, int h, epd42_color_t c)
{
    if (w <= 0 || h <= 0) return;
    epaper_42_draw_hline(x,         y,         w, c);
    epaper_42_draw_hline(x,         y + h - 1, w, c);
    epaper_42_draw_vline(x,         y,         h, c);
    epaper_42_draw_vline(x + w - 1, y,         h, c);
}

void epaper_42_fill_rect(int x, int y, int w, int h, epd42_color_t c)
{
    if (w <= 0 || h <= 0) return;
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > EPD42_W) w = EPD42_W - x;
    if (y + h > EPD42_H) h = EPD42_H - y;
    if (w <= 0 || h <= 0) return;
    for (int row = 0; row < h; row++) {
        epaper_42_draw_hline(x, y + row, w, c);
    }
}

void epaper_42_draw_string_8x8(int x, int y, const char *s, epd42_color_t c)
{
    if (s == NULL) return;
    int cx = x;
    while (*s) {
        unsigned char ch = (unsigned char)*s++;
        if (ch >= 128) { cx += 8; continue; }
        const uint8_t *glyph = font8x8_basic[ch];
        for (int row = 0; row < 8; row++) {
            uint8_t bits = glyph[row];
            for (int col = 0; col < 8; col++) {
                if (bits & (1u << col)) {           // font8x8 LSB 在左
                    epaper_42_draw_pixel(cx + col, y + row, c);
                }
            }
        }
        cx += 8;
    }
}

// ---- GFX 字体渲染（Adafruit_GFX 兼容格式，bit MSB-first） ----
static void draw_char_gfx(int x_baseline, int y_baseline, char ch,
                          const GFXfont *font, epd42_color_t c)
{
    if ((uint8_t)ch < font->first || (uint8_t)ch > font->last) return;
    const GFXglyph *g = &font->glyph[(uint8_t)ch - font->first];
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
                epaper_42_draw_pixel(gx + xx, gy + yy, c);
            }
            bits <<= 1;
        }
    }
}

void epaper_42_draw_string_gfx(int x, int y, const char *s, const GFXfont *font, epd42_color_t c)
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
            cx += font->glyph[0].xAdvance;
            continue;
        }
        draw_char_gfx(cx, cy, ch, font, c);
        cx += font->glyph[(uint8_t)ch - font->first].xAdvance;
    }
}

void epaper_42_get_text_bounds_gfx(const char *s, const GFXfont *font, int *out_w, int *out_h)
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
    if (out_w) *out_w = total_w;
    if (out_h) *out_h = font->yAdvance;
}

esp_err_t epaper_42_display_full(void)
{
    esp_err_t err;

    // 1. init + Power On
    if ((err = uc8276_init()) != ESP_OK) {
        ESP_LOGE(TAG, "uc8276_init 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "init 完成（含 PowerOn）");

    // 2. 写黑层 (0x10)
    if ((err = send_cmd(UC8276_DTM1)) != ESP_OK) return err;
    if ((err = send_data(s_fb_black, sizeof(s_fb_black))) != ESP_OK) {
        ESP_LOGE(TAG, "写黑层失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "已写黑层 (DTM1) %d 字节", (int)sizeof(s_fb_black));

    // 3. 写红层 (0x13)
    if ((err = send_cmd(UC8276_DTM2)) != ESP_OK) return err;
    if ((err = send_data(s_fb_red, sizeof(s_fb_red))) != ESP_OK) {
        ESP_LOGE(TAG, "写红层失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "已写红层 (DTM2) %d 字节", (int)sizeof(s_fb_red));

    // 4. 触发刷新（典型 ~5s）
    if ((err = send_cmd(UC8276_DISPLAY_REFRESH)) != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(10));   // Arduino demo 注释：至少 200us，给 10ms 余量
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_DRF_MS, "DisplayRefresh")) != ESP_OK) return err;
    ESP_LOGI(TAG, "全刷完成");

    return ESP_OK;
}

// 仅关 DC-DC，寄存器/RAM 全保留
esp_err_t epaper_42_power_off(void)
{
    esp_err_t err;
    if ((err = send_cmd(UC8276_POWER_OFF)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_POF_MS, "PowerOff")) != ESP_OK) return err;
    ESP_LOGD(TAG, "DC-DC 已关闭");
    return ESP_OK;
}

esp_err_t epaper_42_power_on(void)
{
    esp_err_t err;
    if ((err = send_cmd(UC8276_POWER_ON)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_PON_MS, "PowerOn")) != ESP_OK) return err;
    ESP_LOGD(TAG, "DC-DC 已开启");
    return ESP_OK;
}

esp_err_t epaper_42_sleep(void)
{
    esp_err_t err;

    // 1. Power Off + 等 BUSY
    if ((err = send_cmd(UC8276_POWER_OFF)) != ESP_OK) return err;
    if ((err = wait_busy_idle(EPD_BUSY_TIMEOUT_POF_MS, "PowerOff")) != ESP_OK) return err;

    // 2. 1s 延时（Arduino demo 注释：必须，砍掉会有问题）
    vTaskDelay(pdMS_TO_TICKS(1000));

    // 3. Deep Sleep（参数 0xA5）
    if ((err = cmd_p1(UC8276_DEEP_SLEEP, 0xA5)) != ESP_OK) {
        ESP_LOGE(TAG, "Deep Sleep 失败: %s", esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "已进入 Deep Sleep");
    return ESP_OK;
}
