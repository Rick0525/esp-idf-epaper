# 墨水屏 Arduino → ESP-IDF v6.0.1 移植计划

## Context（背景）

用户原有一个基于 Arduino + GxEPD2 库的 ESP32 墨水屏测试项目，位于 `/Users/rick/Documents/esp32教程/ink_test/ink_test.ino`。该项目使用 `GxEPD2_154_T8` 显示 helloWorld、字体、位图与部分刷新等多个 demo。

为了脱离 Arduino 框架、获得更好的可裁剪性与对硬件的直接控制，需要把它移植到 ESP-IDF v6.0.1。目标工作目录是空目录 `/Users/rick/Documents/ESP32Projects/eink_screen`（已建好 `CLAUDE.md`：要求中文注释、git 原子化提交、固定的 IDF 激活与构建命令）。

本次移植以**最小可用**为目标：跑通 SPI/GPIO 初始化、IL0373 驱动、单色帧缓冲、基础绘图与一次 helloWorld 全屏刷新。从零手写 C 驱动，不引入第三方组件，但 init 序列、5 张 LUT、刷新流程**严格照搬** `GxEPD2_154_T8.cpp`。

> **型号纠正记录**：项目早期 PLAN.md/CLAUDE.md 误把控制器记成 SSD1681，按错命令集写了一版完全不工作的驱动。Arduino 工程使用的 `GxEPD2_154_T8` 类对应的真实硬件是 1.54" **GDEW0154T8**，控制器 **IL0373**（与 SSD1681 命令集完全不同：BUSY 极性反、写 RAM 用 0x10/0x13、刷新用 0x12、必须手动下发 LUT）。本计划于 2026-05-05 节点 2 重做时全面修正。

## 已确认前提

- 主控：**ESP32-PICO-KIT v4.1**（ESP32-PICO-D4 模块）；`idf.py set-target esp32`
- 接线（**用户要求保持不变**）：CS=GPIO5，MOSI=GPIO23，SCK=GPIO18，MISO 不接，DC=GPIO27，RST=GPIO33，BUSY=GPIO14；3.3V 供电，共地
- 屏幕：1.54" 黑白墨水屏 **GDEW0154T8**，控制器 **IL0373**；分辨率 **152×152**（已实测确认）
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
        ├── CMakeLists.txt          # REQUIRES "esp_driver_spi esp_driver_gpio log freertos"
        ├── include/
        │   └── epaper_154.h        # 公开 API
        ├── epaper_154.c            # 驱动实现 + 帧缓冲 + 5 张 LUT
        ├── il0373_cmd.h            # IL0373 命令字节宏
        └── font8x8_basic.h         # 嵌入字体表（公共域，节点 4 加入）
