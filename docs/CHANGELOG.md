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

## 节点 6 — 图形基元扩展（vline / rect / fill_rect）

主线移植结束后从 PLAN「后续节点候选」选最高价值/最低工作量项落地。

代码改动：
- `epaper_154.h`：新增三个 API
  - `epaper_draw_vline(int x, int y, int len, bool black)`
  - `epaper_draw_rect(int x, int y, int w, int h, bool black)` — 矩形边框（仅 4 条边）
  - `epaper_fill_rect(int x, int y, int w, int h, bool black)` — 实心填充矩形
- `epaper_154.c`：实现三个函数。设计要点：
  - `draw_vline`：与 `draw_hline` 对称，越界静默裁剪
  - `draw_rect`：复用两次 hline + 两次 vline 凑 4 条边，避免重复实现裁剪
  - `fill_rect`：先做整体裁剪再逐行 hline；考虑过按字节优化（同行连续字节直接 memset）但 152×152 像素量级下肉眼不可感知，遵循"必要才优化"原则保持简单。每像素 0.x μs 量级，相比 1.6s 全刷无关紧要
- `main/main.c`：换成图形基元演示
  - 整屏 `draw_rect(0,0,152,152)` 验证可视区边界紧贴
  - 顶部标题 "Node 6: Shapes" + 分隔 hline
  - 中段三个 40×28 矩形横排：边框 / 填充 / 三层同心
  - "vlines:" 标签下 5 条等距 vline（间距 16px，长 30px）
  - 底部 fill_rect 横条（左右各留 8px）
  - "shapes ok" 收尾文字

验证：
- build 通过；flash 后串口日志同前节点（PowerOn 40ms、DisplayRefresh ~1550ms、PowerOff 30ms、Deep Sleep）
- 屏上肉眼确认全部 5 类图元位置正确：外框紧贴 152×152 边、三种矩形排布对称、vlines 等距、底部横条、文字清晰
- 用户确认"符合预期"，节点 6 验收通过

设计决策记录：
- **fill_rect 暂不按字节优化**：原 PLAN 提到"可走按字节优化"，实测 152×152 全屏 fill 仅占数百微秒，远小于 1.6s 全刷与 SPI 写帧时间，按字节优化的代码复杂度（首尾边界 mask、中段 memset）不值
- **rect 边框宽度固定 1px**：与 GxEPD2 / Adafruit_GFX 行为一致；如未来需要粗边框可在调用层多调几次 `draw_rect(x±i, y±i, ...)` 凑出
- **不实现 draw_line（任意方向）**：当前用例没有斜线需求，Bresenham 实现属于增量价值低、可后续按需补

## 节点 7 — 旋转支持（rotation 0/1/2/3）

代码改动：
- `epaper_154.h`：新增 3 个 API
  - `epaper_set_rotation(uint8_t rotation)` — 0=正向、1=顺时针 90°、2=180°、3=顺时针 270°
  - `epaper_width()` / `epaper_height()` — 返回当前旋转下的逻辑尺寸（rotation 1/3 时为 EPD_H/EPD_W）
- `epaper_154.c`：
  - 新增静态状态 `s_rotation`，纯软件层坐标变换，不下发任何屏命令
  - `epaper_draw_pixel`：边界用 `epaper_width()/height()`，再按 Adafruit_GFX 标准 4 个 case 把逻辑坐标映射到物理像素：
    - case 1: `(px, py) = (EPD_W-1-y, x)`
    - case 2: `(px, py) = (EPD_W-1-x, EPD_H-1-y)`
    - case 3: `(px, py) = (y, EPD_H-1-x)`
  - `draw_hline/vline/fill_rect` 的边界裁剪改用 `epaper_width()/height()`（原来用宏 EPD_W/H，旋转后会错）
  - `draw_rect` 走 hline+vline 凑边，无需改
  - `draw_string_8x8` 内部调 `draw_pixel`，自动跟随旋转
- `main/main.c`：循环 `for (r=0..3) { set_rotation(r); 重画带方向标识的 demo; display_full; vTaskDelay(2s); }`，最后才 `epaper_sleep`

旋转 demo 的方向标识：
- 左上角文字 `rot N`（紧贴用户视角的左上）
- 顶边一条实心 fill_rect 横条（标识"上方"）
- 左侧 5 条等距 vline（标识"左方"）
- 中央 "Hello, IDF!"
- 左下角 "bottom" 文字

对照 4 次刷新可清楚看到：每次刷新所有元素相对物理屏面整体顺时针旋转 90°——标签、横条、vline 跟着各自的"逻辑方位"绕屏一圈。

