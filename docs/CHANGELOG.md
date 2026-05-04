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

- `ssd1681_cmd.h`：定义本项目用到的 SSD1681 命令字节宏
- `epaper_154.h`：声明 `epaper_init()`
- `epaper_154.c`：
  - GPIO 配置：DC=27 / RST=33 输出，BUSY=14 输入 + 上拉
  - SPI3_HOST（VSPI，IOMUX 路径）：CS=5 / MOSI=23 / SCK=18 / 4MHz / mode 0 / 硬件 CS
  - 硬件复位：RST 高 20ms → 低 20ms → 高 20ms（与 Arduino GxEPD2 默认时序一致）
  - 发送 0x12 SW Reset，前后各 200ms 高频采样 BUSY 最低电平
  - `wait_busy_low` 带 5s 超时，超时 `ESP_LOGE` 返回错误
- `main/main.c`：调用 `epaper_init()` 并打印结果
- 验证日志：
  - "硬件 RST 完成，BUSY 当前电平=1" → 屏处于 hibernate 状态
  - "SW Reset 后 200ms 内 BUSY 最低电平=0" → 屏响应了 SPI 命令，BUSY 翻转
  - "SPI/DC/RST 时序验证通过"
- 关键经验：屏可能因上次 Arduino 程序末尾 hibernate 而处于深睡，硬件 RST 后 BUSY 仍为 1 是正常的，必须发送 SW Reset 才会清醒。**不能在 hw_reset 后强行要求 BUSY=0**。
