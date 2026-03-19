#include "lcd_init_sequence.h"
#include <stddef.h>

// RM67162 初始化命令序列 (与 Mimiclaw-bak 一致)
// 注意：len字段的低7位是数据长度，最高位(0x80)表示需要延迟
const lcd_cmd_t rm67162_cmd[] = {
    {0x11, {0x00}, 0x80},                 /* Sleep Out + 120ms delay */
    {0x3A, {0x55}, 0x01},                 /* Interface Pixel Format 16bit/pixel */
    {0x51, {0x00}, 0x01},                 /* Write Display Brightness MAX_VAL=0XFF */
    {0x29, {0x00}, 0x80},                 /* Display on + 120ms delay */
    {0x51, {AMOLED_DEFAULT_BRIGHTNESS}, 0x01}, /* Write Display Brightness */
};

// 计算数组长度
const size_t RM67162_INIT_SEQUENCE_LENGTH = sizeof(rm67162_cmd) / sizeof(rm67162_cmd[0]);
