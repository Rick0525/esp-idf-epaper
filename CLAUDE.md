# 项目开发约定

## 工作流

- 按 `docs/PLAN.md` 编程，计划有变实时更新计划文件
- 计划、注释、提交信息一律中文
- 每节点：build 通过 → flash 验证 → 写 `docs/CHANGELOG.md` → git commit（原子化）
- 提交信息格式 `<type>(<scope>): <subject>`，例 `feat(epd): 实现白屏全刷`

## 构建与烧录命令模板

激活环境（每个新 shell 都要 source）：
```
source ~/.espressif/tools/activate_idf_v6.0.1.sh
```

自动化必须走绝对路径 + `-C 项目目录`（`idf.py` 在激活脚本中只是 zsh alias，`&&` 链中不展开）：
```
PY=/Users/rick/.espressif/tools/python/v6.0.1/venv/bin/python
IDF=/Users/rick/.espressif/v6.0.1/esp-idf/tools/idf.py
PROJ=/Users/rick/Documents/ESP32Projects/eink_screen
$PY $IDF -C $PROJ build
$PY $IDF -C $PROJ -p <port> flash
```

## 串口

- 每次 flash 前重新探测：`ls /dev/cu.* | grep usbserial`（USB 重插后端口名会变，例 `usbserial-10` → `usbserial-110`）
- 端口被占用：`lsof <port>` 查进程，多半是 Arduino IDE / VS Code 的 serial-monitor
- macOS 无 `timeout` 命令；自动化读串口用 pyserial（通过 RTS 触发硬件 reset，再读几秒）：
  ```python
  import serial, time
  s = serial.Serial(port, 115200, timeout=1)
  s.setDTR(False); s.setRTS(True); time.sleep(0.1); s.setRTS(False); time.sleep(0.1)
  # s.read(...) 收几秒数据
  ```

## ESP-IDF v6.0 关键变化

### CMakeLists.txt 组件依赖
- `esp_log` → `log`（已重命名）
- `log` / `freertos` / `esp_common` / `esp_hw_support` 是 **common requirements**，自动注入，**不要手写**
- 只列真正需要的：`PRIV_REQUIRES esp_driver_spi esp_driver_gpio` 等
- 参考 `examples/get-started/hello_world/main/CMakeLists.txt` 的最简风格

### GPIO 驱动
- 不再隐式上下拉：`gpio_config_t.pull_up_en` / `pull_down_en` 必须显式
- 驱动头不再隐式带入 FreeRTOS，源文件需手动 `#include "freertos/FreeRTOS.h"`

### 项目骨架
- 顶层 CMakeLists：`cmake_minimum_required(VERSION 3.22)` + `idf_build_set_property(MINIMAL_BUILD ON)`
- `components/<name>/` 会被自动发现，无需在顶层声明

## 墨水屏（GDEW0154T8 / IL0373 / ESP32-PICO-KIT v4.1）

> 注：本项目早期误把控制器当成 SSD1681，按错命令集写了一版完全不工作的驱动；
> 屏的真实型号是 1.54" **GDEW0154T8**，控制器 **IL0373**（与 Arduino 工程
> `GxEPD2_154_T8` 类一致，文件 `/Users/rick/Documents/Arduino/libraries/GxEPD2/src/epd/GxEPD2_154_T8.cpp`
> 顶部注释直接写明 `Controller: IL0373`）。后续做硬件移植**必须**先看现有可工作的原代码。

### 硬件接线（固定，不要改）

| 信号 | ESP32 GPIO | 说明 |
|---|---|---|
| CS   | 5  | VSPI IOMUX |
| MOSI | 23 | VSPI IOMUX |
| SCK  | 18 | VSPI IOMUX |
| DC   | 27 | 普通 GPIO |
| RST  | 33 | RTC GPIO，可输出 |
| BUSY | 14 | 输入 + 必须显式上拉 |
| VCC  | 3.3V | **禁止 5V** |
| GND  | 共地 | |

SPI host 用 `SPI3_HOST`（VSPI），CS/MOSI/SCK 正好是 IOMUX 引脚，速度最优。

### 屏分辨率
**152×152**（不是 200×200）。`EPD_W=EPD_H=152`，帧缓冲 152×152/8 = 2888 字节。

### 复位时序
按 Arduino GxEPD2 默认：RST 高 10ms → 低 10ms → 高 10ms（`GxEPD2_EPD::_reset`）。

### BUSY 处理（**关键，注意极性！**）
- IL0373：**BUSY=LOW 表示忙，BUSY=HIGH 表示空闲**（与 SSD1681 相反）
- 等待屏空闲 = 等 `gpio_get_level(BUSY) == 1`
- 命令发出后先 `vTaskDelay(1)` 再轮询，给屏一点时间真正进入 busy（参照 GxEPD2 `_waitWhileBusy`）
- `wait_busy_idle` 必须带超时（默认 5s）+ `ESP_LOGE`，禁止死循环

### IL0373 命令集（`il0373_cmd.h`）
| 命令 | 作用 |
|---|---|
| `0x00` PANEL_SETTING | 含 LUT 来源（0xbf=register / 0x1f=OTP，本屏只能用 0xbf） |
| `0x01` POWER_SETTING | 5 字节参数 |
| `0x02` POWER_OFF | 跟 wait_busy |
| `0x04` POWER_ON | 跟 wait_busy（约 60ms） |
| `0x06` BOOSTER_SOFT_START | 3 字节 0x17 |
| `0x07` DEEP_SLEEP | **必须**跟参数 0xA5 才生效，唤醒只能靠硬件 RST |
| `0x10` DTM1 | 写 previous 帧（首次刷新需写全 0xFF） |
| `0x12` DISPLAY_REFRESH | 触发刷新，无参数；约 1.6 秒 |
| `0x13` DTM2 | 写当前帧 |
| `0x20`-`0x24` | 5 张 LUT（vcomDC/ww/bw/wb/bb），必须手动下发，OTP 不可用 |
| `0x30` PLL | 0x3a = 100Hz |
| `0x50` VCOM_DATA_INTERVAL | 全刷用 0x97 |
| `0x61` RESOLUTION | 3 字节：宽 8 位 + 高 16 位 |
| `0x82` VCOM_DC | 0x08 |

### 全刷流程（照搬 GxEPD2_154_T8）
```
hw_reset → InitDisplay (0x01/0x06/0x00/0x30/0x61)
        → VCOM_DC + VCOM_DATA_INTERVAL
        → 5 张 LUT (0x20-0x24)
        → POWER_ON (0x04) + wait_busy
        → DTM2 (0x13) + 帧缓冲
        → 首次还要 DTM1 (0x10) + 全 0xFF 作为 previous 基线
        → DISPLAY_REFRESH (0x12) + wait_busy(约 1.6s)
hibernate: POWER_OFF (0x02) + wait_busy
        → DEEP_SLEEP (0x07 0xA5)
```

### SPI 命令/数据封装
- 命令：`gpio_set_level(DC, 0)` + 发 1 字节
- 数据：`gpio_set_level(DC, 1)` + 发 N 字节
- CS 交给 SPI 驱动（`spics_io_num=5`）；mode 0；4MHz（GxEPD2 默认）
- 用 `spi_device_polling_transmit`，帧缓冲在 `.bss` 即可（无需 DMA-capable 内存）
