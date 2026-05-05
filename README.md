# eink_screen

ESP32-PICO-KIT v4.1 + 1.54" GDEW0154T8 黑白墨水屏（IL0373 控制器）的 ESP-IDF v6.0.1 驱动与示例。

从零手写的 C 驱动，不依赖第三方组件；初始化序列、5 张全刷 LUT、刷新流程**严格照搬** Arduino [GxEPD2](https://github.com/ZinggJM/GxEPD2) 库的 `GxEPD2_154_T8` 类。

## 硬件接线

| 信号 | ESP32 GPIO | 说明 |
|------|-----------|------|
| CS   | GPIO5  | VSPI IOMUX，硬件 CS 由 SPI 驱动接管 |
| MOSI | GPIO23 | VSPI IOMUX |
| SCK  | GPIO18 | VSPI IOMUX |
| MISO | —      | 不接 |
| DC   | GPIO27 | 普通 GPIO，命令/数据切换 |
| RST  | GPIO33 | RTC GPIO，硬件复位输出 |
| BUSY | GPIO14 | 输入 + 软件上拉，IL0373 LOW=忙 / HIGH=空闲 |
| VCC  | 3.3V   | **禁止 5V**，逻辑电平也是 3.3V |
| GND  | GND    | 共地 |

```
     ESP32-PICO-KIT v4.1               GDEW0154T8 / IL0373
     ┌─────────────────┐                ┌──────────────┐
     │   GPIO5  (CS)   ├───────────────▶│ CS           │
     │   GPIO23 (MOSI) ├───────────────▶│ DIN          │
     │   GPIO18 (SCK)  ├───────────────▶│ CLK          │
     │   GPIO27 (DC)   ├───────────────▶│ DC           │
     │   GPIO33 (RST)  ├───────────────▶│ RST          │
     │   GPIO14 (BUSY) │◀───────────────┤ BUSY         │
     │   3V3           ├───────────────▶│ VCC (3.3V)   │
     │   GND           ├────────────────┤ GND          │
     └─────────────────┘                └──────────────┘
```

## 显示效果

烧录默认 `main/main.c` 后屏上显示：

```
┌──────────────────────┐
│ Hello, IDF!          │
│ ──────────────────── │
│ GDEW0154T8 OK        │
│                      │
│                      │
└──────────────────────┘
```

> 屏物理玻璃边内会有一圈对称白边——这是 GDEW0154T8 的物理特性（瞬时黑相能扫到、稳定显示扫不到的环带），与驱动无关。详见 [`docs/HARDWARE_NOTES.md`](docs/HARDWARE_NOTES.md)。

## 公开 API

```c
#include "epaper_154.h"

esp_err_t epaper_init(void);                    // SPI/GPIO 初始化 + 硬件复位
void      epaper_clear(uint8_t color);          // 帧缓冲填色，0xFF=白 / 0x00=黑
void      epaper_draw_pixel(int x, int y, bool black);
void      epaper_draw_hline(int x, int y, int len, bool black);
void      epaper_draw_string_8x8(int x, int y, const char *s, bool black);
esp_err_t epaper_display_full(void);            // 一次完整全刷（约 1.6s）
esp_err_t epaper_sleep(void);                   // POWER_OFF + DEEP_SLEEP（唤醒需硬复位）
```

`EPD_W = EPD_H = 152`。坐标原点在左上角。

## 编译与烧录

### 激活 ESP-IDF 环境

每个新 shell 都要 source 一次：

```sh
source ~/.espressif/tools/activate_idf_v6.0.1.sh
```

### 交互式（推荐手动操作）

激活后用 `idf.py` 别名：

```sh
idf.py set-target esp32     # 仅首次
idf.py build
idf.py -p /dev/cu.usbserial-XXX flash monitor
```

### 自动化脚本（绝对路径，避免别名展开问题）

```sh
PY=/Users/rick/.espressif/tools/python/v6.0.1/venv/bin/python
IDF=/Users/rick/.espressif/v6.0.1/esp-idf/tools/idf.py
PROJ=/Users/rick/Documents/ESP32Projects/eink_screen

$PY $IDF -C $PROJ build
$PY $IDF -C $PROJ -p /dev/cu.usbserial-110 flash
```

### 串口端口探测

USB 重插后端口名会变（例 `usbserial-10` → `usbserial-110`）：

```sh
ls /dev/cu.* | grep usbserial
```

## 项目结构

```
eink_screen/
├── CLAUDE.md                          # 项目开发约定（中文注释、原子提交、IDF 命令模板）
├── README.md                          # 本文件
├── CMakeLists.txt                     # 顶层 project(eink_screen)
├── sdkconfig.defaults                 # CONFIG_IDF_TARGET=esp32 等
├── docs/
│   ├── PLAN.md                        # 移植计划与节点表
│   ├── CHANGELOG.md                   # 每个原子提交的功能记录
│   └── HARDWARE_NOTES.md              # 硬件特性研究笔记（暂搁置项）
├── main/
│   ├── CMakeLists.txt
│   └── main.c                         # 示例：helloWorld 演示
└── components/
    └── epaper_154/
        ├── CMakeLists.txt             # PRIV_REQUIRES esp_driver_spi esp_driver_gpio
        ├── include/epaper_154.h       # 公开 API
        ├── epaper_154.c               # 驱动实现 + 帧缓冲 + 5 张全刷 LUT
        ├── il0373_cmd.h               # IL0373 命令字节宏
        └── font8x8_basic.h            # dhepper 公共域 8×8 字体
```

## 故障排查

按现象出现的概率从高到低：

### 1. 屏完全无反应或灰底
**90% 是控制器型号判断错了**。先去看 Arduino 工程里 GxEPD2 用的是哪个类（如 `GxEPD2_154_T8`），打开对应 `.cpp` 看顶部注释里写的真实控制器型号——本项目早期就是把 IL0373 错认成 SSD1681，按错命令集写了一版完全不工作的驱动。

### 2. BUSY 一直为 0（IL0373 视角=屏一直忙）
- POWER_ON (0x04) 失败
- PANEL_SETTING (0x00) 第 1 字节写错——本屏只能 `0xbf`（LUT from register），写 `0x1f`（OTP）会直接卡死
- 5 张 LUT 没下发或顺序错（必须 0x20 vcomDC、0x21 ww、0x22 bw、0x23 wb、0x24 bb）

### 3. 屏全黑/全白且无反应
- SPI mode 错（应为 mode 0）
- MOSI/SCK 接反
- CS 由谁管控制不一致——确保 `spics_io_num=5` 让 SPI 驱动硬件管 CS

### 4. 显示错位
- `EPD_W/EPD_H` 与实际屏不符（GDEW0154T8 是 **152×152**，不是 200×200）
- RESOLUTION 命令 (0x61) 参数错——必须 `{ W, H>>8, H&0xFF }`，注意 H 是大端 16 位

### 5. 花屏 / 残影
- 字节内位序反（帧缓冲约定 MSB→较小 X）
- DTM1/DTM2 顺序错——首次刷新必须 DTM1 + 全 0xFF 作为 previous 基线，否则差分 LUT 按未知基线刷新
- 没等 BUSY 拉到空闲就发下一条命令

### 6. 字符变形 / 颠倒
- font8x8 LSB 在左，与画点 MSB→较小 X 顺序相反，扫描位时必须 `bits & (1u << col)`，不要写成 `0x80 >> col`

### 7. 串口端口被占用
```sh
lsof /dev/cu.usbserial-XXX
```
通常是 Arduino IDE 或 VS Code 的 serial monitor 在占。

### 8. ESP-IDF v6.0 构建报错"esp_log not found"
v6.0 把 `esp_log` 重命名为 `log`。`PRIV_REQUIRES` 里写 `log` 而不是 `esp_log`。`log` / `freertos` / `esp_common` / `esp_hw_support` 是 common requirements，自动注入，**不要手写**。

### 9. GPIO 没有上拉
v6.0 起 GPIO 驱动不再隐式上拉。`gpio_config_t.pull_up_en` / `pull_down_en` 必须显式，否则 BUSY 引脚悬空读到随机值。

## 关键参考

- Arduino 原工程：`/Users/rick/Documents/Arduino/libraries/GxEPD2/src/epd/GxEPD2_154_T8.{h,cpp}`（ground truth，移植时反复对照）
- IL0373 datasheet：[http://www.e-paper-display.com/download_detail/downloadsId=535.html](http://www.e-paper-display.com/download_detail/downloadsId=535.html)
- GDEW0154T8 产品页：[http://www.e-paper-display.com/products_detail/productId=345.html](http://www.e-paper-display.com/products_detail/productId=345.html)
- 字体来源：[github.com/dhepper/font8x8](https://github.com/dhepper/font8x8)（公共域）

## 许可

代码部分：MIT。嵌入的 `font8x8_basic.h` 为公共域。
