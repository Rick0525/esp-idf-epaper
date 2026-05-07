# 项目开发约定

## 工作流

- 计划、注释、提交信息一律中文
- 每次改动：`build` 通过 → `flash` 验证 → 原子化 commit
- 提交信息格式 `<type>(<scope>): <subject>`，例 `feat(epd): 实现白屏全刷`

## 构建与烧录命令模板

激活 ESP-IDF 环境（每个新 shell 都要 source 一次）：

```sh
source ~/.espressif/tools/activate_idf_v6.0.1.sh
```

激活后用 `idf.py` 别名（`set-target esp32` 仅首次）：

```sh
idf.py set-target esp32
idf.py build
idf.py -p /dev/cu.usbserial-XXX flash monitor
```

`idf.py` 是 zsh alias，在 `&&` 链或非交互 shell 不展开。CI / 自动化场景改用激活脚本注入的环境变量直调：

```sh
$IDF_PYTHON_ENV_PATH/bin/python $IDF_PATH/tools/idf.py -C $(pwd) build
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

## 墨水屏（本项目硬件配置）

**已验证屏体**：WeiFeng `WF0154T8PCZ17230H`（无锡威峰科技），1.54" 黑白单色、0154T8 行业兼容屏、控制器 IL0373、分辨率 152×152。屏体溯源详见 [`docs/HARDWARE_NOTES.md`](docs/HARDWARE_NOTES.md) 的"屏体溯源"段。

**驱动通用知识**（命令集、5 张 LUT、全刷/局部刷流程、BUSY 极性、复位时序、SPI 封装、踩坑速查、移植到其他 MCU）见 [`docs/EPAPER_IL0373_GUIDE.md`](docs/EPAPER_IL0373_GUIDE.md)——任何 IL0373 0154T8 屏项目都能直接拿走。

本项目的 IL0373 驱动实现照搬 Arduino [GxEPD2](https://github.com/ZinggJM/GxEPD2) 库的 `GxEPD2_154_T8` 类作 ground truth（顶部注释写明 `Controller: IL0373`）。后续做任何硬件移植**先看现有可工作的原代码**，不凭型号假设。

### 默认硬件接线（要改改 `components/epaper_il0373_154t8/epaper_154.c` 顶部宏）

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

SPI host 用 `SPI3_HOST`（VSPI），CS/MOSI/SCK 正好是 IOMUX 引脚，速度最优。SPI mode 0、4MHz、硬件 CS、polling 模式。
