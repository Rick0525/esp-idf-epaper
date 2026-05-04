# 墨水屏 Arduino → ESP-IDF v6.0.1 移植计划

## Context（背景）

用户原有一个基于 Arduino + GxEPD2 库的 ESP32 墨水屏测试项目，位于 `/Users/rick/Documents/esp32教程/ink_test/ink_test.ino`。该项目使用 `GxEPD2_154_T8`（1.54" 单色，控制器 SSD1681 兼容）显示 helloWorld、字体、位图与部分刷新等多个 demo。

为了脱离 Arduino 框架、获得更好的可裁剪性与对硬件的直接控制，需要把它移植到 ESP-IDF v6.0.1。目标工作目录是空目录 `/Users/rick/Documents/ESP32Projects/eink_screen`（已建好 `CLAUDE.md`：要求中文注释、git 原子化提交、固定的 IDF 激活与构建命令）。

本次移植以**最小可用**为目标：跑通 SPI/GPIO 初始化、SSD1681 驱动、单色帧缓冲、基础绘图与一次 helloWorld 全屏刷新。从零手写 C 驱动，不引入第三方组件。

## 已确认前提

- 主控：**ESP32-PICO-KIT v4.1**（ESP32-PICO-D4 模块）；`idf.py set-target esp32`
- 接线（**用户要求保持不变**）：CS=GPIO5，MOSI=GPIO23，SCK=GPIO18，MISO 不接，DC=GPIO27，RST=GPIO33，BUSY=GPIO14；3.3V 供电，共地
- 屏幕：1.54" 黑白墨水屏，控制器 **SSD1681** 系列；分辨率先按 200×200 写，若实际为 152×152 在节点 4 调整 `EPD_W/EPD_H` 宏
- IDF 版本：v6.0.1（路径 `/Users/rick/.espressif/v6.0.1/esp-idf`）
- 构建命令：`source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py set-target esp32 && idf.py build`
- **开发板已连接电脑**：每节点完成后由我直接 `idf.py -p <port> flash monitor` 烧录验证（节点开始前用 `ls /dev/cu.*` 自动探测端口）

### ESP32-PICO-KIT v4.1 引脚兼容性核对

- GPIO5：strapping pin，默认上拉，作 SPI CS 由驱动接管，OK
- GPIO18/23：标准 VSPI SCK/MOSI，OK
- GPIO27：通用 GPIO，作 DC，OK
- GPIO33：RTC GPIO（非 input-only），可输出，作 RST，OK
- GPIO14：非 strapping，作 BUSY 输入，需 `gpio_set_pull_mode(14, GPIO_PULLUP_ONLY)`，OK
- 不冲突的内部占用：GPIO6-11 是模组内 SPI Flash（ESP32-PICO-D4 集成 4MB flash），GPIO16/17 在 PICO-D4 上未被 PSRAM 占用（PICO-D4 无 PSRAM），均不影响

## 项目目录结构

```
/Users/rick/Documents/ESP32Projects/eink_screen/
├── CLAUDE.md                       # 已存在，规范约束
├── README.md                       # 项目说明、接线图、使用方法
├── CMakeLists.txt                  # 顶层 project(eink_screen)
├── sdkconfig.defaults              # CONFIG_IDF_TARGET=esp32 等默认
├── docs/
│   └── PLAN.md                     # 本计划落盘版（中文）
│   └── CHANGELOG.md                # 每个原子提交对应的功能记录
├── main/
│   ├── CMakeLists.txt              # idf_component_register SRCS "main.c" REQUIRES "epaper_154 esp_log freertos"
│   └── main.c                      # app_main 流程
└── components/
    └── epaper_154/
        ├── CMakeLists.txt          # REQUIRES "esp_driver_spi esp_driver_gpio esp_log freertos"
        ├── include/
        │   └── epaper_154.h        # 公开 API
        ├── epaper_154.c            # 驱动实现 + 帧缓冲 + 画图
        ├── ssd1681_cmd.h           # 命令字节宏
        └── font8x8_basic.h         # 嵌入字体表（公共域）
```

## SSD1681 驱动设计要点

**波形 LUT**：本项目**不写自定义 LUT**。`0x22 0xF7` 触发的是 SSD1681 内置 OTP 波形（厂家烧录），用于全刷已足够。后续若要做快速部分刷新再考虑下发自定义 LUT，本次范围之外。

**BUSY 等待**：所有 `wait_busy` 调用必须带超时（默认 5s），超时返回 `ESP_ERR_TIMEOUT` 并 `ESP_LOGE(TAG, "BUSY timeout @ %s")`，**禁止死循环**。

