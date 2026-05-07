# esp-idf-epaper

E-paper driver library for ESP32 / ESP-IDF.
基于 ESP-IDF 的 ESP32 系列墨水屏驱动库。

目前实现：ESP32-PICO-KIT v4.1 + 1.54" **0154T8 规格**黑白墨水屏（IL0373 控制器）的 ESP-IDF v6.0.1 **驱动库 + 示例工程**。

设计目标：做一个在 ESP-IDF 下**好复用的基础驱动库**，组件目录 `components/epaper_il0373_154t8/` 可整体拷到任何 IDF 项目即用。命名约定 `epaper_<chip>_<size>` 留出未来加其它屏的空间（如 `epaper_il3897_213/`）。视觉层不绑定 Arduino——`GxEPD2` 仅作为**实现层 ground truth**（init 序列、5 张 LUT、partial 流程严格照搬），上层 API 形态、字体选型、demo 风格自主决定。

已验证屏体：WeiFeng `WF0154T8PCZ17230H`，与 Good Display `GDEW0154T8` 是同规格不同品牌的兼容屏（详见 [`docs/HARDWARE_NOTES.md`](docs/HARDWARE_NOTES.md)）。

## 库能力一览

| 域 | API | 节点 |
|---|---|---|
| **初始化 / 帧缓冲** | `epaper_init` / `epaper_clear` | 0-2 |
| **图形基元** | `draw_pixel` / `draw_hline` / `draw_vline` / `draw_rect` / `fill_rect` | 3, 6 |
| **变换** | `epaper_set_rotation` (0/1/2/3) / `epaper_width` / `epaper_height` | 7 |
| **字体（点阵）** | `draw_string_8x8` — 复古 8×8 ASCII | 4 |
| **字体（比例）** | `draw_string_gfx` / `get_text_bounds_gfx` — Adafruit_GFX 兼容格式 | 9 |
| **bitmap** | `draw_bitmap` — 1bit 透明覆盖 | 10 |
| **显示** | `display_full` (~1.6s) / `display_partial` (~360ms) / `sleep` | 2, 8 |

物理分辨率 **152×152**；坐标原点在屏左上（rotation=0 时）。

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

## 默认 demo

烧录默认 `main/main.c`（节点 10：bitmap 演示）后屏上显示同一份 16×16 上箭头 bitmap 贴 5 处（中央 + 4 角）+ 中央方框 + 顶部标题 + "x5 same data" 说明。

`main/main.c` 是示例工程而非库本体——可随意改写。把它换成自己的应用代码不会影响 `components/epaper_il0373_154t8/` 库的任何东西。

## 公开 API

```c
#include "epaper_154.h"

// ---- 初始化 ----
esp_err_t epaper_init(void);                          // SPI/GPIO 初始化 + 硬件复位
esp_err_t epaper_sleep(void);                         // POWER_OFF + DEEP_SLEEP（唤醒只能硬复位）

// ---- 帧缓冲填充（不动屏） ----
void      epaper_clear(uint8_t color);                // 0xFF=白 / 0x00=黑

// ---- 图形基元 ----
void      epaper_draw_pixel(int x, int y, bool black);
void      epaper_draw_hline(int x, int y, int len, bool black);
void      epaper_draw_vline(int x, int y, int len, bool black);
void      epaper_draw_rect(int x, int y, int w, int h, bool black);   // 仅边框
void      epaper_fill_rect(int x, int y, int w, int h, bool black);   // 实心填充

// ---- 旋转（纯软件层坐标变换，跟随所有 draw_*） ----
void      epaper_set_rotation(uint8_t rotation);      // 0=正向、1=顺 90°、2=180°、3=顺 270°
int       epaper_width(void);                         // rotation 1/3 时为 EPD_H
int       epaper_height(void);                        // rotation 1/3 时为 EPD_W

// ---- 字体 ----
void      epaper_draw_string_8x8(int x, int y, const char *s, bool black);  // 左上角坐标
void      epaper_draw_string_gfx(int x, int y, const char *s,
                                 const GFXfont *font, bool black);          // baseline 坐标
void      epaper_get_text_bounds_gfx(const char *s, const GFXfont *font,
                                     int *out_w, int *out_h);

// ---- bitmap（1bit raster，每行 (w+7)/8 字节、行内 MSB 在左） ----
void      epaper_draw_bitmap(int x, int y, const uint8_t *bmp,
                             int w, int h, bool black);  // bit=0 透明覆盖

// ---- 屏更新 ----
esp_err_t epaper_display_full(void);                  // 全刷 ~1.6s
esp_err_t epaper_display_partial(int x, int y, int w, int h);
                                                      // 局部刷 ~360ms；物理坐标
                                                      // x/w 自动 8 对齐；首次自动转 full
```