验证：
- build 通过；4 次 DisplayRefresh 各 ~1550ms，总耗时 ~14.6s
- 注意串口里第 1 次 PowerOn ~50ms、后续 3 次 PowerOn ~0ms：因为屏没经过 PowerOff，重新下 0x04 时控制器立刻就绪，wait_busy_idle 第一次轮询前的 1ms tickdelay 后 BUSY 已经是高。这与 GxEPD2 的行为一致，不是 bug
- 用户确认 4 个角度旋转方向都正确

设计决策：
- **旋转纯软件层实现**：不动屏命令（IL0373 也不存在 RAM 旋转命令），只在 `draw_pixel` 入口前转坐标。代价是每像素多一个 switch + 算术，量级几纳秒，相比 1.6s 全刷完全可忽略
- **正方形屏的旋转价值**：152×152 旋转 logical width/height 没有数值变化，但内容方向真实变化——主要给 UI 适配设备物理朝向（如壁挂 vs 横放）用
- **不在 IL0373 层做帧缓冲翻转**：理论上可以在写 DTM2 前把 framebuf 整片转置/翻转再发，但代码复杂度远高于 draw_pixel 入口变换，且失去"画到一半切 rotation"的灵活性。坚持走 GxEPD2/Adafruit_GFX 路线
- **不实现 mirror**：GxEPD2 有 `_mirror` 字段处理镜像（一些屏物理上左右反），本屏不需要，省了

## 节点 8 — partial refresh（局部刷新）

代码改动：
- `il0373_cmd.h`：新增 partial 三命令
  - `IL0373_PARTIAL_WINDOW = 0x90`（7 字节：x_lo, xe_lo, y_hi, y_lo, ye_hi, ye_lo, 0x01）
  - `IL0373_PARTIAL_IN     = 0x91`（无参数）
  - `IL0373_PARTIAL_OUT    = 0x92`（无参数）
- `epaper_154.c` 新增：
  - 5 张 partial LUT（vcomDC/ww/bw/wb/bb_partial），第一行 phase length = `Tx19 = 0x20`、其它行全 0；直接复制自 GxEPD2_154_T8.cpp `lut_*_partial`
  - 静态状态 `s_using_partial_mode`：跟踪当前模式，只在切换时重发 init（避免重复下 LUT）
  - `il0373_init_partial`：与 `init_full` 仅两处不同——`VCOM_DATA_INTERVAL` 0x97→0x17；LUT 用 partial 系列；末尾置 `s_using_partial_mode=true`
  - `il0373_init_full` 末尾加 `s_using_partial_mode=false`，`epaper_init` / `epaper_sleep` 也重置该状态
  - `il0373_set_partial_window(x,y,w,h)`：发 0x90 + 7 字节，x 按字节单字节（屏宽 152<256，与 GxEPD2 一致）、y 双字节
  - `epaper_display_partial(x,y,w,h)`：完整 partial 流程
- `epaper_154.h`：新增 `epaper_display_partial` 声明
- `main/CMakeLists.txt`：加 `PRIV_REQUIRES esp_timer`（用 `esp_timer_get_time` 测耗时）
- `main/main.c`：counter 演示——先 full 画"Partial demo"标题与数字框；循环 5 次 partial 把框内数字 1→5 刷新；末尾 full 清屏作对比

`epaper_display_partial` 关键设计：
1. **首次自动转 full**：若 `s_initial_refresh==true`，partial 没基线会乱刷，自动调 `epaper_display_full()`（与 GxEPD2 行为一致）
2. **8 字节强制对齐**：IL0373 RAM 寻址按字节，x 向下对齐到 8 边界、w 向上对齐到 8 边界，对齐过程吞掉的偏移先补回再对齐
3. **模式切换懒执行**：`if (!s_using_partial_mode) il0373_init_partial();`，连续 partial 不重发 LUT
4. **双写（write+refresh+write）**：IL0373 partial 刷新后控制器内部 current/previous 角色互换，不再写一次会导致下次 partial 用过期帧做差分基线产生 ghost。在同一 API 内做闭环，上层调用语义干净

验证（串口实测）：
- 首次全刷耗时 **1598ms**（含 init_full 序列）
- 5 次 partial 耗时各 **360ms**（GxEPD2 标称 350ms，几乎完美吻合）
- **partial 比 full 快 ~4.4×**
- 末尾全刷耗时 **1549ms**，恢复全屏白底 + 新内容
- 切到 partial 模式时 PowerOn(P) 0ms（控制器已上电、wait_busy_idle 1ms tick 后 BUSY 已是高，与 init_full 重新切回 full 模式时同理）
- 用户肉眼确认：partial 时屏不再全屏闪烁、只局部刷新数字区，其它内容保持稳定