**最少命令集**（封装为宏，定义在 `ssd1681_cmd.h`）：
- `0x12` SW Reset
- `0x01` Driver Output Control（参数 0xC7,0x00,0x00 表示 200 行）
- `0x11` Data Entry Mode（参数 0x03：X 自增、Y 自增）
- `0x44/0x45` 设置 X/Y RAM 范围；`0x4E/0x4F` 设置 RAM 起始指针
- `0x3C` Border Waveform（0x05）
- `0x18` Temperature Sensor（0x80 内部传感器）
- `0x21` Display Update Control 1（0x00,0x80）
- `0x22` Display Update Control 2（0xF7 全刷模式）
- `0x24` 写黑白 RAM
- `0x20` Master Activation
- `0x10` Deep Sleep（0x01）

**全刷流程**：硬件 RST 拉低 10ms → 释放 10ms → 等 BUSY 低 → 0x12 SW Reset → 等 BUSY → 写初始化序列 → 0x24 + 帧缓冲整片 → 0x22 0xF7 → 0x20 → 等 BUSY（典型 2~3 秒）→ 0x10 0x01 进入深睡。

**BUSY 检测**：`while (gpio_get_level(BUSY)==1) { vTaskDelay(pdMS_TO_TICKS(10)); if (++cnt>500) return ESP_ERR_TIMEOUT; }`。SSD1681 BUSY 高表示忙。

**SPI 命令/数据封装**：手动 GPIO 控制 DC（命令前 `gpio_set_level(DC,0)`、数据前置 1）。CS 交给 SPI 驱动硬件管理（`spics_io_num=5`）。SPI mode 0、MSB first、起步 4 MHz。

## 公开 API（`epaper_154.h`）

```c
esp_err_t epaper_init(void);
void      epaper_clear(uint8_t color);          // 0xFF 白 / 0x00 黑
void      epaper_draw_pixel(int x, int y, bool black);
void      epaper_draw_hline(int x, int y, int len, bool black);
void      epaper_draw_string_8x8(int x, int y, const char *s, bool black);
esp_err_t epaper_display_full(void);
esp_err_t epaper_sleep(void);
```

## 帧缓冲与坐标系

- 宏：`EPD_W 200`、`EPD_H 200`（节点 4 后若实际屏是 152×152 再改）
- 缓冲：`static uint8_t framebuf[(EPD_W/8)*EPD_H]`，5KB，放 `.bss`
- 位序：每字节 MSB 对应较小 X，与 SSD1681 RAM 布局一致；1=白 / 0=黑
- 画点：`byte_idx = (x>>3) + y*(EPD_W>>3); bit = 0x80 >> (x&7); black? buf[idx]&=~bit : buf[idx]|=bit;`

## 8x8 字体

直接嵌入 dhepper/font8x8 的 `font8x8_basic.h`（公共域，ASCII 0~127，约 1KB），格式 `static const uint8_t font8x8_basic[128][8]`。**注意**：该字体每字节 LSB 在左，画字符时按位扫描需 `bit = 0x01 << col`，与画点 MSB 顺序不同，不要混淆。文件放 `components/epaper_154/font8x8_basic.h`。

## app_main 流程

```c
ESP_LOGI(TAG, "boot");
ESP_ERROR_CHECK(epaper_init());
epaper_clear(0xFF);
epaper_draw_string_8x8(10, 10, "Hello, IDF!", true);
epaper_draw_hline(0, 30, 200, true);
ESP_ERROR_CHECK(epaper_display_full());
ESP_ERROR_CHECK(epaper_sleep());
```

## 原子化 git 提交节点

| 节点 | 内容 | 提交信息 | 验证 |
|---|---|---|---|
| 0 | `git init`、`.gitignore`（含 `build/`、`sdkconfig`、`sdkconfig.old`、`managed_components/`、`dependencies.lock`、`.DS_Store`）、`sdkconfig.defaults`（`CONFIG_IDF_TARGET="esp32"`、`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y`、`CONFIG_LOG_DEFAULT_LEVEL_INFO=y`）、骨架文件、空 `app_main` 打印 "boot ok"、`docs/PLAN.md` 落盘 | `chore: 初始化 ESP-IDF 项目骨架` | `idf.py build` 通过 → `flash monitor` 看到 "boot ok" |
| 1 | SPI 总线 + GPIO 配置 + RST 脉冲；**发送 `0x12` SW Reset 命令并观察 BUSY 短暂变高再恢复**（验证 SPI 通路 + DC 切换 + RST 时序，比单读电平有说服力）；超时则 `ESP_LOGE` 且返回错误 | `feat(epd): 完成 SPI/GPIO 初始化与 SW Reset 时序验证` | flash monitor 看到 "BUSY rose then fell after SW reset" 日志 |
| 2 | SSD1681 完整 init 序列、整屏写 0xFF、触发全刷、**全刷完立即调用 `epaper_sleep`**（防残影/省电）；BUSY 等待全部带超时与 `ESP_LOGE` | `feat(epd): 实现 SSD1681 初始化、白屏刷新与深睡` | 屏变全白后串口看到 "sleep ok"；触摸屏背面驱动 IC 不再发热 |
| 3 | 帧缓冲 + draw_pixel/hline；**绘制分辨率诊断图样**：四角各 4×4 黑块、屏中横线、屏中纵线 | `feat(epd): 添加帧缓冲与分辨率诊断图样` | 屏显示四角块 + 十字线；据此**确定实际分辨率是 200×200 还是 152×152**，更新 `EPD_W/EPD_H` 宏（如需要） |
| 4 | 嵌入 font8x8 + draw_string + helloWorld | `feat(epd): 添加 8x8 字体与 helloWorld 演示` | 屏显示 "Hello, IDF!" |
| 5 | README 接线图 + 故障排查文档 + `docs/CHANGELOG.md` 收尾整理 | `docs: 完善 README 与故障排查文档` | 文档齐全可交付 |