`EPD_W = EPD_H = 152`（物理分辨率宏，不随 rotation 变；用 `epaper_width()/height()` 读逻辑尺寸）。

## 使用示例

### 最小 hello world

```c
#include "epaper_154.h"
ESP_ERROR_CHECK(epaper_init());
epaper_clear(0xFF);
epaper_draw_string_8x8(10, 10, "Hello, IDF!", true);
ESP_ERROR_CHECK(epaper_display_full());
ESP_ERROR_CHECK(epaper_sleep());
```

### partial 局部刷新（counter 计数器）

```c
// 1. 先全刷画静态布局
epaper_clear(0xFF);
epaper_draw_string_8x8(8, 8, "Count:", true);
epaper_draw_rect(64, 4, 80, 16, true);
epaper_display_full();    // ~1.6s，首次必须 full（建立 partial 基线）

// 2. 之后每次只刷数字框（~360ms）
for (int i = 1; i <= 100; i++) {
    char buf[8]; snprintf(buf, sizeof(buf), "%d", i);
    epaper_fill_rect(65, 5, 78, 14, false);   // 清白底
    epaper_draw_string_8x8(70, 8, buf, true);
    epaper_display_partial(64, 4, 80, 16);    // 只刷这块矩形
}
// 频繁 partial 后建议每 ~10 次做一次 full 清屏避免残影积累
```

### GFX 比例字体 + 居中

```c
#include "FreeSansBold9pt7b.h"   // fonts/ 下任意 .h

const char *s = "centered";
int w, h;
epaper_get_text_bounds_gfx(s, &FreeSansBold9pt7b, &w, &h);
int cx = (epaper_width() - w) / 2;

// 注意：(x, y) 是 baseline，不是左上
// y_baseline = y_top + yAdvance - 5（"j/g/p/q" 等下伸字符预留）
epaper_draw_string_gfx(cx, 30, s, &FreeSansBold9pt7b, true);

// '\n' 自动换行（cursor x 回到入参 x、y += font->yAdvance）
epaper_draw_string_gfx(8, 60, "line A\nline B", &FreeSansBold9pt7b, true);
```

### bitmap 贴图

```c
// 16×16 自定义图标，每行 2 字节 × 16 行 = 32 字节，行内 MSB 在左
static const uint8_t my_icon[] = {
    0x01, 0x80,  // .......##.......
    0x03, 0xC0,  // ......####......
    /* ... */
};

epaper_draw_bitmap(10, 10, my_icon, 16, 16, true);  // 贴一份
epaper_draw_bitmap(50, 10, my_icon, 16, 16, true);  // 同数据贴第二份
// bit=1 涂 black 指定色、bit=0 不动 → 可在已有内容上叠加贴图
```

### 旋转

```c
epaper_set_rotation(1);                       // 顺 90°
// 此后所有 draw_* 用旋转后的逻辑坐标系
// epaper_width()/height() 自动交换（正方形屏数值不变）
epaper_draw_string_8x8(0, 0, "TOP-LEFT", true);   // 显示后的左上角
```

## 添加自己的字体

GFX 字体只是数据，库代码无需任何改动：

