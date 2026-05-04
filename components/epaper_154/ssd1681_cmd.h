// SSD1681 命令字节宏定义
// 仅包含本项目实际用到的命令；完整命令表见 SSD1681 datasheet

#pragma once

// 软复位
#define SSD1681_CMD_SW_RESET            0x12

// 驱动输出控制（需跟 3 字节参数：MUX[7:0]、MUX[8]、GD/SM/TB）
#define SSD1681_CMD_DRIVER_OUTPUT_CTRL  0x01

// 数据输入模式（X/Y 自增方向）
#define SSD1681_CMD_DATA_ENTRY_MODE     0x11

// 设置 X/Y RAM 范围 与 起始指针
#define SSD1681_CMD_SET_RAMX_RANGE      0x44
#define SSD1681_CMD_SET_RAMY_RANGE      0x45
#define SSD1681_CMD_SET_RAMX_PTR        0x4E
#define SSD1681_CMD_SET_RAMY_PTR        0x4F

// 边框波形 / 温度传感器
#define SSD1681_CMD_BORDER_WAVEFORM     0x3C
#define SSD1681_CMD_TEMP_SENSOR_CTRL    0x18

// 显示更新控制 1 / 2
#define SSD1681_CMD_DISP_UPDATE_CTRL1   0x21
#define SSD1681_CMD_DISP_UPDATE_CTRL2   0x22

// 写黑白 RAM / 主激活 / 深度睡眠
#define SSD1681_CMD_WRITE_BW_RAM        0x24
#define SSD1681_CMD_MASTER_ACTIVATION   0x20
#define SSD1681_CMD_DEEP_SLEEP          0x10
