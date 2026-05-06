// 主程序入口
// 节点 10：bitmap 演示
//   - 硬编码一份 16×16 上箭头 bitmap（共 32 字节）
//   - 同一份数据贴 5 处：屏中央 + 4 角
//   - 中央那份外加 draw_rect 边框，验证 bitmap 与图形基元能叠加
//   - 顶部 8×8 标题、底部说明

#include "epaper_154.h"
#include "esp_log.h"

static const char *TAG = "MAIN";

// 16×16 上箭头：每行 2 字节 × 16 行 = 32 字节
// 数据按行 raster scan，行内 MSB 对应较小 x（与帧缓冲一致）
//
// 注释里的 #/. 直观显示像素：# = bit 1（要画）、. = bit 0（透明）
static const uint8_t arrow_up_16x16[] = {
    0x01, 0x80,  // .......##.......
    0x03, 0xC0,  // ......####......
    0x07, 0xE0,  // .....######.....
    0x0F, 0xF0,  // ....########....
    0x1F, 0xF8,  // ...##########...
    0x3F, 0xFC,  // ..############..
    0x7F, 0xFE,  // .##############.
    0xFF, 0xFF,  // ################
    0x03, 0xC0,  // ......####......
    0x03, 0xC0,  // ......####......
    0x03, 0xC0,  // ......####......
    0x03, 0xC0,  // ......####......
    0x03, 0xC0,  // ......####......
    0x03, 0xC0,  // ......####......
    0x03, 0xC0,  // ......####......
    0x03, 0xC0,  // ......####......
};

static void draw_arrow(int x, int y)
{
    epaper_draw_bitmap(x, y, arrow_up_16x16, 16, 16, true);
}

void app_main(void)
{
    ESP_LOGI(TAG, "boot ok");

    ESP_ERROR_CHECK(epaper_init());
    epaper_set_rotation(0);

    epaper_clear(0xFF);

    // 顶部标题
    epaper_draw_string_8x8(8, 4, "Bitmap demo", true);
    epaper_draw_hline(0, 16, EPD_W, true);

    // 同一份 16×16 bitmap 贴 5 处
    // 1) 屏中央
    const int cx = (EPD_W - 16) / 2;   // = 68
    const int cy = (EPD_H - 16) / 2;   // = 68
    draw_arrow(cx, cy);

    // 中央那份外加方框（演示 bitmap 与图形基元叠加）
    epaper_draw_rect(cx - 4, cy - 4, 16 + 8, 16 + 8, true);

    // 2)-5) 4 个角
    draw_arrow(8,           24);              // 左上
    draw_arrow(EPD_W - 24,  24);              // 右上
    draw_arrow(8,           EPD_H - 24);      // 左下
    draw_arrow(EPD_W - 24,  EPD_H - 24);      // 右下

    // 说明文字：放在中央方框正下方（避开左下角箭头）
    // "x5 same data" = 12 字符 × 8 = 96px 宽，居中 x = (152-96)/2 = 28
    epaper_draw_string_8x8(28, cy + 16 + 8, "x5 same data", true);

    ESP_ERROR_CHECK(epaper_display_full());
    ESP_ERROR_CHECK(epaper_sleep());

    ESP_LOGI(TAG, "节点 10 完成：bitmap 已上屏");
}
