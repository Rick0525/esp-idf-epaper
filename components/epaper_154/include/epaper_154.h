// 1.54 寸单色墨水屏（SSD1681 兼容）公开 API
//
// 硬件接线（ESP32-PICO-KIT v4.1，固定不变）：
//   CS  = GPIO5    MOSI = GPIO23   SCK = GPIO18   MISO 不接
//   DC  = GPIO27   RST  = GPIO33   BUSY = GPIO14
//   VCC = 3.3V，共地

#pragma once

#include "esp_err.h"

// 初始化：配置 GPIO + SPI 总线 + 硬件复位 + SW Reset 时序验证
// 节点 1 范围：仅完成到 SW Reset，未做 SSD1681 init 序列
esp_err_t epaper_init(void);