设计决策：
- **partial 用物理坐标、不跟随 rotation**：rotation 1/3 下 8 对齐约束作用于物理 x（对应逻辑 y），逻辑窗口转物理 bbox 后还要再对齐——复杂度暴涨。第一版让用户在 rotation=0 下用 partial。如确需 rotation 下用，未来可加内部坐标变换层
- **partial LUT 第一行用 0x20 而不是 0x19**：GxEPD2 注释明确说 0x19=25 phase length 太短（屏行为不稳）、0x20=32 更稳；照搬不动
- **不暴露 `epaper_using_partial_mode()` 查询**：上层不需要知道当前模式，状态由本组件全权管理。如未来需要诊断再加
- **partial 写帧的 SPI transaction 按行拆分**：每行 `w/8` 字节一次 `send_data`，例如 80×16 窗口共 16 次 SPI transaction、每次 10 字节。看似低效但 SPI 总耗时 <2ms（远小于 360ms 刷新），优化每行合并成单次 transaction 改动量大、收益不可见，不做
- **不做"残影 N 次后自动 full 清屏"**：是上层 UI 策略而非驱动职责（不同应用阈值不同）。文档建议用户每隔 ~10 次 partial 调 `epaper_display_full()` 清一次

**关键经验**：partial 的双写是 IL0373 系列的硬要求（GxEPD2 在 drawImage 而非 refresh 内做了双写）。如果只在 API 内单写 + refresh，肉眼第一次看不出问题，但连续 partial 几次就会看到上一次的残影叠加。这种"对了一半"的 bug 比完全错更难定位——必须照搬 ground truth 的完整流程，不能省略看似冗余的步骤

## 节点 9 — GFX 字体渲染器 + 默认字体

代码改动：
- `components/epaper_154/include/gfxfont.h`（新增）：从 Adafruit-GFX-Library 1:1 复制 `GFXglyph/GFXfont` 结构定义，保留 BSD 3-clause 版权头；加 `#define PROGMEM` 空宏让原版字体 `.h` 不必修改 `PROGMEM` 关键字
- `components/epaper_154/fonts/FreeSansBold9pt7b.h`（新增）：从 Adafruit-GFX-Library 复制，仅改第二行 `#include <Adafruit_GFX.h>` → `#include "gfxfont.h"`，字体本体数据原封不动；加注释说明数据来源 + BSD 头位置
- `components/epaper_154/CMakeLists.txt`：`INCLUDE_DIRS` 增加 `"fonts"`，让上层用户 `#include "FreeSansBold9pt7b.h"` 即可（无需相对路径）
- `epaper_154.h`：新增 `#include "gfxfont.h"`、两个 API
  - `epaper_draw_string_gfx(x, y, s, font, black)` — (x,y) 是**baseline**坐标
  - `epaper_get_text_bounds_gfx(s, font, &w, &h)` — 算字符串渲染包围盒（居中对齐用）
- `epaper_154.c`：新增 GFX 渲染逻辑
  - `draw_char_gfx`：按 `GFXglyph` 元数据从 `bitmap[]` bit-stream 取像素，MSB 在左（与帧缓冲一致，比 8×8 字体的 LSB-first 顺）
  - `epaper_draw_string_gfx`：循环字符 + 每字符 `xAdvance` 推进；`'\n'` 换行 cursor x 回归、y += `font->yAdvance`；超出 `[first, last]` 范围的字符跳过但保留 `glyph[0].xAdvance` 间距
  - `epaper_get_text_bounds_gfx`：累加 `xAdvance`，h 取 `yAdvance`
- `main/main.c`：节点 9 演示——上半屏 8×8 字体对照、下半屏 FreeSansBold9pt7b 三种用法（普通显示 / `get_text_bounds` 居中 / `'\n'` 多行）

**视觉评估与字体选型迭代**：
- 第一版用 FreeSansBold9pt7b 作为默认字。验收时用户反映"拐角锯齿感很强"——这是 1bit 输出在小字号下的固有现象（FreeSans 是矢量字体经 fontconvert 栅格化，9pt ≈ 13px 高，圆角和斜线像素不够 → 楼梯化）
- 临时增加 FreeMonoBold9pt7b（直角等宽风）做同屏对比验证
- 用户对比后决定**保留 FreeSansBold9pt7b 作为基础库默认字**——FreeMono 拿掉，fonts/ 仅留 FreeSansBold9pt7b 一个示例字
- 后续应用层若想换字体，只需把 Adafruit_GFX 仓库任意 `.h` 复制进 `fonts/`、改第二行 include 即可，库代码无需任何修改