每节点流程：**编辑 → `idf.py build` → `idf.py -p <port> flash monitor` → 肉眼/串口验证 → 更新 `docs/CHANGELOG.md` → `git add` → `git commit`**。任意一步失败立即回滚到上一节点排查，不在失败状态上叠加新功能。

## 验证方法

- 构建：`source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py set-target esp32 && idf.py build`
- 烧录与串口：`idf.py -p /dev/cu.usbserial-* flash monitor`
- 现象观察：白屏 → 几何图形 → "Hello, IDF!"
- 排查清单（按概率）：
  1. BUSY 一直高 → 检查 RST 是否真低 10ms、电源 3.3V
  2. 全黑/全白无反应 → SPI mode、MOSI/SCK 接反、CS 未选中
  3. 显示错位 → `EPD_W/EPD_H` 与实际屏不符（200 ↔ 152）、`0x11` 参数错
  4. 花屏 → 字节内位序反、`0x01` Driver Output Control Y 值错
  5. 字符变形 → font8x8 LSB 与画点 MSB 位序混淆

## 风险与注意事项

- **GPIO33**：RTC GPIO，可输入可输出，作 RST 没问题。input-only 仅 GPIO34-39。
- **GPIO14**：不是 strapping（strapping 为 0/2/5/12/15），可作 BUSY 输入；v6.0 起 GPIO 驱动不再隐式上拉，需 `gpio_set_pull_mode(14, GPIO_PULLUP_ONLY)`。
- **GPIO5**：是 strapping pin，上电默认上拉，作 CS 由 SPI 驱动接管即可。
- **3.3V 供电**：墨水屏 VCC 必须 3.3V，禁止 5V，逻辑电平也 3.3V。
- **FreeRTOS 头**：v6.0 起 `driver/spi_master.h` 不再隐式带入，必须显式 `#include "freertos/FreeRTOS.h"` `"freertos/task.h"`。
- **DMA**：起步用 `spi_device_polling_transmit`，帧缓冲在 `.bss` 即可。若后续切到 `spi_device_transmit` + DMA，帧缓冲改 `heap_caps_malloc(size, MALLOC_CAP_DMA)`，总线 init 时 `dma_chan = SPI_DMA_CH_AUTO`。
- **CS**：先用 SPI 硬件 CS，最稳；如发现首字节丢失再改软件 CS。

## 关键修改文件路径

- `/Users/rick/Documents/ESP32Projects/eink_screen/CMakeLists.txt`
- `/Users/rick/Documents/ESP32Projects/eink_screen/sdkconfig.defaults`
- `/Users/rick/Documents/ESP32Projects/eink_screen/main/CMakeLists.txt`
- `/Users/rick/Documents/ESP32Projects/eink_screen/main/main.c`
- `/Users/rick/Documents/ESP32Projects/eink_screen/components/epaper_154/CMakeLists.txt`
- `/Users/rick/Documents/ESP32Projects/eink_screen/components/epaper_154/include/epaper_154.h`
- `/Users/rick/Documents/ESP32Projects/eink_screen/components/epaper_154/epaper_154.c`
- `/Users/rick/Documents/ESP32Projects/eink_screen/components/epaper_154/ssd1681_cmd.h`
- `/Users/rick/Documents/ESP32Projects/eink_screen/components/epaper_154/font8x8_basic.h`
- `/Users/rick/Documents/ESP32Projects/eink_screen/docs/PLAN.md`
- `/Users/rick/Documents/ESP32Projects/eink_screen/docs/CHANGELOG.md`
- `/Users/rick/Documents/ESP32Projects/eink_screen/README.md`
