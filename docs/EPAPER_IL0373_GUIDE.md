# IL0373 / 0154T8 兼容屏驱动笔记

跨项目复用的"如何驱动 IL0373 控制器墨水屏"知识库。任何用 IL0373 控制器的 1.54" 黑白电子纸（WeiFeng WF0154T8、Good Display GDEW0154T8、其他 0154T8 代号兼容屏）都可以参考本笔记从零写驱动。

下个墨水屏项目可以把这份文档直接复制过去。本笔记只描述协议和驱动逻辑，不带任何具体框架/项目的接口名。

## 1. 屏体识别

### 0154T8 是什么
- **`0154`** = 1.54 英寸
- **`T8`** = 行业通用规格代号：1.54" 黑白电子纸"B 规格"——8 pin 接线、152×152 分辨率、IL0373 控制器
- 屏面板印的型号代号（`WF0154T8...`、`GDEW0154T8` 等）**不等于控制器型号**——同代号的不同品牌屏协议互通
- 要查控制器：看 GxEPD2 类对应 `.cpp` 顶部注释，或看 IC 上的丝印（IL0373 / SSD1681 / IL3897 等）

### 与其他常见控制器的区别
| 维度 | IL0373 | SSD1681 | 注意 |
|---|---|---|---|
| BUSY 极性 | LOW=忙、HIGH=空闲 | HIGH=忙、LOW=空闲 | **完全相反**——把 IL0373 当 SSD1681 写驱动会"屏完全无反应或灰底" |
| 写 RAM 命令 | 0x10（previous）/ 0x13（current） | 0x24（B/W）/ 0x26（red） | |
| 触发刷新 | 0x12（无参数） | 0x22 + 0x20 | |
| LUT | 必须手动下发 5 张（vcomDC/ww/bw/wb/bb） | 一张大表，可用 OTP | |
| 深睡 | 0x07 0xA5 | 0x10 0x01 | |