设计决策：
- **(x, y) 取 baseline**：与 Adafruit_GFX 一致；`GFXglyph::yOffset` 是相对 baseline 的偏移（通常负数），不做转换最直接。代价是上层定位字符串"上沿"时需要 `y_baseline = y_top + yAdvance - 5` 之类的换算，不直观但能避免双重坐标系
- **`'\n'` 由渲染器处理**：cursor x 回归到入参 x（不是回归到 0），与"段落"语义一致
- **超出范围字符保留间距**：用 `glyph[0].xAdvance` 兜底，避免奇怪字符让后续字符贴在一起
- **PROGMEM 空宏接收原版字体**：让 fonts/ 里的 .h 与 Adafruit_GFX 仓库逐字节一致（仅 include 行不同），未来更新字体只需重抓 + 改一行；如果要把 PROGMEM 真当 Flash 段语义实现就要改 IDF link script，得不偿失
- **不实现 `epaper_set_font` 全局状态**：每次调用显式传 `const GFXfont *`，避免隐式状态，便于多字体混排
- **不做 GFX 字体自动换行（word wrap）**：是 UI 策略不是字体渲染职责；上层自己用 `get_text_bounds_gfx` 算宽度后切分

**关键经验**：1bit 小屏字体选型不能只看"字体本身好看"——要看**栅格化后**在 13-16px 高度下的样子。FreeSans 矢量字体在屏幕上是"小尺寸 fontconvert 出来"的产物，与原 TTF 美感无关。下次做"更好看的小字号字体"应转向：
- 专为 1bit 设计的位图字体（U8g2 helvB10 / inb 系列、Topaz 风格点阵字）
- 或用更大字号（12pt/18pt）让锯齿相对像素总数变小
本次保留 FreeSans 是用户在对比 FreeMono 后的明确选择，作为基础库的"占位默认字"——上层项目要更好的视觉应自己换字

## 节点 10 — bitmap 绘制

代码改动：
- `epaper_154.h`：新增 `epaper_draw_bitmap(x, y, bmp, w, h, black)` 声明
- `epaper_154.c`：实现按行 raster scan 解析 1bit bitmap
  - 行间跨度 = `(w + 7) / 8` 字节，行内 MSB 在左（与帧缓冲一致）
  - bit=1 时画 `black` 参数指定的颜色，bit=0 不动（**透明覆盖语义**）
  - 内层调 `epaper_draw_pixel`，自动跟随 rotation + 越界裁剪
- `main/main.c`：节点 10 演示
  - 硬编码一份 16×16 上箭头 bitmap（32 字节，注释带 `#/.` 直观显示像素）
  - 同一份数据贴 5 处：屏中央 + 4 角
  - 中央箭头外加 `draw_rect` 方框，演示 bitmap 与图形基元叠加
  - 顶部标题、中央方框下方说明文字 "x5 same data"

**布局调整记录**：第一版把说明文字放在 `(8, EPD_H-12)`，与左下角箭头 (y=128..143) 重叠 4 行。改到中央方框正下方 `(28, cy+24)`、水平居中——避开 4 个角箭头位置。坐标算错的教训：写 demo 时要在脑子里把所有元素的 y 范围列出来过一遍，特别是边缘元素与底部 EPD_H-N 的重叠

设计决策：
- **透明覆盖（bit=0 不画）vs 强制覆盖（bit=0 画反色）**：选 Adafruit_GFX 风格的透明覆盖。理由：
  - 上层可在已有内容（如背景纹理）上贴 logo 不破坏底色
  - 想要"清屏 + 画图"语义可显式 `fill_rect(...)` 后再 `draw_bitmap`
  - 反过来强制覆盖语义无法回退到透明，灵活度更低
- **不加 `invert` 参数**：上层切换 `black=true/false` 已能控制 bit=1 时画哪个色（true 涂黑、false 涂白）；XBM 风格反向位序留待真用上 XBM 数据时再加
- **不限制 8 对齐**：与 GxEPD2 的 partial bitmap 写不同——我们走 draw_pixel 入口，没有 IL0373 RAM 寻址的字节边界约束，任意 (x, y) 都行
- **不压缩位图**：1bit raster 已经是最紧凑的"通用格式"。RLE/PackBits 之类压缩对小图标 (16×16=32B) 反而增大；上层若有大图频繁刷新可自己解压到 RAM 后调本 API

验证：build 通过，bin 与节点 9 比仅多出 32 字节（bitmap 数据 + 渲染器代码 ~80 字节）。屏上 5 个箭头形状完全一致（同一份数据），位置精确在 4 角 + 中央，外框紧贴中央箭头 4px 间距。用户确认验收通过

**关键经验**：bitmap 概念在墨水屏库的位置是"完整能力闭环的最后一块"——加上之后，库支持三种数据来源：
1. **几何**（draw_pixel/line/rect/fill_rect）：从规则生成像素
2. **字体**（draw_string_8x8 / draw_string_gfx）：从字符索引到预存 glyph 数据
3. **bitmap**：直接从一块预先准备的像素数据贴到帧缓冲

字体本质是 bitmap 的特例（每字符是个小 bitmap + 间距元数据），加 bitmap 后用户能显示任何 PC 上预生成的图——logo / 二维码 / 手画图标 / 二值化照片。这是"纯几何 → 真实 UI"的关键步