1. 从 [Adafruit-GFX-Library/Fonts/](https://github.com/adafruit/Adafruit-GFX-Library/tree/master/Fonts) 拷一个 `.h` 到 `components/epaper_il0373_154t8/fonts/`
2. 把第二行 `#include <Adafruit_GFX.h>` 改成 `#include "gfxfont.h"`
3. 在你的代码里 `#include "FreeXxxYpt7b.h"` 直接用

如要更"小屏顺眼"的字体，可以试 U8g2 用 `lv_font_conv` 或 [u8g2_to_gfx](https://github.com/olikraus/u8g2/blob/master/tools/u8g2_oled_font_to_gfx.pl) 转 GFX 格式。已知 FreeSans 系列在 9pt 下圆角处锯齿明显（1bit 输出无抗锯齿），若不能接受可换 FreeMono、U8g2 helvB、专门的 1bit 位图字体。

## 添加自己的 bitmap

用 Python 的 Pillow 把 PNG/JPG 转成 1bit C 数组：

```python
from PIL import Image
img = Image.open('icon.png').convert('1')           # 1bit 黑白
img = img.resize((24, 24))                          # 调整尺寸
data = img.tobytes()                                # bytes，按行 packed MSB-first
print('static const uint8_t icon_24x24[] = {')
print(',\n'.join('  ' + ', '.join(f'0x{b:02X}' for b in data[i:i+8])
                 for i in range(0, len(data), 8)))
print('};')
```

或用 LVGL 的[在线 Image Converter](https://lvgl.io/tools/imageconverter)，输出选 "C array" + "1 bit per pixel"。注意位序——LVGL 输出可能是 LSB-first，传入前需要按位反转或选择 MSB-first 选项。

## 编译与烧录

每个新 shell 都要先激活 ESP-IDF 环境：

```sh
source ~/.espressif/tools/activate_idf_v6.0.1.sh
```

激活后用 `idf.py` 别名（`set-target esp32` 仅首次需要）：

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/cu.usbserial-XXX flash monitor
```

USB 重插后串口端口名会变（例 `usbserial-10` → `usbserial-110`），用 `ls /dev/cu.* | grep usbserial` 重新探测。

`idf.py` 是 shell 别名，在 `&&` 链或非交互 shell 里不展开。CI / 自动化场景改用激活脚本注入的环境变量直调：

```sh
$IDF_PYTHON_ENV_PATH/bin/python $IDF_PATH/tools/idf.py -C $(pwd) build
$IDF_PYTHON_ENV_PATH/bin/python $IDF_PATH/tools/idf.py -C $(pwd) -p /dev/cu.usbserial-XXX flash
```

## 项目结构

```
eink_screen/
├── CLAUDE.md                          # 项目开发约定（中文注释、原子提交、IDF 命令模板）
├── README.md                          # 本文件
├── CMakeLists.txt                     # 顶层 project(eink_screen)
├── sdkconfig.defaults                 # CONFIG_IDF_TARGET=esp32 等
├── docs/
│   ├── HARDWARE_NOTES.md              # 硬件特性研究笔记 + 字体方案选型
│   └── EPAPER_IL0373_GUIDE.md         # IL0373 通用驱动笔记（任何 0154T8 屏可参考）
├── main/
│   ├── CMakeLists.txt                 # PRIV_REQUIRES esp_timer
│   └── main.c                         # 示例：bitmap 演示（按需自行替换）
└── components/
    └── epaper_il0373_154t8/           # 整个目录拷到任何 IDF 项目即用
        ├── CMakeLists.txt             # PRIV_REQUIRES esp_driver_spi esp_driver_gpio
        ├── include/
        │   ├── epaper_154.h           # 公开 API
        │   └── gfxfont.h              # GFXglyph/GFXfont 结构定义（PROGMEM 兼容）
        ├── fonts/                     # GFX 字体目录，可任意添加 .h
        │   └── FreeSansBold9pt7b.h    # 默认字体
        ├── epaper_154.c               # 驱动实现 + 帧缓冲 + LUT + 渲染器
        ├── il0373_cmd.h               # IL0373 命令字节宏
        └── font8x8_basic.h            # dhepper 公共域 8×8 字体
```

## 实测笔记

### OTP LUT 路径（在 WeiFeng WF0154T8 上可用，但慢）

GxEPD2 源码注释暗示 `PANEL_SETTING = 0x1f`（让屏从 OTP 读 LUT）路径"仅作测试"，并标注 OTP 烧的是别的型号 `128x296` 的 LUT；本驱动也按此默认走 `0xbf`（register）+ 手动下发 5 张 LUT。

实测发现这条注释对当前批次 WeiFeng WF0154T8 **不成立**：

| 路径 | 全刷耗时 | 显示效果 | ROM 占用 |
|---|---|---|---|
| `0xbf` + 5 张 LUT（默认） | 1550 ms | 清晰 | +5 张 LUT 数据（~210 字节）|
| `0x1f` + 不发 LUT（OTP） | **4870 ms** | 清晰 | 省 ~210 字节 |

OTP 路径全刷慢 ~3 倍但显示无明显异常（清晰可读、无残影、无灰底）。慢 3 倍的耗时特征暗示 OTP 内置的是更长阶段数的通用 LUT（可能是给三色屏设计的），但其单色相位对 152×152 黑白屏仍能将电泳粒子正确推到位。

**结论**：register 路径仍是更优默认（速度优势压倒 ROM 节省），但本仓库代码注释里说"OTP 不可用"的旧表述是不准确的——OTP 在该批次 IC 上可用。如果项目对刷新速度不敏感且想极致省 ROM，OTP 路径可作为备选。**注意此结论对其他厂家 / 其他批次 IC 不一定成立**，移植到不同硬件上仍要测。

## 故障排查

按现象出现的概率从高到低：

### 1. 屏完全无反应或灰底
**90% 是控制器型号判断错了**。先去看 Arduino GxEPD2 库里对应屏型号的 `.cpp`（如 `GxEPD2_154_T8.cpp`）顶部注释，确认真实控制器是什么。IL0373、SSD1681、UC8151D 等命令集互不通用，型号错则整套时序、LUT、刷新流程都对不上。

> 屏面板印的代号（如 `WF0154T8...`、`GDEW0154T8`）是规格代号不是控制器型号。`0154T8` = 1.54" + IL0373，同代号的不同品牌屏协议互通。控制器只能查 GxEPD2 类对应 `.cpp` 的顶部注释或屏厂家 datasheet。

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
- **partial 后看到上次内容残影**：partial 必须 write+refresh+write 双写（IL0373 内部 current/previous 翻页机制）。库内已实现，自写驱动遇到此问题查 `epaper_display_partial` 实现

### 6. partial 第一次调用走了 1.6s
预期行为：partial LUT 是差分，需要 full 建立基线。库会在 `s_initial_refresh==true` 时自动把 `display_partial` 转调 `display_full`。日志会有 `首次刷新自动转 full（partial 需要先建立基线）`。

### 7. partial 窗口实际比传入的大
预期行为：IL0373 RAM 寻址按字节，x 和 w 强制 8 对齐——传入 `(63, 0, 70, 16)` 实际刷 `(56, 0, 80, 16)`。库自动对齐，不报错。

### 8. GFX 字体串显示位置偏低
`epaper_draw_string_gfx` 的 `(x, y)` 是 **baseline**（字母 A 的底部），不是左上角。要按"上沿"定位用 `y_baseline = y_top + font->yAdvance - 5`（5 是给下伸字符 j/g/p/q 留的空间，按字体不同微调）。

### 9. 字符变形 / 颠倒
- `font8x8` LSB 在左，画字符时 `bits & (1u << col)`（**不是** `0x80 >> col`）
- GFX 字体则是 MSB 在左，与帧缓冲一致——切勿把两种字体的扫描代码混用

### 10. 串口端口被占用
```sh
lsof /dev/cu.usbserial-XXX
```
通常是 Arduino IDE 或 VS Code 的 serial monitor 在占。

## 关键参考

- Arduino GxEPD2 库 `GxEPD2_154_T8.{h,cpp}`：[github.com/ZinggJM/GxEPD2](https://github.com/ZinggJM/GxEPD2)（实现层 ground truth，移植任何变更建议先对照）
- IL0373 datasheet：[http://www.e-paper-display.com/download_detail/downloadsId=535.html](http://www.e-paper-display.com/download_detail/downloadsId=535.html)
- GDEW0154T8 产品页：[http://www.e-paper-display.com/products_detail/productId=345.html](http://www.e-paper-display.com/products_detail/productId=345.html)
- 8×8 字体：[github.com/dhepper/font8x8](https://github.com/dhepper/font8x8)（公共域）
- GFX 字体格式 + FreeSans：[github.com/adafruit/Adafruit-GFX-Library](https://github.com/adafruit/Adafruit-GFX-Library)（BSD 3-clause）
- IL0373 通用移植笔记：[`docs/EPAPER_IL0373_GUIDE.md`](docs/EPAPER_IL0373_GUIDE.md)（任何 0154T8 屏项目可直接参考）

## 许可

- 项目代码：MIT
- `font8x8_basic.h`：公共域（来自 dhepper/font8x8）
- `gfxfont.h` + `fonts/FreeSansBold9pt7b.h`：BSD 3-clause（Copyright © 2012 Adafruit Industries）；字体本体由 [GNU FreeFont](https://www.gnu.org/software/freefont/) 项目以 GPL with font-embedding exception 提供