**坑**：CLAUDE.md/对话/手册里型号断言不要直接信。**移植任何硬件第一步是定位现有可工作代码作 ground truth**——对 IL0373 0154T8 系列，这就是 [Arduino GxEPD2](https://github.com/ZinggJM/GxEPD2) 库的 `GxEPD2_154_T8.cpp`，文件顶部注释会写明 `Controller: IL0373`。

## 2. 硬件接口

### 8 pin 接线（典型 0154T8 模组）

| Pin | 信号 | 方向 | 说明 |
|---|---|---|---|
| 1 | VCC  | 入 | **必须 3.3V**，逻辑电平也 3.3V，禁止 5V |
| 2 | GND  | — | 共地 |
| 3 | CS   | 入 | 片选，低有效 |
| 4 | DC   | 入 | 命令(0)/数据(1)切换 |
| 5 | RST  | 入 | 硬件复位，低有效 |
| 6 | BUSY | 出 | 状态指示，**LOW=忙**、HIGH=空闲 |
| 7 | DIN  | 入 | SPI MOSI |
| 8 | CLK  | 入 | SPI SCK |

无 MISO（屏不需要回数据）。

### MCU 端要求
- **SPI 主控**：mode 0（CPOL=0, CPHA=0）、MSB first、4 MHz（GxEPD2 默认）
- **DC 引脚**：可独立 GPIO 控制（每条命令前后都要切换电平）
- **BUSY 引脚**：输入 + **必须显式软件上拉**（v6.0 起 ESP-IDF 不再隐式上拉）
- **RST 引脚**：可输出（注意 ESP32 GPIO34-39 是 input-only 不能用）
- **CS**：交给 SPI 控制器硬件管理最稳；如发现首字节丢失再改软件 CS

## 3. 关键参数

| 参数 | 值 | 备注 |
|---|---|---|
| 分辨率 | 152×152 | **不是 200×200**（写错的话显示错位） |
| 帧缓冲大小 | 152×152/8 = **2888 字节** | 单色 1bit |
| 帧缓冲位序 | MSB 对应较小 X | 1=白、0=黑 |
| BUSY 极性 | LOW=忙、HIGH=空闲 | **关键**，与 SSD1681 相反 |
| 全刷耗时 | ~1.6 秒 | GxEPD2 头文件 `full_refresh_time = 1600ms` |
| 局部刷耗时 | ~350 ms | GxEPD2 头文件 `partial_refresh_time = 350ms` |
| PowerOn 耗时 | ~60 ms | GxEPD2 头文件 `power_on_time = 60ms` |
| PowerOff 耗时 | ~20 ms | GxEPD2 头文件 `power_off_time = 20ms` |
| 物理可视区 | 152×152 | 屏物理像素阵列略大，但 RAM 寻址有效区只有 152×152，外圈一圈白边是物理特性 |

## 4. 复位时序

按 GxEPD2 默认（`GxEPD2_EPD::_reset`）：

```
RST 拉高 10ms → 拉低 10ms → 拉高 10ms
```

之后屏的 BUSY 会自然从 LOW（忙）回到 HIGH（空闲），不需等待固定时间——但下条命令前可读 BUSY 确认空闲。

## 5. SPI 命令/数据封装

```
发命令：DC = 0，发 1 字节
发数据：DC = 1，发 N 字节
```

CS 由 SPI 硬件 mananger 管。命令字节和后续数据字节之间允许时间间隔，但**同一笔事务的 DC 电平必须保持稳定**——即不要在一个 SPI 事务里中途切换 DC。

帧缓冲（2888 字节）用 polling 模式传输即可，**不需要 DMA-capable 内存**——放 `.bss` 静态数组就行。如果切到 DMA 模式，帧缓冲要走 `MALLOC_CAP_DMA` 分配。

## 6. IL0373 命令集

| 命令 | 名称 | 参数 | 作用 |
|---|---|---|---|
| `0x00` | PANEL_SETTING | 2 字节 | 第 1 字节 LUT 来源（**0xbf=register / 0x1f=OTP**——本类屏 OTP 不可用，必须 0xbf）；第 2 字节 0x0d = VCOM to 0V fast |
| `0x01` | POWER_SETTING | 5 字节 | `0x03, 0x00, 0x2b, 0x2b, 0x03`（GxEPD2 默认） |
| `0x02` | POWER_OFF | — | 跟 wait_busy（~20ms） |
| `0x04` | POWER_ON | — | 跟 wait_busy（~60ms） |
| `0x06` | BOOSTER_SOFT_START | 3 字节 | `0x17, 0x17, 0x17` |
| `0x07` | DEEP_SLEEP | 1 字节 | **必须**跟参数 `0xA5` 才生效；唤醒只能靠硬件 RST |
| `0x10` | DTM1 | 2888 字节 | 写 previous 帧（首次刷新需写全 0xFF） |
| `0x12` | DISPLAY_REFRESH | — | 触发刷新，无参数；约 1.6 秒 |
| `0x13` | DTM2 | 2888 字节 | 写当前帧 |
| `0x20` | LUT_VCOM | 44 字节 | LUT for VCOM |
| `0x21` | LUT_WW | 42 字节 | LUT for white→white |
| `0x22` | LUT_BW | 42 字节 | LUT for black→white |
| `0x23` | LUT_WB | 42 字节 | LUT for white→black |
| `0x24` | LUT_BB | 42 字节 | LUT for black→black |
| `0x30` | PLL_CONTROL | 1 字节 | `0x3a` = 100Hz（GxEPD2 默认）；其他选项：0x29=150Hz、0x39=200Hz、0x31=171Hz |
| `0x50` | VCOM_DATA_INTERVAL | 1 字节 | 全刷模式 `0x97`（白边框）；局部刷模式 `0x17` |
| `0x61` | RESOLUTION_SETTING | 3 字节 | `W, H>>8, H&0xFF`——W 用 1 字节，H 用 2 字节大端 |
| `0x82` | VCOM_DC | 1 字节 | `0x08` |
| `0x90` | PARTIAL_WINDOW | 7 字节 | 见局部刷一节 |
| `0x91` | PARTIAL_IN | — | 进入局部刷模式 |
| `0x92` | PARTIAL_OUT | — | 退出局部刷模式 |

## 7. 全刷流程

```
1. hw_reset                                       (RST 10/10/10ms 脉冲)
2. POWER_SETTING        (0x01) + 5 字节
3. BOOSTER_SOFT_START   (0x06) + 3 字节 0x17
4. PANEL_SETTING        (0x00) + 2 字节 (0xbf, 0x0d)
5. PLL_CONTROL          (0x30) + 0x3a
6. RESOLUTION_SETTING   (0x61) + (W, H>>8, H&0xFF)
7. VCOM_DC              (0x82) + 0x08
8. VCOM_DATA_INTERVAL   (0x50) + 0x97
9. LUT_VCOM             (0x20) + lut_vcomDC[44]
10. LUT_WW              (0x21) + lut_ww[42]
11. LUT_BW              (0x22) + lut_bw[42]
12. LUT_WB              (0x23) + lut_wb[42]
13. LUT_BB              (0x24) + lut_bb[42]
14. POWER_ON            (0x04) → wait_busy (~60ms)
15. DTM2                (0x13) + 2888 字节当前帧
16. 首次刷新追加：DTM1   (0x10) + 2888 字节全 0xFF（previous 基线）
17. DISPLAY_REFRESH     (0x12) → wait_busy (~1.6s)
```

进入 hibernate（关电 + 深睡）：
```
18. POWER_OFF           (0x02) → wait_busy (~20ms)
19. DEEP_SLEEP          (0x07) + 0xA5
```

下次使用必须重新 hw_reset 才能唤醒——deep sleep 状态下命令完全不响应。

### 关键点

- **PANEL_SETTING 第 1 字节必须 0xbf**——0154T8 屏的 OTP LUT 不可用，写 0x1f 会卡死
- **5 张 LUT 必须手动下发**——直接复制自 GxEPD2_154_T8.cpp 的 `lut_20_vcomDC` ~ `lut_24_bb`
- **首次刷新必须写 DTM1 + 全 0xFF**——给差分 LUT 一个"上次=白"的基线，否则首次结果不可控；后续刷新由控制器自己内部翻页，不再需要
- **wait_busy 必须带超时**（推荐 5s）+ 报错日志，**禁止死循环**
- **命令发出后先 delay(1) 再轮询 BUSY**——给屏一点时间真正进入 busy 状态（参照 GxEPD2 `_waitWhileBusy`）

## 8. 局部刷新流程

局部刷新（partial refresh）用第二组 LUT（速度快不闪屏，但残影会积累），耗时 ~350ms vs 全刷 1.6s。适合时钟、传感器读数、小区域动画等。

```
1. 完整 init（步骤 1-6 同全刷）
2. VCOM_DC              (0x82) + 0x08
3. VCOM_DATA_INTERVAL   (0x50) + 0x17    ← 注意是 0x17 不是 0x97
4. 5 张 LUT             (0x20-0x24) + lut_*_partial（来自 GxEPD2_154_T8.cpp 的 lut_*_partial 数组）
5. POWER_ON             (0x04) → wait_busy
6. PARTIAL_IN           (0x91)
7. PARTIAL_WINDOW       (0x90) + 7 字节（见下）
8. DTM2                 (0x13) + 矩形内字节数据
9. DISPLAY_REFRESH      (0x12) → wait_busy (~350ms)
10. PARTIAL_OUT         (0x92)
```

`PARTIAL_WINDOW` 7 字节参数（设定矩形 (x, y, w, h)，注意必须按字节对齐 X 方向）：

```c
xe = (x + w - 1) | 0x07;   // 对齐到字节末位（含最后字节）
ye = y + h - 1;
x &= 0xF8;                  // 对齐到字节起始

参数：
  x % 256
  xe % 256
  y / 256
  y % 256
  ye / 256
  ye % 256
  0x01
```

工程实践：**每 N 次 partial refresh 后做一次全刷清残影**（N 经验值 30-100）。

## 9. 5 张全刷 LUT 数据

直接复制自 [GxEPD2_154_T8.cpp](https://github.com/ZinggJM/GxEPD2/blob/master/src/epd/GxEPD2_154_T8.cpp) 的 `lut_20_vcomDC` / `lut_21_ww` / `lut_22_bw` / `lut_23_wb` / `lut_24_bb`：

```c
static const uint8_t lut_vcomDC[44] = {
    0x00, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x60, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x00, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x00, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00,
};
static const uint8_t lut_ww[42] = {
    0x40, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x40, 0x14, 0x00, 0x00, 0x00, 0x01,
    0xA0, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
// lut_bw 与 lut_ww 完全相同
// lut_wb 与 lut_bb 完全相同（首字节 0x80 替换 0x40，第 13 字节 0x80 替换 0x40，第 19 字节 0x50 替换 0xA0）
static const uint8_t lut_wb[42] = {
    0x80, 0x08, 0x00, 0x00, 0x00, 0x02,
    0x90, 0x28, 0x28, 0x00, 0x00, 0x01,
    0x80, 0x14, 0x00, 0x00, 0x00, 0x01,
    0x50, 0x12, 0x12, 0x00, 0x00, 0x01,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
};
```

partial LUT 在 GxEPD2_154_T8.cpp 同文件下方 `lut_*_partial` 数组，按需复制。

## 10. 常见踩坑速查

| 现象 | 90% 原因 |
|---|---|
| 屏完全无反应或灰底 | 控制器型号判断错（最可能把 IL0373 当 SSD1681） |
| BUSY 一直为 0（屏一直忙） | POWER_ON 失败；或 PANEL_SETTING 第 1 字节写成 `0x1f`（OTP）而非 `0xbf`（register）；或 5 张 LUT 没下发 |
| 全黑/全白无反应 | SPI mode 错（应 mode 0）；MOSI/SCK 接反；CS 由谁管控不一致 |
| 显示错位 | RESOLUTION (0x61) 参数错（W/H 字节序、字节数错）；驱动用 200×200 但屏是 152×152 |
| 花屏 / 残影 | 字节内位序反；DTM1/DTM2 顺序错（首次必须 DTM1 + 全 0xFF） |
| 字符变形 | 字体位序与帧缓冲位序不一致（如 8×8 字体 LSB 在左 vs 帧缓冲 MSB 对应较小 X） |
| 屏物理边内一圈白边 | **不是 bug**——0154T8 物理像素阵列略大于 152×152，外圈像素不在 RAM 寻址范围，是固有特性 |

## 11. 移植到不同 MCU/框架

驱动逻辑完全平台无关。需要平台提供的最小能力：

- 1 路 SPI 主控，能配置 mode 0、4MHz、polling 传输
- 3 个独立 GPIO 输出（CS、DC、RST）
- 1 个 GPIO 输入 + 软件上拉（BUSY）
- 一个 ms 级别的延时函数（用于复位时序、wait_busy 轮询间隔、命令前 delay）

**不需要**：
- DMA（帧缓冲只 2888 字节，polling 足够）
- 中断（BUSY 用轮询即可，反应在 ms 级）
- RTOS（裸机也能驱动，wait_busy 改为忙等带超时即可）

跨平台要改的代码量：只有 GPIO 操作 + SPI 写字节 + delay 这三类系统调用的封装；纯协议部分（命令字节、LUT 数据、流程顺序）原样可用。

## 12. 参考资料

- **Arduino GxEPD2 库**（移植 IL0373 屏的"圣经"，ground truth）：
  - 仓库：https://github.com/ZinggJM/GxEPD2
  - 关键文件：`src/epd/GxEPD2_154_T8.cpp` 与 `.h`
  - 头文件常量（power_on_time、full_refresh_time 等）就是预期时序
- **IL0373 datasheet**：http://www.e-paper-display.com/download_detail/downloadsId=535.html
- **GoodDisplay 0154T8 产品页**：http://www.e-paper-display.com/products_detail/productId=345.html
- **GxEPD2 类与控制器对照**：每个 `GxEPD2_<尺寸>_<代号>.cpp` 顶部注释写明真实控制器

## 附：从零写驱动的最小步骤（半小时）

1. 配置 GPIO（DC、RST 输出；BUSY 输入 + 上拉）
2. 配置 SPI（mode 0、4MHz、硬件 CS、polling）
3. 实现 `send_cmd(cmd)` / `send_data(buf, len)` 两个原语
4. 实现 `wait_busy(timeout_ms)`（轮询 BUSY，HIGH 退出）
5. 实现 `hw_reset()`（10/10/10ms 脉冲）
6. 把第 7 节"全刷流程"的 19 步顺序串起来调用
7. 准备 152×152/8 = 2888 字节帧缓冲，先 memset 为 0xFF（白）跑通"白屏全刷"
8. 之后再加 draw_pixel 等绘图函数

照这个顺序，从空项目到屏刷出第一帧白屏 **约半小时**——前提是已经有 GxEPD2_154_T8.cpp 作 ground truth 对照。
