# 变更日志

按节点记录每次原子化提交的功能与验证结果。

## 节点 0 — 项目骨架

- 创建目录结构：`main/`、`components/epaper_154/`（含 `include/`）、`docs/`
- `CMakeLists.txt`（顶层）、`main/CMakeLists.txt`、`components/epaper_154/CMakeLists.txt`
- `sdkconfig.defaults`：`CONFIG_IDF_TARGET="esp32"`、`CONFIG_ESPTOOLPY_FLASHMODE_DIO=y`、`CONFIG_LOG_DEFAULT_LEVEL_INFO=y`
- `.gitignore`：`build/`、`sdkconfig`、`sdkconfig.old`、`managed_components/`、`dependencies.lock`、`.DS_Store` 等
- `main/main.c`：仅 `ESP_LOGI(TAG, "boot ok")`
- `components/epaper_154/`：占位 `epaper_154.h` / `epaper_154.c`，后续节点填充
- `docs/PLAN.md` 落盘
- 验证：`idf.py set-target esp32` → `idf.py build` 通过 → `idf.py -p /dev/cu.usbserial-10 flash monitor` 看到 "boot ok"

## 节点 1 — SPI/GPIO 初始化与 SW Reset 验证

- `ssd1681_cmd.h`：定义本项目用到的 SSD1681 命令字节宏（**节点 2 重做时已删除**——见下文型号纠正）
- `epaper_154.h`：声明 `epaper_init()`
- `epaper_154.c`：
  - GPIO 配置：DC=27 / RST=33 输出，BUSY=14 输入 + 上拉
  - SPI3_HOST（VSPI，IOMUX 路径）：CS=5 / MOSI=23 / SCK=18 / 4MHz / mode 0 / 硬件 CS
  - 硬件复位：RST 高 20ms → 低 20ms → 高 20ms
  - 发送 0x12 SW Reset，前后各 200ms 高频采样 BUSY 最低电平
  - `wait_busy_low` 带 5s 超时，超时 `ESP_LOGE` 返回错误
- `main/main.c`：调用 `epaper_init()` 并打印结果
- 验证日志：
  - "硬件 RST 完成，BUSY 当前电平=1"
  - "SW Reset 后 200ms 内 BUSY 最低电平=0"
  - "SPI/DC/RST 时序验证通过"
- **节点 2 复盘补充**：当时把 BUSY 短暂翻转解读为 SW Reset 命令生效，实际屏是 IL0373 而非 SSD1681，0x12 在 IL0373 是 display refresh 而非 SW Reset；BUSY 那次 1→0 翻转其实是 hw RST 之后屏自然从初始 busy 回落，与命令是否被屏接收无关。这个误判一直延续到节点 2 第一版调试，导致排查方向走偏。

## 节点 2 — 移植 GxEPD2 IL0373 驱动并实现白屏全刷

**型号纠正**：早期 CLAUDE.md/PLAN.md 把控制器记成 SSD1681；本节点初版按 SSD1681 命令集写完后屏完全没正确刷新（首次"全黑"，第二次刷新出现"灰底+残留字符"）。Arduino 工程使用的 `GxEPD2_154_T8` 类对应的真实硬件是 1.54" **GDEW0154T8**，控制器 **IL0373**——`/Users/rick/Documents/Arduino/libraries/GxEPD2/src/epd/GxEPD2_154_T8.cpp` 第 5-6 行注释直接写明 `Panel: GDEW0154T8` / `Controller: IL0373`。本节点全面修正型号、命令集、BUSY 极性、分辨率，文档也同步更新。

代码改动：
- 删除 `components/epaper_154/ssd1681_cmd.h`
- 新增 `components/epaper_154/il0373_cmd.h`：IL0373 命令字节宏（`POWER_SETTING/BOOSTER_SOFT_START/PANEL_SETTING/PLL/RESOLUTION/DTM1/DTM2/DISPLAY_REFRESH/POWER_ON/POWER_OFF/DEEP_SLEEP/VCOM_DC/VCOM_DATA_INTERVAL/LUT_VCOM/LUT_WW/LUT_BW/LUT_WB/LUT_BB`）
- 改写 `epaper_154.c`：
  - 屏分辨率：`EPD_W = EPD_H = 152`（GDEW0154T8 真实尺寸），帧缓冲 2888 字节
  - 5 张全刷 LUT（`vcomDC/ww/bw/wb/bb`）直接复制自 `GxEPD2_154_T8.cpp`
  - `wait_busy_idle`：等 BUSY=HIGH 才返回（IL0373 LOW=忙），仿 GxEPD2 `_waitWhileBusy` 在轮询前先 `vTaskDelay(1)`
  - `il0373_init_display`：`POWER_SETTING(5字节)` → `BOOSTER_SOFT_START(0x17×3)` → `PANEL_SETTING(0xbf,0x0d)` → `PLL(0x3a)` → `RESOLUTION(W,H>>8,H&0xFF)`
  - `il0373_init_full`：在 `init_display` 之后追加 `VCOM_DC(0x08)` → `VCOM_DATA_INTERVAL(0x97)` → 5 张 LUT → `POWER_ON(0x04)` + wait
  - `epaper_display_full`：`init_full` → `DTM2(0x13) + 帧缓冲` → 首次刷新还要 `DTM1(0x10) + 全0xFF` 作为 previous 基线 → `DISPLAY_REFRESH(0x12)` + wait（约 1.6s）
  - `epaper_sleep`：`POWER_OFF(0x02)` + wait → `DEEP_SLEEP(0x07, 0xA5)`
- `epaper_154.h`：`EPD_W/H` 改 152；保持 `epaper_init/clear/display_full/sleep` 四个 API 不变
- `main/main.c`：流程仍是 `init → clear(0xFF) → display_full → sleep`

验证：屏物理上完整变全白；串口日志：
- `硬件 RST 完成，BUSY 当前电平=1 (1=空闲, 0=忙)` —— 极性符合 IL0373
- `[PowerOn] 空闲（耗时约 40 ms）` —— 与 GxEPD2 头文件 `power_on_time = 60ms` 数量级一致
- `[DisplayRefresh] 空闲（耗时约 1550 ms）` —— 与 GxEPD2 `full_refresh_time = 1600ms` 几乎完全吻合
- `[PowerOff] 空闲（耗时约 30 ms）` / `已进入 Deep Sleep`

文档同步修正：
- `CLAUDE.md`：屏标题改为 `GDEW0154T8 / IL0373`；新增"屏分辨率"段；BUSY 极性翻转；命令集表替换为 IL0373；新增全刷流程示意；保留接线表与 SPI 封装
- `docs/PLAN.md`：Context 段加型号纠正记录；驱动设计要点整段重写为 IL0373；节点 2 描述重写；排查清单首条改为"屏完全无反应或灰底→先核对 GxEPD2 类对应的真实控制器"；关键文件路径中 `ssd1681_cmd.h → il0373_cmd.h`；分辨率全改 152

**关键经验（已写入 auto memory）**：硬件设备从一个框架移植到另一个框架时，**第一步永远是定位并阅读已经在工作的原代码**——它对当前这块物理硬件是 ground truth。不要直接信任 CLAUDE.md/对话里的型号断言；不要凭"这类设备一般是 X 控制器"的常识脑补。当 build/flash 通过但硬件行为反复异常时，必须回到上游假设质疑（型号、协议），而不是只在当前假设下做局部微调（CS 模式、时钟频率、sequence option）。
