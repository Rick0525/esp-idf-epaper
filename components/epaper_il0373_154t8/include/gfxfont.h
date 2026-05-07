// GFX 字体数据结构（兼容 Adafruit_GFX 1.1+ 字体头文件格式）
//
// 本文件直接复制自 Adafruit-GFX-Library 仓库的 gfxfont.h，仅做以下本地化：
//   1. 添加 PROGMEM 空宏定义，让字体 .h 文件原样可用（ESP32 const 自动放 .rodata）
//   2. 添加 #include <stdint.h> 让 uint*_t 类型可用
//
// 使用方式：在 IDF 项目里照搬 Adafruit_GFX 的字体 .h 到 fonts/ 目录、
// 把开头的 `#include <Adafruit_GFX.h>` 改成 `#include "gfxfont.h"` 即可，
// 字体本体数据原封不动。
//
// 原始版权（按 BSD 3-clause 许可证条款保留）：
// ----------------------------------------------------------------
// Software License Agreement (BSD License)
//
// Copyright (c) 2012 Adafruit Industries.  All rights reserved.
//
// Redistribution and use in source and binary forms, with or without
// modification, are permitted provided that the following conditions are met:
//
// - Redistributions of source code must retain the above copyright notice,
//   this list of conditions and the following disclaimer.
// - Redistributions in binary form must reproduce the above copyright notice,
//   this list of conditions and the following disclaimer in the documentation
//   and/or other materials provided with the distribution.
//
// THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
// AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
// IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
// ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
// LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
// CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
// SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
// INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
// CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
// ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
// POSSIBILITY OF SUCH DAMAGE.
// ----------------------------------------------------------------

#ifndef _GFXFONT_H_
#define _GFXFONT_H_

#include <stdint.h>

// Adafruit_GFX 字体源把数据放 PROGMEM（AVR Flash 段）；ESP32 上 const 数据
// 自动驻 .rodata（Flash），所以让 PROGMEM 展开为空，字体 .h 文件不用改
#ifndef PROGMEM
#define PROGMEM
#endif

/// 单个 glyph 的字体数据
typedef struct {
    uint16_t bitmapOffset;  ///< 在 GFXfont->bitmap[] 中的字节偏移
    uint8_t  width;         ///< glyph 像素宽
    uint8_t  height;        ///< glyph 像素高
    uint8_t  xAdvance;      ///< 写完该字符后光标 x 推进多少像素
    int8_t   xOffset;       ///< 从光标 x 到 glyph 左上的偏移（通常负数或 0）
    int8_t   yOffset;       ///< 从光标 baseline y 到 glyph 顶的偏移（通常负数）
} GFXglyph;

/// 整个字体的元数据
typedef struct {
    uint8_t  *bitmap;       ///< 所有 glyph 位图拼接后的连续字节流（每 bit 1 像素，MSB 先）
    GFXglyph *glyph;        ///< glyph 数组，索引 = ASCII - first
    uint16_t  first;        ///< 字体覆盖的第一个 ASCII 码（如 0x20 空格）
    uint16_t  last;         ///< 字体覆盖的最后一个 ASCII 码（如 0x7E ~）
    uint8_t   yAdvance;     ///< 行高（换行时 baseline y 增加多少）
} GFXfont;

#endif // _GFXFONT_H_