```

## IL0373 驱动设计要点

**波形 LUT**：IL0373 在本屏上**不能用 OTP**（PANEL_SETTING 第一字节必须 `0xbf` = LUT from register），所以必须手动下发 5 张全刷 LUT（vcomDC/ww/bw/wb/bb），数据直接复制自 `GxEPD2_154_T8.cpp`。

**BUSY 等待**：IL0373 **BUSY=LOW=忙、HIGH=空闲**（与 SSD1681 相反）。所有 `wait_busy_idle` 调用必须带超时（默认 5s），超时返回 `ESP_ERR_TIMEOUT` 并 `ESP_LOGE`，**禁止死循环**。命令发出后先 `vTaskDelay(1)` 再轮询，参照 GxEPD2 `_waitWhileBusy` 的实现。

**命令集**（封装为宏，定义在 `il0373_cmd.h`）：
- `0x00` PANEL_SETTING（2 字节：0xbf, 0x0d）
- `0x01` POWER_SETTING（5 字节）
- `0x02` POWER_OFF
- `0x04` POWER_ON
- `0x06` BOOSTER_SOFT_START（3 字节 0x17）
- `0x07` DEEP_SLEEP（参数 0xA5）
- `0x10` DTM1（写 previous 帧）
- `0x12` DISPLAY_REFRESH（无参数）
- `0x13` DTM2（写当前帧）
- `0x20`-`0x24` 五张 LUT
- `0x30` PLL（0x3a = 100Hz）
- `0x50` VCOM_DATA_INTERVAL（全刷 0x97）
- `0x61` RESOLUTION（3 字节：W, H>>8, H&0xFF）
- `0x82` VCOM_DC（0x08）

**全刷流程**：hw_reset → InitDisplay → VCOM_DC + VCOM_DATA_INTERVAL → 5 张 LUT → POWER_ON + wait → DTM2 + 帧缓冲 → 首次还要 DTM1 + 全 0xFF → DISPLAY_REFRESH + wait（约 1.6s）。

**hibernate**：POWER_OFF + wait → DEEP_SLEEP (0x07 0xA5)。再次唤醒只能靠硬件 RST。

**SPI 命令/数据封装**：手动 GPIO 控制 DC（命令前 `gpio_set_level(DC,0)`、数据前置 1）。CS 交给 SPI 驱动硬件管理（`spics_io_num=5`）。SPI mode 0、MSB first、4 MHz（GxEPD2 默认）。

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

- 宏：`EPD_W 152`、`EPD_H 152`（实测确认）
- 缓冲：`static uint8_t framebuf[(EPD_W/8)*EPD_H]` = 2888 字节，放 `.bss`
- 位序：每字节 MSB 对应较小 X；1=白 / 0=黑
- 画点：`byte_idx = (x>>3) + y*(EPD_W>>3); bit = 0x80 >> (x&7); black? buf[idx]&=~bit : buf[idx]|=bit;`

## 8x8 字体

直接嵌入 dhepper/font8x8 的 `font8x8_basic.h`（公共域，ASCII 0~127，约 1KB），格式 `static const uint8_t font8x8_basic[128][8]`。**注意**：该字体每字节 LSB 在左，画字符时按位扫描需 `bit = 0x01 << col`，与画点 MSB 顺序不同，不要混淆。文件放 `components/epaper_154/font8x8_basic.h`。

## app_main 流程

```c
ESP_LOGI(TAG, "boot");
ESP_ERROR_CHECK(epaper_init());
epaper_clear(0xFF);
epaper_draw_string_8x8(10, 10, "Hello, IDF!", true);
epaper_draw_hline(0, 30, 152, true);
ESP_ERROR_CHECK(epaper_display_full());
ESP_ERROR_CHECK(epaper_sleep());
```

## 原子化 git 提交节点

| 节点 | 内容 | 提交信息 | 验证 |
|---|---|---|---|
| 0 | `git init`、`.gitignore`（含 `build/`、`sdkconfig`、`sdkconfig.old`、`managed_components/`、`dependencies.lock`、`.DS_Store`）、`sdkconfig.defaults`（`CONFIG_IDF_TARGET="esp32"`、`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y`、`CONFIG_LOG_DEFAULT_LEVEL_INFO=y`）、骨架文件、空 `app_main` 打印 "boot ok"、`docs/PLAN.md` 落盘 | `chore: 初始化 ESP-IDF 项目骨架` | `idf.py build` 通过 → `flash monitor` 看到 "boot ok" |
| 1 | SPI 总线 + GPIO 配置 + RST 脉冲；按当时（错误的）SSD1681 假设发送 `0x12` 并采样 BUSY 翻转 | `feat(epd): 完成 SPI/GPIO 初始化与 SW Reset 时序验证` | flash 后 BUSY 有翻转 → 判定 SPI 通路通（**事后回看**：那次 BUSY 翻转其实是 hw RST 自然回落，不是命令触发；详见节点 2 复盘） |
| 2 | **重做**（型号纠正）：按 GxEPD2_154_T8 移植 IL0373 驱动到 IDF —— 完整 init 序列 + 5 张 LUT 下发 + DTM2/DTM1 帧缓冲 + DISPLAY_REFRESH (0x12) + POWER_OFF + DEEP_SLEEP (0x07 0xA5)；屏分辨率定为 152×152；BUSY 极性按 IL0373 = LOW 表示忙 | `feat(epd): 移植 GxEPD2 IL0373 驱动并实现白屏全刷` | 屏物理变全白；串口 `[DisplayRefresh] 空闲（耗时约 1550 ms）`、`已进入 Deep Sleep` |
| 3 | 帧缓冲 + draw_pixel/hline + 诊断图样（四角块 + 十字线） | `feat(epd): 添加帧缓冲与分辨率诊断图样` | 屏显示四角块 + 十字线，几何位置正确（确认 152×152 排布） |
| 4 | 嵌入 font8x8 + draw_string + helloWorld | `feat(epd): 添加 8x8 字体与 helloWorld 演示` | 屏显示 "Hello, IDF!" |
| 5 | README 接线图 + 故障排查文档 + `docs/CHANGELOG.md` 收尾整理 | `docs: 完善 README 与故障排查文档` | 文档齐全可交付 |

每节点流程：**编辑 → `idf.py build` → `idf.py -p <port> flash monitor` → 肉眼/串口验证 → 更新 `docs/CHANGELOG.md` → `git add` → `git commit`**。任意一步失败立即回滚到上一节点排查，不在失败状态上叠加新功能。

## 验证方法

- 构建：`source ~/.espressif/tools/activate_idf_v6.0.1.sh && idf.py set-target esp32 && idf.py build`
- 烧录与串口：`idf.py -p /dev/cu.usbserial-* flash monitor`
- 现象观察：白屏 → 几何图形 → "Hello, IDF!"
- 排查清单（按概率）：
  1. **屏完全无反应或灰底** → 90% 是控制器型号判断错了。**先查 Arduino 工程里 GxEPD2 用的是哪个类**（如 `GxEPD2_154_T8`），打开对应的 `.cpp` 看顶部注释里写的真实控制器型号
  2. BUSY 一直为 0（IL0373 视角=屏一直忙）→ POWER_ON 失败或 PANEL_SETTING 写错
  3. 全黑/全白无反应 → SPI mode、MOSI/SCK 接反
  4. 显示错位 → `EPD_W/EPD_H` 与实际屏不符、RESOLUTION (0x61) 参数错
  5. 花屏 → 字节内位序反；DTM1/DTM2 顺序错（首次刷新 DTM1 必须填 0xFF）
  6. 字符变形 → font8x8 LSB 与画点 MSB 位序混淆

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
- `/Users/rick/Documents/ESP32Projects/eink_screen/components/epaper_154/il0373_cmd.h`
- `/Users/rick/Documents/ESP32Projects/eink_screen/components/epaper_154/font8x8_basic.h`
- `/Users/rick/Documents/ESP32Projects/eink_screen/docs/PLAN.md`
- `/Users/rick/Documents/ESP32Projects/eink_screen/docs/CHANGELOG.md`
- `/Users/rick/Documents/ESP32Projects/eink_screen/README.md`
