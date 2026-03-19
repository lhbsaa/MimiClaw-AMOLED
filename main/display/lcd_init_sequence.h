#pragma once

#include <stdint.h>
#include <stddef.h>

// LCD 命令结构
typedef struct {
    uint32_t addr;
    uint8_t param[20];
    uint32_t len;
} lcd_cmd_t;

// RM67162 AMOLED 配置
#define RM67162_WIDTH                           240
#define RM67162_HEIGHT                          536

// 扫描方向控制 (横屏模式)
#define RM67162_MADCTL_MY                       0x80
#define RM67162_MADCTL_MX                       0x40
#define RM67162_MADCTL_MV                       0x20
#define RM67162_MADCTL_ML                       0x10
#define RM67162_MADCTL_RGB                      0x00
#define RM67162_MADCTL_MH                       0x04
#define RM67162_MADCTL_BGR                      0x08

// 横屏模式设置 (MX | MV | RGB)
#define RM67162_MADCTL_LANDSCAPE                (RM67162_MADCTL_MX | RM67162_MADCTL_MV | RM67162_MADCTL_RGB)

// 默认亮度
#define AMOLED_DEFAULT_BRIGHTNESS               175

// RM67162 初始化命令序列
extern const lcd_cmd_t rm67162_cmd[];
extern const size_t RM67162_INIT_SEQUENCE_LENGTH;
