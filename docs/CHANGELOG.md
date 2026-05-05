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

## 节点 3 — 帧缓冲 API 与分辨率诊断图样

代码改动：
- `epaper_154.h`：新增 `epaper_draw_pixel(int x, int y, bool black)` 与 `epaper_draw_hline(int x, int y, int len, bool black)`，加 `<stdbool.h>` 包含
- `epaper_154.c`：实现两函数，越界静默裁剪；位序按 MSB→较小 X、1=白 0=黑（GxEPD2 风格）
- `main/main.c`：流程改为 init → clear(0xFF) → 四角 16×16 黑块 + 中心十字（水平整行 + 垂直整列）→ display_full → sleep

验证过程（一次有教益的反复）：
1. 首次 flash 后用户观察"四角块好像没贴到屏物理边"，怀疑驱动有 offset
2. 假设错误：用户最初观察"全刷瞬时黑覆盖整屏"被当成"屏可视区比 152 大"的证据
3. 回到 ground truth 重读 `GxEPD2_154_T8.cpp`：`WIDTH = WIDTH_VISIBLE = HEIGHT = 152`，`_setPartialRamArea` 无 offset，RESOLUTION 命令参数与我方完全一致——驱动层没问题
4. 临时把 `main.c` 改成黑/白循环全刷诊断版（不进 Deep Sleep，反复 `clear(0/0xFF) + display_full`）
5. 用户现场观察决定性证据：**瞬时黑相满屏，稳定全黑少四条边对称留白**
6. 结论：屏物理像素阵列大于 152×152，黑相 LUT 电压能扫到完整阵列；但 RAM 只能寻址 0x61 设定的 152×152，外圈像素不被持续维持→稳定后回白。**这一圈白边是 GDEW0154T8 物理特性，不可由驱动消除**。GxEPD2 作者钉死 152×152 也是同样原因
7. 还原诊断图样、重新烧录，肉眼确认四角块对称贴 152×152 有效区四角、十字居中——节点 3 几何验收通过

文档新增：
- `docs/HARDWARE_NOTES.md`：专门收录"知道但暂不动手"的硬件研究笔记（与 PLAN/CHANGELOG 分工）。本节点已记录 RESOLUTION (0x61) 寄存器位置、能改的上限（IL0373 datasheet 最大 160×296，GDEW0154T8 实际 ~160×160）、三条主要风险（帧缓冲扩大、LUT 时序、GxEPD2 隐含信号）、未来"切分支改宏跑全黑诊断"的实验步骤。当前决策：暂不试，先走完字体/helloWorld 主线再回头

杂项：
- `.gitignore`：加 `.cache/`（clangd LSP 缓存）

**关键经验**：观察到的"硬件异常"在没复现成稳定相之前不要急着归因。瞬时黑相是 LUT 中间相位，与稳定显示边界无关；用户最初的"满屏黑覆盖"观察其实是 LUT 电压扫描特性，不是 RAM 寻址范围的证据。下次遇到边界类问题，**先做一组对照实验区分稳定相和瞬时相**再下结论。

## 节点 4 — 嵌入 8×8 字体与 helloWorld 演示

代码改动：
- 新增 `components/epaper_154/font8x8_basic.h`：从 github.com/dhepper/font8x8 抓取的公共域 8×8 字模（128 个 ASCII 字符，每字符 8 字节，每字节 LSB 在左）。本地化处理：原文件第 23 行 `char font8x8_basic[128][8] =` 改为 `static const uint8_t font8x8_basic[128][8] =`，并加 `#pragma once` + `#include <stdint.h>`，让数据走 .rodata 不占 RAM、多 .c 包含也不重复定义
- `epaper_154.h`：新增 `epaper_draw_string_8x8(int x, int y, const char *s, bool black)`
- `epaper_154.c`：实现 draw_string_8x8。**关键点**：font8x8 字模每字节 LSB(bit0) 对应较小 X，与帧缓冲 MSB→较小 X 顺序相反，扫描位时用 `bits & (1u << col)`（不是 `0x80 >> col`）。≥128 的字符跳过但仍占 8 像素宽
- `main/main.c`：流程改为 `init → clear(0xFF) → draw_string(10,10,"Hello, IDF!") → draw_hline(0,30,EPD_W) → draw_string(10,40,"GDEW0154T8 OK") → display_full → sleep`

验证：屏上稳定显示
- 第 1 行 "Hello, IDF!"
- y=30 一条横线
- 第 2 行 "GDEW0154T8 OK"
- 字符形态正常、无错位、无残影、无颠倒

串口日志同前节点：DisplayRefresh ~1550ms、PowerOff、Deep Sleep。

**关键经验**：嵌入第三方字体源文件时，先决定它在内存里的归属——`char xxx[][]=` 在头文件里被多次包含会产生多重定义/内存膨胀；改 `static const uint8_t` 一行就能让它走 .rodata、避免链接冲突、还多一层类型语义。这个改动属于"接入"而非"修改字体内容"，与公共域许可不冲突。

## 节点 5 — README 与故障排查文档

新增 `README.md`，作为项目对外入口，覆盖：
- 项目定位（ESP-IDF v6.0.1 移植 + 严格照搬 GxEPD2_154_T8）
- 硬件接线表 + 文本接线图（GPIO 5/23/18/27/33/14、3.3V、共地）
- 默认 helloWorld 显示效果（含外圈白边的物理特性说明，链 `docs/HARDWARE_NOTES.md`）
- 公开 API 速览（init/clear/draw_pixel/draw_hline/draw_string_8x8/display_full/sleep）
- 编译烧录两种方式（交互式 `idf.py` 别名 + 自动化绝对路径模板）
- 串口端口探测命令
- 项目目录结构
- **故障排查清单**（9 条，按概率从高到低）：型号误判、BUSY 卡 0、SPI mode/接线、显示错位、花屏/残影、字符位序、串口占用、v6.0 esp_log→log 重命名、v6.0 GPIO 不再隐式上拉
- 关键参考链接（GxEPD2 源文件路径、IL0373 datasheet、GDEW0154T8 产品页、dhepper/font8x8）
- 许可声明（代码 MIT、嵌入字体公共域）

故障排查段直接复用并扩展了 `PLAN.md` 的"排查清单"，把节点 0-4 实际踩过的坑都收录了（型号纠正、BUSY 极性、152 vs 200、DTM1/DTM2 顺序、字体位序、esp_log→log）。

CHANGELOG 自身在本节点的角色：节点 0-5 段落齐全，作为本次移植的完整流水账可交付。

至此 PLAN.md 节点 0-5 全部落地：项目骨架 → SPI/GPIO 验证 → IL0373 全刷驱动 → 帧缓冲与诊断图样 → 字体与 helloWorld → 文档收尾。git 历史 5 个原子提交对应 5 个节点（节点 0/1/2/3/4 已提交，节点 5 即本提交）。
