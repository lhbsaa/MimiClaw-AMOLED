/**
 * @file simple_gui.c
 * @brief 帧缓冲 GUI — 移植自 Mimiclaw-bak ui_status.c
 *
 * 使用帧缓冲 (536x240 RGB565 ≈ 257 KB)
 * 所有绘图操作在帧缓冲上进行，最后通过 gui_flush() 推送到屏幕
 */

#include "simple_gui.h"
#include "display_manager.h"
#include "driver/gpio.h"
#include "driver/spi_master.h"
#include "esp_log.h"
#include "esp_heap_caps.h"
#include "esp_system.h"
#include <string.h>
#include <stdarg.h>
#include <math.h>
#include <time.h>

static const char *TAG = "simple_gui";

// 外部 SPI 句柄声明
extern spi_device_handle_t spi_handle;

// 帧缓冲 (PSRAM 或内部 RAM)
static uint16_t *s_fb = NULL;

// 显示是否已初始化
static bool s_initialized = false;

// 颜色定义 - 字节交换后的 RGB565 值 (与 rgb565() 函数一致)
#define C_BLACK   0x0000
#define C_WHITE   0xFFFF
#define C_RED     0x00F8   // rgb565(255,0,0) = 0xF800 -> swap -> 0x00F8
#define C_GREEN   0xE007   // rgb565(0,255,0) = 0x07E0 -> swap -> 0xE007
#define C_BLUE    0x1F00   // rgb565(0,0,255) = 0x001F -> swap -> 0x1F00
#define C_CYAN    0xFF07   // rgb565(0,255,255) = 0x07FF -> swap -> 0xFF07
#define C_YELLOW  0xE0FF   // rgb565(255,255,0) = 0xFFE0 -> swap -> 0xE0FF
#define C_ORANGE  0x20FD   // rgb565(255,165,0) = 0xFD20 -> swap -> 0x20FD
#define C_DGRAY   0x0842   // rgb565(66,66,66) swapped
#define C_LGRAY   0x18C6   // rgb565(198,198,198) swapped
#define C_PRIMARY 0x07FF

// RGB565 颜色生成并交换字节 
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    return (c >> 8) | (c << 8);  // 交换高低字节
}

// CS 引脚控制
static inline void cs_high(void)
{
    gpio_set_level(AMOLED_PIN_CS, 1);
}

static inline void cs_low(void)
{
    gpio_set_level(AMOLED_PIN_CS, 0);
}

// QSPI 发送命令（内部函数，调用者负责加锁）
static esp_err_t qspi_send_cmd(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.flags = (SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR);
    t.cmd = 0x02;
    t.addr = ((uint32_t)cmd) << 8;

    if (data && len > 0) {
        t.tx_buffer = data;
        t.length = 8 * len;
    }

    cs_low();
    esp_err_t ret = spi_device_polling_transmit(spi_handle, &t);
    cs_high();
    return ret;
}

// 推送像素数据到屏幕（内部函数，调用者负责加锁）
static void push_pixels(const uint16_t *data, uint32_t pixel_count, bool first_send)
{
    if (!data || pixel_count == 0) return;

    spi_transaction_ext_t t_ext = {0};
    memset(&t_ext, 0, sizeof(t_ext));
    
    if (first_send) {
        t_ext.base.flags = SPI_TRANS_MODE_QIO;
        t_ext.base.cmd = 0x32;
        t_ext.base.addr = 0x002C00;
    } else {
        t_ext.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
        t_ext.command_bits = 0;
        t_ext.address_bits = 0;
        t_ext.dummy_bits = 0;
    }
    
    t_ext.base.tx_buffer = data;
    t_ext.base.length = pixel_count * 16;

    spi_device_polling_transmit(spi_handle, (spi_transaction_t *)&t_ext);
}

// 设置显示窗口（使用 qspi_send_cmd，与 display_manager_clear 一致）
static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t ca[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t ra[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};

    qspi_send_cmd(0x2A, ca, 4);  /* Column Address Set */
    qspi_send_cmd(0x2B, ra, 4);  /* Row Address Set */
}

// ── 帧缓冲绘图原语 ─────────────────────────────────────────────

static inline void fb_set_pixel(uint16_t x, uint16_t y, uint16_t color)
{
    if (x < SCREEN_WIDTH && y < SCREEN_HEIGHT && s_fb) {
        s_fb[y * SCREEN_WIDTH + x] = color;
    }
}

static void fb_fill_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    if (!s_fb) return;
    
    // 边界检查
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    
    // 使用 32 位写入加速
    uint32_t color32 = ((uint32_t)color << 16) | color;
    
    // 逐行填充
    for (uint16_t iy = y; iy < y + h; iy++) {
        uint16_t *row_start = &s_fb[iy * SCREEN_WIDTH + x];
        uint16_t pixels_left = w;
        
        // 对齐到 32 位边界
        if ((uintptr_t)row_start & 2) {
            *row_start++ = color;
            pixels_left--;
        }
        
        // 32 位填充
        if (pixels_left >= 2) {
            uint32_t *row32 = (uint32_t *)row_start;
            uint32_t count = pixels_left / 2;
            for (uint32_t i = 0; i < count; i++) {
                row32[i] = color32;
            }
            row_start += count * 2;
            pixels_left &= 1;
        }
        
        // 处理剩余像素
        if (pixels_left) {
            *row_start = color;
        }
    }
}

static void fb_draw_hline(uint16_t x, uint16_t y, uint16_t w, uint16_t color)
{
    fb_fill_rect(x, y, w, 1, color);
}

static void fb_draw_rect(uint16_t x, uint16_t y, uint16_t w, uint16_t h, uint16_t color)
{
    fb_draw_hline(x, x + w - 1, y, color);
    fb_draw_hline(x, x + w - 1, y + h - 1, color);
    for (uint16_t iy = y; iy < y + h && iy < SCREEN_HEIGHT; iy++) {
        fb_set_pixel(x, iy, color);
        fb_set_pixel(x + w - 1, iy, color);
    }
}

static void fb_draw_vline(uint16_t x, uint16_t y0, uint16_t y1, uint16_t color)
{
    if (y0 > y1) { uint16_t tmp = y0; y0 = y1; y1 = tmp; }
    for (uint16_t y = y0; y <= y1 && y < SCREEN_HEIGHT; y++) {
        fb_set_pixel(x, y, color);
    }
}

// 5x7 ASCII 位图字体
static const uint8_t font5x7[][5] = {
    {0x00,0x00,0x00,0x00,0x00}, /* space */
    {0x00,0x00,0x5F,0x00,0x00}, /* ! */
    {0x00,0x07,0x00,0x07,0x00}, /* " */
    {0x14,0x7F,0x14,0x7F,0x14}, /* # */
    {0x24,0x2A,0x7F,0x2A,0x12}, /* $ */
    {0x23,0x13,0x08,0x64,0x62}, /* % */
    {0x36,0x49,0x55,0x22,0x50}, /* & */
    {0x00,0x05,0x03,0x00,0x00}, /* ' */
    {0x00,0x1C,0x22,0x41,0x00}, /* ( */
    {0x00,0x41,0x22,0x1C,0x00}, /* ) */
    {0x08,0x2A,0x1C,0x2A,0x08}, /* * */
    {0x08,0x08,0x3E,0x08,0x08}, /* + */
    {0x00,0x50,0x30,0x00,0x00}, /* , */
    {0x08,0x08,0x08,0x08,0x08}, /* - */
    {0x00,0x60,0x60,0x00,0x00}, /* . */
    {0x20,0x10,0x08,0x04,0x02}, /* / */
    {0x3E,0x51,0x49,0x45,0x3E}, /* 0 */
    {0x00,0x42,0x7F,0x40,0x00}, /* 1 */
    {0x42,0x61,0x51,0x49,0x46}, /* 2 */
    {0x21,0x41,0x45,0x4B,0x31}, /* 3 */
    {0x18,0x14,0x12,0x7F,0x10}, /* 4 */
    {0x27,0x45,0x45,0x45,0x39}, /* 5 */
    {0x3C,0x4A,0x49,0x49,0x30}, /* 6 */
    {0x01,0x71,0x09,0x05,0x03}, /* 7 */
    {0x36,0x49,0x49,0x49,0x36}, /* 8 */
    {0x06,0x49,0x49,0x29,0x1E}, /* 9 */
    {0x00,0x36,0x36,0x00,0x00}, /* : */
    {0x00,0x56,0x36,0x00,0x00}, /* ; */
    {0x00,0x08,0x14,0x22,0x41}, /* < */
    {0x14,0x14,0x14,0x14,0x14}, /* = */
    {0x41,0x22,0x14,0x08,0x00}, /* > */
    {0x02,0x01,0x51,0x09,0x06}, /* ? */
    {0x32,0x49,0x79,0x41,0x3E}, /* @ */
    {0x7E,0x11,0x11,0x11,0x7E}, /* A */
    {0x7F,0x49,0x49,0x49,0x36}, /* B */
    {0x3E,0x41,0x41,0x41,0x22}, /* C */
    {0x7F,0x41,0x41,0x22,0x1C}, /* D */
    {0x7F,0x49,0x49,0x49,0x41}, /* E */
    {0x7F,0x09,0x09,0x01,0x01}, /* F */
    {0x3E,0x41,0x41,0x51,0x32}, /* G */
    {0x7F,0x08,0x08,0x08,0x7F}, /* H */
    {0x00,0x41,0x7F,0x41,0x00}, /* I */
    {0x20,0x40,0x41,0x3F,0x01}, /* J */
    {0x7F,0x08,0x14,0x22,0x41}, /* K */
    {0x7F,0x40,0x40,0x40,0x40}, /* L */
    {0x7F,0x02,0x04,0x02,0x7F}, /* M */
    {0x7F,0x04,0x08,0x10,0x7F}, /* N */
    {0x3E,0x41,0x41,0x41,0x3E}, /* O */
    {0x7F,0x09,0x09,0x09,0x06}, /* P */
    {0x3E,0x41,0x51,0x21,0x5E}, /* Q */
    {0x7F,0x09,0x19,0x29,0x46}, /* R */
    {0x46,0x49,0x49,0x49,0x31}, /* S */
    {0x01,0x01,0x7F,0x01,0x01}, /* T */
    {0x3F,0x40,0x40,0x40,0x3F}, /* U */
    {0x1F,0x20,0x40,0x20,0x1F}, /* V */
    {0x7F,0x20,0x18,0x20,0x7F}, /* W */
    {0x63,0x14,0x08,0x14,0x63}, /* X */
    {0x03,0x04,0x78,0x04,0x03}, /* Y */
    {0x61,0x51,0x49,0x45,0x43}, /* Z */
    {0x00,0x00,0x7F,0x41,0x41}, /* [ */
    {0x02,0x04,0x08,0x10,0x20}, /* \ */
    {0x41,0x41,0x7F,0x00,0x00}, /* ] */
    {0x04,0x02,0x01,0x02,0x04}, /* ^ */
    {0x40,0x40,0x40,0x40,0x40}, /* _ */
    {0x00,0x01,0x02,0x04,0x00}, /* ` */
    {0x20,0x54,0x54,0x54,0x78}, /* a */
    {0x7F,0x48,0x44,0x44,0x38}, /* b */
    {0x38,0x44,0x44,0x44,0x20}, /* c */
    {0x38,0x44,0x44,0x48,0x7F}, /* d */
    {0x38,0x54,0x54,0x54,0x18}, /* e */
    {0x08,0x7E,0x09,0x01,0x02}, /* f */
    {0x08,0x54,0x54,0x54,0x3C}, /* g */
    {0x7F,0x08,0x04,0x04,0x78}, /* h */
    {0x00,0x44,0x7D,0x40,0x00}, /* i */
    {0x20,0x40,0x44,0x3D,0x00}, /* j */
    {0x00,0x7F,0x10,0x28,0x44}, /* k */
    {0x00,0x41,0x7F,0x40,0x00}, /* l */
    {0x7C,0x04,0x18,0x04,0x78}, /* m */
    {0x7C,0x08,0x04,0x04,0x78}, /* n */
    {0x38,0x44,0x44,0x44,0x38}, /* o */
    {0x7C,0x14,0x14,0x14,0x08}, /* p */
    {0x08,0x14,0x14,0x18,0x7C}, /* q */
    {0x7C,0x08,0x04,0x04,0x08}, /* r */
    {0x48,0x54,0x54,0x54,0x20}, /* s */
    {0x04,0x3F,0x44,0x40,0x20}, /* t */
    {0x3C,0x40,0x40,0x20,0x7C}, /* u */
    {0x1C,0x20,0x40,0x20,0x1C}, /* v */
    {0x3C,0x40,0x30,0x40,0x3C}, /* w */
    {0x44,0x28,0x10,0x28,0x44}, /* x */
    {0x0C,0x50,0x50,0x50,0x3C}, /* y */
    {0x44,0x64,0x54,0x4C,0x44}, /* z */
};

// ============================================================================
// 16x16 Emoji 位图 (每个 emoji 32 字节, 16 行 x 16 位)
// 使用 scale 参数放大显示: scale=1 为 16x16, scale=2 为 32x32
// ============================================================================
#define EMOJI_SIZE  16
#define EMOJI_ROW_BYTES  2  // 每行 16 位 = 2 字节

// Emoji 颜色定义 - 提高对比度
static const uint16_t emoji_colors[] = {
    [EMOJI_OK]          = 0x07E0,      // ✓ 亮绿色 (0,255,0)
    [EMOJI_FAIL]        = 0xF800,      // ✗ 亮红色 (255,0,0)
    [EMOJI_WARN]        = 0xFFE0,      // ⚠ 亮黄色 (255,255,0)
    [EMOJI_INFO]        = 0x07FF,      // ℹ 亮青色 (0,255,255)
    [EMOJI_QUESTION]    = 0xFFFF,      // ? 亮白色
    [EMOJI_BATTERY]     = 0x07E0,      // 🔋 亮绿色
    [EMOJI_BATTERY_LOW] = 0xF800,      // 🔋 亮红色
    [EMOJI_WIFI]        = 0x07E0,      // 📶 亮绿色
    [EMOJI_WIFI_OFF]    = 0x7BEF,      // 📶 中灰
    [EMOJI_ROBOT]       = 0x07FF,      // 🤖 亮青色
    [EMOJI_BRAIN]       = 0xF81F,      // 🧠 亮粉色
    [EMOJI_LIGHT]       = 0xFFE0,      // 💡 亮黄色
    [EMOJI_SPARK]       = 0xFFE0,      // ⚡ 亮黄色
    [EMOJI_MSG]         = 0x07FF,      // 📨 亮青色
    [EMOJI_CHAT]        = 0x07E0,      // 💬 亮绿色
    [EMOJI_SEND]        = 0x07FF,      // 📤 亮青色
    [EMOJI_SMILE]       = 0xFFE0,      // 😊 亮黄色
    [EMOJI_SLEEP]       = 0x87FF,      // 😴 浅蓝
    [EMOJI_THINK]       = 0xFFE0,      // 🤔 亮黄色
    [EMOJI_CLOCK]       = 0xFFFF,      // 🕐 亮白色
    [EMOJI_SUN]         = 0xFFE0,      // ☀ 亮黄色
    [EMOJI_MOON]        = 0xFFE0,      // 🌙 亮黄色
    [EMOJI_HOME]        = 0x07FF,      // 🏠 亮青色
    [EMOJI_SETTINGS]    = 0xB5B6,      // ⚙ 亮灰色
    [EMOJI_SEARCH]      = 0xFFFF,      // 🔍 亮白色
    [EMOJI_RESTART]     = 0x07E0,      // 🔄 亮绿色
};

// Emoji 位图数据 (16x16, 每行 2 字节, 16 行 = 32 字节/emoji)
// 位为 1 表示绘制，位为 0 表示透明
// 使用 scale=2 可放大到 32x32 像素
static const uint8_t emoji_bitmaps[][EMOJI_SIZE * EMOJI_ROW_BYTES] = {
    // EMOJI_NONE - 空
    [EMOJI_NONE] = {0},
    
    // EMOJI_OK - ✓ 勾号
    [EMOJI_OK] = {
        0x00,0x00,  // 
        0x00,0x40,  //        •
        0x00,0x20,  //       •
        0x00,0x10,  //      •
        0x00,0x08,  //     •
        0x00,0x04,  //    •
        0x80,0x04,  // •  •
        0x40,0x08,  //  •    •
        0x20,0x10,  //   •  •
        0x10,0x20,  //    ••
        0x08,0x40,  //   • •
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_FAIL - ✗ 叉号
    [EMOJI_FAIL] = {
        0x00,0x00,  // 
        0x20,0x08,  //   •       •
        0x10,0x10,  //    •     •
        0x08,0x20,  //     •   •
        0x04,0x40,  //      • •
        0x02,0x80,  //       •
        0x01,0x00,  //        •
        0x02,0x80,  //       • •
        0x04,0x40,  //      • •
        0x08,0x20,  //     •   •
        0x10,0x10,  //    •     •
        0x20,0x08,  //   •       •
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_WARN - ⚠ 警告三角
    [EMOJI_WARN] = {
        0x00,0x00,  // 
        0x00,0x80,  //        •
        0x01,0xC0,  //       •••
        0x03,0xE0,  //      ••••
        0x03,0xE0,  //      ••••
        0x06,0x30,  //     ••  ••
        0x06,0x30,  //     ••  ••
        0x0C,0x18,  //    ••    ••
        0x0C,0x18,  //    ••    ••
        0x1F,0xF8,  //   •••••••••
        0x18,0x18,  //   ••    ••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_INFO - ℹ 信息圆圈
    [EMOJI_INFO] = {
        0x03,0xC0,  //      ••••
        0x07,0xE0,  //     ••••••
        0x0E,0x70,  //    •••   •••
        0x1C,0x38,  //   •••     •••
        0x18,0x18,  //   ••       ••
        0x18,0xD8,  //   ••  ••   ••
        0x18,0x18,  //   ••       ••
        0x18,0x18,  //   ••       ••
        0x18,0x18,  //   ••       ••
        0x1C,0x38,  //   •••     •••
        0x0E,0x70,  //    •••   •••
        0x07,0xE0,  //     ••••••
        0x03,0xC0,  //      ••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_QUESTION - ? 问号
    [EMOJI_QUESTION] = {
        0x07,0xE0,  //     ••••••
        0x1F,0xF8,  //   •••••••••
        0x3C,0x3C,  //  ••••   ••••
        0x30,0x0C,  //  ••       ••
        0x00,0x18,  //       ••
        0x00,0x30,  //      ••
        0x00,0x60,  //     ••
        0x00,0xC0,  //    ••
        0x01,0x80,  //   ••
        0x01,0x80,  //   ••
        0x01,0x80,  //   ••
        0x00,0x00,  // 
        0x01,0x80,  //   ••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_BATTERY - 🔋 电池满
    [EMOJI_BATTERY] = {
        0x01,0xC0,  //      •••
        0x01,0xC0,  //      •••
        0x3F,0xF8,  // ••••••••••
        0x7F,0xFC,  // ••••••••••••
        0xFF,0xFE,  // ••••••••••••••
        0xFF,0xFE,  // ••••••••••••••
        0xFF,0xFE,  // ••••••••••••••
        0xFF,0xFE,  // ••••••••••••••
        0xFF,0xFE,  // ••••••••••••••
        0x7F,0xFC,  // ••••••••••••
        0x3F,0xF8,  // ••••••••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_BATTERY_LOW - 🔋 电池低
    [EMOJI_BATTERY_LOW] = {
        0x01,0xC0,  //      •••
        0x01,0xC0,  //      •••
        0x3F,0xF8,  // ••••••••••
        0x70,0x1C,  // •••       •••
        0x60,0x0C,  // ••         ••
        0x60,0x0C,  // ••         ••
        0x60,0x0C,  // ••         ••
        0x60,0x0C,  // ••         ••
        0x70,0x1C,  // •••       •••
        0x3F,0xF8,  // ••••••••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_WIFI - 📶 WiFi信号
    [EMOJI_WIFI] = {
        0x00,0x80,  //        •
        0x01,0xC0,  //       •••
        0x03,0xE0,  //      •••••
        0x06,0x30,  //     ••   ••
        0x0C,0x18,  //    ••     ••
        0x1F,0xF8,  //   •••••••••
        0x18,0x18,  //   ••     ••
        0x07,0xE0,  //      •••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_WIFI_OFF - 📶 WiFi断开
    [EMOJI_WIFI_OFF] = {
        0x18,0x18,  //   ••       ••
        0x0C,0x30,  //    ••     ••
        0x06,0x60,  //     ••   ••
        0x03,0xC0,  //      ••••
        0x00,0x80,  //        •
        0x01,0x80,  //       ••
        0x07,0xE0,  //      •••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_ROBOT - 🤖 机器人
    [EMOJI_ROBOT] = {
        0x01,0x80,  //       ••
        0x01,0x80,  //       ••
        0x01,0x80,  //       ••
        0x0F,0xF0,  //     •••••••
        0x1E,0x78,  //    ••••   ••••
        0x38,0x1C,  //    •••     •••
        0x30,0x0C,  //    ••       ••
        0xFF,0xFC,  // ••••••••••••••
        0xC9,0x94,  // ••  •  ••  • •
        0xC9,0x94,  // ••  •  ••  • •
        0xFF,0xFC,  // ••••••••••••••
        0xAA,0xAC,  // •• • • • • • ••
        0xAA,0xAC,  // •• • • • • • ••
        0x07,0xE0,  //      ••••••
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_BRAIN - 🧠 大脑
    [EMOJI_BRAIN] = {
        0x03,0xC0,  //      ••••
        0x0F,0xF0,  //    •••••••
        0x1C,0x38,  //   •••     •••
        0x34,0x2C,  //  •• •   • ••
        0x6A,0x56,  // •• • • • • ••
        0x55,0xAA,  // • • • •••• •
        0x55,0xAA,  // • • • •••• •
        0x6A,0x56,  // •• • • • • ••
        0x34,0x2C,  //  •• •   • ••
        0x1C,0x38,  //   •••     •••
        0x0F,0xF0,  //    •••••••
        0x03,0xC0,  //      ••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_LIGHT - 💡 灯泡
    [EMOJI_LIGHT] = {
        0x03,0xC0,  //      ••••
        0x0F,0xF0,  //    •••••••
        0x1F,0xF8,  //   •••••••••
        0x3F,0xFC,  //  •••••••••••
        0x3F,0xFC,  //  •••••••••••
        0x3F,0xFC,  //  •••••••••••
        0x1F,0xF8,  //   •••••••••
        0x0F,0xF0,  //    •••••••
        0x07,0xE0,  //     ••••••
        0x07,0xE0,  //     ••••••
        0x0F,0xF0,  //    •••••••
        0x0F,0xF0,  //    •••••••
        0x07,0xE0,  //     ••••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_SPARK - ⚡ 闪电
    [EMOJI_SPARK] = {
        0x00,0x80,  //        •
        0x01,0x00,  //       •
        0x02,0x00,  //      •
        0x04,0x00,  //     •
        0x08,0x00,  //    •
        0xFF,0xFE,  // ••••••••••••••
        0x01,0xC0,  //       •••
        0x01,0x00,  //       •
        0x02,0x00,  //      •
        0x04,0x00,  //     •
        0x08,0x00,  //    •
        0x10,0x00,  //   •
        0x20,0x00,  //  •
        0x40,0x00,  // •
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_MSG - 📨 信封
    [EMOJI_MSG] = {
        0x7F,0xF8,  // •••••••••••
        0xC0,0x0C,  // ••         ••
        0xE0,0x0C,  // •••        ••
        0xD0,0x0C,  // •• •       ••
        0xC8,0x0C,  // ••  •      ••
        0xC4,0x0C,  // ••   •     ••
        0xC2,0x0C,  // ••    •    ••
        0xC1,0x0C,  // ••     •   ••
        0x7F,0xF8,  // •••••••••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_CHAT - 💬 对话气泡
    [EMOJI_CHAT] = {
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x03,0xF0,  //              ••••••
        0x0F,0xFC,  //            •••••••••
        0x1C,0x1C,  //           •••     •••
        0x18,0x0C,  //           ••       ••
        0x18,0x0C,  //           ••       ••
        0x1C,0x1C,  //           •••     •••
        0x0F,0xFC,  //            •••••••••
        0x03,0xF0,  //              ••••••
        0x00,0xC0,  //               •••
        0x00,0x60,  //                ••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_SEND - 📤 发送
    [EMOJI_SEND] = {
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x08,  //                  •
        0x00,0x10,  //                 •
        0x00,0x20,  //                •
        0x00,0x40,  //               •
        0x1F,0xFF,  //      ••••••••••••••
        0x00,0x40,  //               •
        0x00,0x20,  //                •
        0x1F,0xFF,  //      ••••••••••••••
        0x00,0x20,  //                •
        0x00,0x10,  //                 •
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_SMILE - 😊 笑脸
    [EMOJI_SMILE] = {
        0x00,0x00,  // 
        0x01,0xC0,  //              •••
        0x03,0xE0,  //             •••••
        0x07,0xF0,  //            ••••••
        0x0E,0x38,  //           •••   •••
        0x1C,0x1C,  //          •••     •••
        0x18,0x8C,  //          ••   •   ••
        0x18,0x8C,  //          ••   •   ••
        0x1C,0x1C,  //          •••     •••
        0x0C,0x38,  //           ••    •••
        0x07,0xE0,  //            ••••••
        0x03,0xC0,  //             ••••
        0x01,0x80,  //              ••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_SLEEP - 😴 睡觉
    [EMOJI_SLEEP] = {
        0x00,0x00,  // 
        0x01,0xC0,  //              •••
        0x03,0xE0,  //             •••••
        0x07,0xF0,  //            ••••••
        0x0E,0x38,  //           •••   •••
        0x1C,0x1C,  //          •••     •••
        0x1D,0x9C,  //          ••• ••  •••
        0x1D,0x9C,  //          ••• ••  •••
        0x1C,0x1C,  //          •••     •••
        0x0C,0x38,  //           ••    •••
        0x07,0xE0,  //            ••••••
        0x03,0xC0,  //             ••••
        0x01,0x80,  //              ••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_THINK - 🤔 思考
    [EMOJI_THINK] = {
        0x00,0x00,  // 
        0x01,0xC0,  //              •••
        0x03,0xE0,  //             •••••
        0x06,0x30,  //            •••   ••
        0x0C,0x18,  //           ••     ••
        0x19,0x98,  //          ••  ••  ••
        0x19,0x98,  //          ••  ••  ••
        0x18,0x18,  //          ••     ••
        0x1C,0x38,  //          •••   •••
        0x0C,0x30,  //           ••   ••
        0x07,0xE0,  //            ••••••
        0x03,0xC0,  //             ••••
        0x00,0xC0,  //               •••
        0x00,0x80,  //                •
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_CLOCK - 🕐 时钟
    [EMOJI_CLOCK] = {
        0x00,0x00,  // 
        0x03,0xE0,  //             •••••
        0x07,0xF0,  //            ••••••
        0x0E,0x38,  //           •••   •••
        0x0C,0x18,  //           ••     ••
        0x0C,0xF8,  //           ••  •••••
        0x0C,0x38,  //           ••   •••
        0x0C,0x18,  //           ••     ••
        0x0C,0x38,  //           ••   •••
        0x0C,0x30,  //           ••    ••
        0x0E,0x38,  //           •••   •••
        0x07,0xF0,  //            ••••••
        0x03,0xE0,  //             •••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_SUN - ☀ 太阳
    [EMOJI_SUN] = {
        0x00,0x80,  //                •
        0x00,0x80,  //                •
        0x08,0x88,  //         •     •    •
        0x04,0x90,  //           •  •  •
        0x03,0xE0,  //              •••••
        0x07,0xF0,  //             ••••••
        0x0F,0xF8,  //            ••••••••
        0x07,0xF0,  //             ••••••
        0x03,0xE0,  //              •••••
        0x04,0x90,  //           •  •  •
        0x08,0x88,  //         •     •    •
        0x00,0x80,  //                •
        0x00,0x80,  //                •
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_MOON - 🌙 月亮
    [EMOJI_MOON] = {
        0x00,0x00,  // 
        0x03,0xE0,  //             •••••
        0x07,0xF0,  //            ••••••
        0x0C,0x38,  //           •••   •••
        0x18,0x18,  //          ••      ••
        0x18,0x08,  //          ••        •
        0x18,0x08,  //          ••        •
        0x18,0x08,  //          ••        •
        0x18,0x18,  //          ••      ••
        0x0C,0x10,  //           ••      •
        0x06,0x20,  //            ••    •
        0x03,0xC0,  //             ••••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_HOME - 🏠 首页
    [EMOJI_HOME] = {
        0x00,0x00,  // 
        0x00,0x80,  //                •
        0x01,0xC0,  //               •••
        0x03,0xE0,  //              •••••
        0x07,0xF0,  //             ••••••
        0x0F,0xF8,  //            ••••••••
        0x1F,0xFC,  //           •••••••••
        0x1E,0x3C,  //           ••••   ••••
        0x1C,0x1C,  //           •••     •••
        0x1C,0x1C,  //           •••     •••
        0x1C,0x1C,  //           •••     •••
        0x1C,0x1C,  //           •••     •••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_SETTINGS - ⚙ 设置
    [EMOJI_SETTINGS] = {
        0x00,0x00,  // 
        0x04,0x10,  //           •      •
        0x04,0x10,  //           •      •
        0x02,0x20,  //            •    •
        0x08,0x88,  //         •    •    •
        0x1F,0xF8,  //          •••••••••
        0x0E,0x70,  //            ••• •••
        0x04,0x20,  //             •  •
        0x0E,0x70,  //            ••• •••
        0x1F,0xF8,  //          •••••••••
        0x08,0x88,  //         •    •    •
        0x02,0x20,  //            •    •
        0x04,0x10,  //           •      •
        0x04,0x10,  //           •      •
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_SEARCH - 🔍 搜索
    [EMOJI_SEARCH] = {
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x01,0xC0,  //               •••
        0x03,0xE0,  //              •••••
        0x06,0x30,  //             •••   ••
        0x0C,0x18,  //            ••     ••
        0x0C,0x18,  //            ••     ••
        0x0C,0x18,  //            ••     ••
        0x06,0x30,  //             ••   ••
        0x03,0xE0,  //              •••••
        0x01,0xC0,  //               •••
        0x00,0x80,  //                •
        0x01,0x00,  //               •
        0x02,0x00,  //              •
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
    
    // EMOJI_RESTART - 🔄 重启
    [EMOJI_RESTART] = {
        0x00,0x00,  // 
        0x01,0xC0,  //               •••
        0x03,0xE0,  //              •••••
        0x07,0x00,  //             •••
        0x0C,0x00,  //            ••
        0x18,0x0C,  //           ••        ••
        0x10,0x04,  //           •          •
        0x00,0x1C,  //                •••
        0x00,0x38,  //               •••
        0x00,0x60,  //              ••
        0x01,0xC0,  //               •••
        0x03,0x80,  //              •••
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
        0x00,0x00,  // 
    },
};

// 绘制单个 emoji (16x16 像素, 每行 2 字节, 支持 scale 缩放)
void gui_draw_emoji(int16_t x, int16_t y, emoji_id_t emoji, uint8_t scale)
{
    if (emoji <= EMOJI_NONE || emoji >= EMOJI_COUNT) return;
    
    const uint8_t *bitmap = emoji_bitmaps[emoji];
    uint16_t color = emoji_colors[emoji];
    
    for (int row = 0; row < EMOJI_SIZE; row++) {
        // 读取 16 位行数据 (2 字节)
        uint16_t row_data = ((uint16_t)bitmap[row * 2] << 8) | bitmap[row * 2 + 1];
        for (int col = 0; col < EMOJI_SIZE; col++) {
            if (row_data & (0x8000 >> col)) {
                if (scale <= 1) {
                    fb_set_pixel(x + col, y + row, color);
                } else {
                    fb_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

// 前向声明
static void fb_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint8_t scale);

// 获取 emoji 宽度
int16_t gui_emoji_width(uint8_t scale)
{
    return EMOJI_SIZE * scale;
}

// 绘制带 emoji 的字符串
// 格式: "\x01\xNN" 其中 NN 是 emoji ID (十进制或十六进制)
void gui_draw_string_with_emoji(int16_t x, int16_t y, const char *str, 
                                 uint16_t color, uint8_t scale)
{
    if (!str) return;
    
    uint16_t ox = x;
    int char_w = 6 * scale;
    int emoji_w = EMOJI_SIZE * scale;
    int line_h = 8 * scale > EMOJI_SIZE * scale ? 8 * scale : EMOJI_SIZE * scale;
    
    while (*str) {
        if (*str == '\n') {
            x = ox;
            y += line_h + 2;
            str++;
        } else if (*str == 0x01 && *(str + 1) != '\0') {
            // Emoji 编码: \x01\xNN
            str++;
            emoji_id_t emoji = (emoji_id_t)(uint8_t)*str;
            gui_draw_emoji(x, y, emoji, scale);
            x += emoji_w + 2 * scale;  // emoji 后加一点间距
            str++;
        } else {
            fb_draw_char(x, y, *str, color, scale);
            x += char_w;
            str++;
        }
        
        if (x > SCREEN_WIDTH - char_w) {
            x = ox;
            y += line_h + 2;
        }
        if (y + line_h > SCREEN_HEIGHT) break;
    }
}

static void fb_draw_char(uint16_t x, uint16_t y, char c, uint16_t color, uint8_t scale)
{
    if (c < 32 || c > 122) c = '?';
    const uint8_t *glyph = font5x7[c - 32];

    for (int col = 0; col < 5; col++) {
        uint8_t bits = glyph[col];
        for (int row = 0; row < 7; row++) {
            if (bits & (1 << row)) {
                if (scale <= 1) {
                    fb_set_pixel(x + col, y + row, color);
                } else {
                    fb_fill_rect(x + col * scale, y + row * scale, scale, scale, color);
                }
            }
        }
    }
}

static void fb_draw_string(uint16_t x, uint16_t y, const char *str, uint16_t color, uint8_t scale)
{
    uint16_t ox = x;
    int char_w = 6 * scale;
    int line_h = 8 * scale;

    while (*str) {
        if (*str == '\n') {
            x = ox;
            y += line_h + 2;
        } else {
            fb_draw_char(x, y, *str, color, scale);
            x += char_w;
            if (x + char_w > SCREEN_WIDTH) {
                x = ox;
                y += line_h + 2;
            }
        }
        if (y + line_h > SCREEN_HEIGHT) break;
        str++;
    }
}

// ── 公开 API ─────────────────────────────────────────────────

// 初始化 GUI
esp_err_t simple_gui_init(void)
{
    ESP_LOGI(TAG, "Initializing simple GUI (framebuffer mode)...");
    
    // 初始化显示管理器
    esp_err_t ret = display_manager_init();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Failed to initialize display manager");
        return ret;
    }
    
    // 获取 SPI 句柄
    spi_handle = (spi_device_handle_t)display_manager_get_spi_handle();
    if (!spi_handle) {
        ESP_LOGE(TAG, "Failed to get SPI handle");
        return ESP_FAIL;
    }
    
    // 分配帧缓冲 - 优先使用 PSRAM
    s_fb = heap_caps_calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint16_t), MALLOC_CAP_SPIRAM);
    if (!s_fb) {
        ESP_LOGW(TAG, "PSRAM alloc failed, trying internal RAM...");
        s_fb = heap_caps_calloc(SCREEN_WIDTH * SCREEN_HEIGHT, sizeof(uint16_t), MALLOC_CAP_INTERNAL);
    }
    if (!s_fb) {
        ESP_LOGE(TAG, "Failed to alloc framebuffer (%d bytes)", SCREEN_WIDTH * SCREEN_HEIGHT * 2);
        return ESP_ERR_NO_MEM;
    }
    
    s_initialized = true;
    ESP_LOGI(TAG, "Simple GUI initialized (framebuffer=%dKB)", SCREEN_WIDTH * SCREEN_HEIGHT * 2 / 1024);
    return ESP_OK;
}

// SPI DMA 单次传输最大像素数（保守值，ESP32-S3 约为 32KB）
#define SPI_MAX_PIXELS_PER_TRANS  0x4000  // 16384 像素 = 32768 字节

/**
 * @brief 刷新帧缓冲到屏幕
 * 
 * 关键实现说明：
 * 
 * 1. SPI 时序必须与 display_manager_clear() 保持一致
 * 2. 像素数据分块大小 = AMOLED_WIDTH (536像素/行)
 * 3. 使用 spi_transaction_ext_t 结构支持变长命令/地址
 * 
 * 传输流程：
 *   a) 获取 SPI 锁
 *   b) 发送 0x2A 设置列地址 (CS: 低->传输->高)
 *   c) 发送 0x2B 设置行地址 (CS: 低->传输->高)
 *   d) 发送 0x2C + 像素数据 (CS: 低->传输...->高)
 *   e) 释放 SPI 锁
 * 
 * 注意事项：
 *   - 不要在此函数内部调用 qspi_send_cmd()，因为它会再次获取锁导致死锁
 *   - 第一个像素包包含 RAMWR 命令 (0x2C)，后续包只发送数据
 *   - 整个像素传输期间 CS 保持低电平
 */
void gui_flush(void)
{
    if (!s_fb || !s_initialized) return;
    
    // 获取 SPI 锁（整个刷新操作期间持有）
    display_manager_spi_lock();
    
    // 设置整个屏幕为显示区域（直接调用，不使用额外的函数）
    uint8_t ca[4] = {0, 0, (uint8_t)((SCREEN_WIDTH - 1) >> 8), (uint8_t)((SCREEN_WIDTH - 1) & 0xFF)};
    uint8_t ra[4] = {0, 0, (uint8_t)((SCREEN_HEIGHT - 1) >> 8), (uint8_t)((SCREEN_HEIGHT - 1) & 0xFF)};
    
    // 发送列地址设置命令
    spi_transaction_t t;
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = 0x2A << 8;
    t.tx_buffer = ca;
    t.length = 32;
    cs_low();
    spi_device_polling_transmit(spi_handle, &t);
    cs_high();
    
    // 发送行地址设置命令
    t.addr = 0x2B << 8;
    t.tx_buffer = ra;
    cs_low();
    spi_device_polling_transmit(spi_handle, &t);
    cs_high();
    
    // 发送像素数据（与 display_manager_clear 完全一致的实现）
    bool first_send = true;
    uint32_t total_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    uint16_t *p = s_fb;
    
    cs_low();  // 在循环开始前 CS 低
    while (total_pixels > 0) {
        uint32_t chunk = (total_pixels > AMOLED_WIDTH) ? AMOLED_WIDTH : total_pixels;
        
        spi_transaction_ext_t t_ext = {0};
        memset(&t_ext, 0, sizeof(t_ext));
        
        if (first_send) {
            t_ext.base.flags = SPI_TRANS_MODE_QIO;
            t_ext.base.cmd = 0x32;
            t_ext.base.addr = 0x002C00;
            first_send = false;
        } else {
            t_ext.base.flags = SPI_TRANS_MODE_QIO | SPI_TRANS_VARIABLE_CMD | SPI_TRANS_VARIABLE_ADDR | SPI_TRANS_VARIABLE_DUMMY;
            t_ext.command_bits = 0;
            t_ext.address_bits = 0;
            t_ext.dummy_bits = 0;
        }
        
        t_ext.base.tx_buffer = p;
        t_ext.base.length = chunk * 16;
        
        esp_err_t ret = spi_device_polling_transmit(spi_handle, (spi_transaction_t *)&t_ext);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to push %lu pixels", chunk);
            break;
        }
        
        total_pixels -= chunk;
        p += chunk;
    }
    cs_high();  // 循环结束后 CS 高
    
    // 释放 SPI 锁
    display_manager_spi_unlock();
}

// 刷新指定区域
void gui_flush_region(uint16_t x, uint16_t y, uint16_t w, uint16_t h)
{
    if (!s_fb || !s_initialized) return;
    
    // 边界检查
    if (x >= SCREEN_WIDTH || y >= SCREEN_HEIGHT) return;
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    
    // 获取 SPI 锁
    display_manager_spi_lock();
    
    // 设置显示区域（每个命令后 CS 会拉高）
    set_window(x, y, x + w - 1, y + h - 1);
    
    // 逐行传输（每行像素数远小于 SPI 限制，安全）
    bool first_send = true;
    cs_low();  // CS 拉低开始发送像素数据
    for (uint16_t row = y; row < y + h; row++) {
        push_pixels(&s_fb[row * SCREEN_WIDTH + x], w, first_send);
        first_send = false;
    }
    cs_high();  // CS 拉高结束
    
    // 释放 SPI 锁
    display_manager_spi_unlock();
}

// 清屏 - 优化版本：使用 32 位填充
void gui_clear_screen(uint16_t color)
{
    if (!s_fb) return;
    
    // 使用 32 位写入加速填充
    uint32_t color32 = ((uint32_t)color << 16) | color;
    uint32_t *fb32 = (uint32_t *)s_fb;
    uint32_t pixel_count = (SCREEN_WIDTH * SCREEN_HEIGHT) / 2;
    
    for (uint32_t i = 0; i < pixel_count; i++) {
        fb32[i] = color32;
    }
}

// 绘制像素点
void gui_draw_pixel(int16_t x, int16_t y, uint16_t color)
{
    if (x >= 0 && x < SCREEN_WIDTH && y >= 0 && y < SCREEN_HEIGHT) {
        fb_set_pixel(x, y, color);
    }
}

// 绘制水平线
void gui_draw_hline(int16_t x0, int16_t x1, int16_t y, uint16_t color)
{
    if (x0 > x1) { int16_t tmp = x0; x0 = x1; x1 = tmp; }
    if (y < 0 || y >= SCREEN_HEIGHT) return;
    if (x0 < 0) x0 = 0;
    if (x1 >= SCREEN_WIDTH) x1 = SCREEN_WIDTH - 1;
    fb_fill_rect(x0, y, x1 - x0 + 1, 1, color);
}

// 绘制垂直线
void gui_draw_vline(int16_t x, int16_t y0, int16_t y1, uint16_t color)
{
    if (y0 > y1) { int16_t tmp = y0; y0 = y1; y1 = tmp; }
    if (x < 0 || x >= SCREEN_WIDTH) return;
    if (y0 < 0) y0 = 0;
    if (y1 >= SCREEN_HEIGHT) y1 = SCREEN_HEIGHT - 1;
    fb_draw_vline(x, y0, y1, color);
}

// 绘制矩形
void gui_draw_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    gui_draw_hline(x, x + w - 1, y, color);
    gui_draw_hline(x, x + w - 1, y + h - 1, color);
    gui_draw_vline(x, y, y + h - 1, color);
    gui_draw_vline(x + w - 1, y, y + h - 1, color);
}

// 绘制填充矩形
void gui_fill_rect(int16_t x, int16_t y, int16_t w, int16_t h, uint16_t color)
{
    if (x < 0) { w += x; x = 0; }
    if (y < 0) { h += y; y = 0; }
    if (x + w > SCREEN_WIDTH) w = SCREEN_WIDTH - x;
    if (y + h > SCREEN_HEIGHT) h = SCREEN_HEIGHT - y;
    if (w <= 0 || h <= 0) return;
    
    fb_fill_rect(x, y, w, h, color);
}

// 绘制圆形
void gui_draw_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    int16_t f = 1 - r;
    int16_t ddF_x = 1;
    int16_t ddF_y = -2 * r;
    int16_t x = 0;
    int16_t y = r;
    
    fb_set_pixel(x0, y0 + r, color);
    fb_set_pixel(x0, y0 - r, color);
    fb_set_pixel(x0 + r, y0, color);
    fb_set_pixel(x0 - r, y0, color);
    
    while (x < y) {
        if (f >= 0) { y--; ddF_y += 2; f += ddF_y; }
        x++; ddF_x += 2; f += ddF_x;
        
        fb_set_pixel(x0 + x, y0 + y, color);
        fb_set_pixel(x0 - x, y0 + y, color);
        fb_set_pixel(x0 + x, y0 - y, color);
        fb_set_pixel(x0 - x, y0 - y, color);
        fb_set_pixel(x0 + y, y0 + x, color);
        fb_set_pixel(x0 - y, y0 + x, color);
        fb_set_pixel(x0 + y, y0 - x, color);
        fb_set_pixel(x0 - y, y0 - x, color);
    }
}

// 绘制填充圆形
void gui_fill_circle(int16_t x0, int16_t y0, int16_t r, uint16_t color)
{
    for (int16_t y = -r; y <= r; y++) {
        int16_t x = (int16_t)sqrt(r * r - y * y);
        gui_draw_hline(x0 - x, x0 + x, y0 + y, color);
    }
}

// 绘制字符
void gui_draw_char(int16_t x, int16_t y, char c, uint16_t color, font_size_t size)
{
    uint8_t scale = (size == FONT_8x8) ? 1 : ((size == FONT_16x16) ? 2 : 3);
    fb_draw_char(x, y, c, color, scale);
}

// 绘制字符串
void gui_draw_string(int16_t x, int16_t y, const char *str, uint16_t color, font_size_t size)
{
    if (!str) return;
    uint8_t scale = (size == FONT_8x8) ? 1 : ((size == FONT_16x16) ? 2 : 3);
    fb_draw_string(x, y, str, color, scale);
}

// 获取字符串宽度
int16_t gui_string_width(const char *str, font_size_t size)
{
    if (!str) return 0;
    int char_w = (size == FONT_8x8) ? 6 : ((size == FONT_16x16) ? 12 : 18);
    return strlen(str) * char_w;
}

// ============================================================================
// 页面 1: 启动画面 (gui_show_boot_screen)
// ============================================================================
void gui_show_boot_screen(void)
{
    // 清屏为黑色背景
    fb_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_BLACK);
    
    // 显示标题 "MimiClaw-AMOLED" - 白色，scale=6，居中
    fb_draw_string(50, 70, "MimiClaw-AmoLed", C_WHITE, 5);
    //fb_draw_string(100, 115, "AMOLED", C_CYAN, 4);
    
    // 显示副标题 "AI Assistant" - 绿色，scale=3，居中
    fb_draw_string(180, 130, "AI Assistant", C_GREEN, 3);
    
    // 版本号 - 白色，scale=2，右下角
    fb_draw_string(440, 200, "v1.0", C_WHITE, 2);
}

// ============================================================================
// 页面 2: 主控制器/状态栏 (gui_show_status_bar)
// ============================================================================
void gui_show_status_bar(const char *time_str, bool wifi_connected, bool telegram_connected, uint8_t battery_pct, const char *page_title)
{
    // 绘制状态栏背景（绿色，高度35px）
    fb_fill_rect(0, 0, SCREEN_WIDTH, 35, C_GREEN);
    
    // 显示时间 - 黑色，scale=2，左侧
    if (time_str) {
        fb_draw_string(12, 10, time_str, C_BLACK, 2);
    }
    
    // 显示页面标题 - 黑色，scale=3，居中
    if (page_title) {
        int16_t title_width = strlen(page_title) * 12;  // scale=2, char width=6*2
        int16_t title_x = (SCREEN_WIDTH - title_width) / 2;
        fb_draw_string(title_x, 7, page_title, C_BLACK, 3);
    }
    
    // 右侧状态信息位置
    int16_t right_x = SCREEN_WIDTH - 20;  // 右移字符
    
    // 显示电池电量 - 黑色文字
    if (battery_pct > 0) {
        char bat_str[8];
        snprintf(bat_str, sizeof(bat_str), "%d%%", battery_pct);
        int16_t bat_width = strlen(bat_str) * 12;  // scale=2, 每字符12像素
        right_x -= bat_width;
        fb_draw_string(right_x, 10, bat_str, C_BLACK, 2);
        right_x -= 10;  // 间距加大
    }
    
    // 显示 Telegram 状态 - 黑色文字
    if (telegram_connected) {
        right_x -= 24;  // "TG" 宽度 (2字符 * 12像素)
        fb_draw_string(right_x, 10, "TG", C_BLACK, 2);
        right_x -= 10;  // 间距加大
    }
    
    // 显示 WiFi 状态 - 黑色文字
    if (wifi_connected) {
        right_x -= 48;  // "WiFi" 宽度 (4字符 * 12像素)
        fb_draw_string(right_x, 10, "WiFi", C_BLACK, 2);
    }
    
    // 底部分隔线
    fb_draw_hline(0, SCREEN_WIDTH - 1, 32, rgb565(0, 100, 0));
}

// ============================================================================
// 页面 2b: Home页面（时间、日期、问候语）
// ============================================================================
void gui_show_home_page(const char *time_str, const char *date_str, const char *greeting, 
                         bool ai_busy, uint8_t battery_pct)
{
    char buf[32];
    
    // 大字体时间（居中）
    if (time_str && time_str[0]) {
        int16_t time_width = strlen(time_str) * 24;  // scale=3, 每字符约24像素
        int16_t time_x = (SCREEN_WIDTH - time_width) / 2;
        if (time_x < 10) time_x = 10;
        fb_draw_string(time_x, 50, time_str, C_WHITE, 3);
    } else {
        fb_draw_string(200, 50, "--:--", COLOR_GRAY, 3);
    }
    
    // 日期显示
    if (date_str && date_str[0]) {
        int16_t date_width = strlen(date_str) * 12;  // scale=2
        int16_t date_x = (SCREEN_WIDTH - date_width) / 2.5;
        fb_draw_string(date_x, 85, date_str, COLOR_CYAN, 3);
    }
    
    // 问候语
    if (greeting && greeting[0]) {
        int16_t greet_width = strlen(greeting) * 12;
        int16_t greet_x = (SCREEN_WIDTH - greet_width) / 2;
        fb_draw_string(greet_x, 125, greeting, COLOR_ORANGE, 2);
    }
    
    // AI状态指示
    const char *ai_status = ai_busy ? "AI: Working..." : "AI: Ready";
    uint16_t ai_color = ai_busy ? COLOR_YELLOW : COLOR_GREEN;
    int16_t ai_width = strlen(ai_status) * 12;
    int16_t ai_x = (SCREEN_WIDTH - ai_width) / 2;
    fb_draw_string(ai_x, 155, ai_status, ai_color, 2);
    
    // 电池电量条
    int16_t bar_x = 170;
    int16_t bar_y = 190;
    int16_t bar_width = 176;
    int16_t bar_height = 12;
    
    // 电池外框
    fb_draw_rect(bar_x, bar_y, bar_width, bar_height, COLOR_GRAY);
    fb_draw_rect(bar_x + bar_width, bar_y + 2, 4, bar_height - 4, COLOR_GRAY);
    
    // 电量填充
    int16_t fill_width = (bar_width - 4) * battery_pct / 100;
    uint16_t bat_color = battery_pct > 50 ? COLOR_GREEN : 
                         (battery_pct > 20 ? COLOR_YELLOW : COLOR_RED);
    if (fill_width > 0) {
        fb_fill_rect(bar_x + 2, bar_y + 2, fill_width, bar_height - 4, bat_color);
    }
    
    // 电量百分比
    snprintf(buf, sizeof(buf), "%d%%", battery_pct);
    fb_draw_string(bar_x + bar_width + 10, bar_y - 2, buf, C_WHITE, 1);
    
    // 底部操作提示
    fb_draw_string(140, 215, "Press BOOT to navigate", COLOR_GRAY, 1);
}

// ============================================================================
// 页面 3: 系统信息 (gui_show_system_info)
// ============================================================================
void gui_show_system_info(const char *wifi_ip, const char *llm_provider, 
                           uint32_t free_heap, uint32_t free_psram,
                           uint8_t battery_pct, uint32_t uptime_sec)
{
    char line[64];
    int16_t y = 50;
    
    // WiFi 信息
    fb_draw_string(20, y, "WiFi:", C_CYAN, 2);
    if (wifi_ip && wifi_ip[0]) {
        snprintf(line, sizeof(line), "%s", wifi_ip);
        fb_draw_string(100, y, line, C_WHITE, 2);
    } else {
        fb_draw_string(100, y, "--", COLOR_GRAY, 2);
    }
    y += 28;
    
    // LLM 提供商
    fb_draw_string(20, y, "LLM:", C_CYAN, 2);
    if (llm_provider && llm_provider[0]) {
        snprintf(line, sizeof(line), "%s", llm_provider);
        fb_draw_string(100, y, line, C_WHITE, 2);
    } else {
        fb_draw_string(100, y, "--", COLOR_GRAY, 2);
    }
    y += 28;
    
    // 内存信息
    fb_draw_string(20, y, "RAM:", C_CYAN, 2);
    snprintf(line, sizeof(line), "%dKB", (int)(free_heap / 1024));
    fb_draw_string(100, y, line, C_WHITE, 2);
    y += 28;
    
    // PSRAM 信息
    fb_draw_string(20, y, "PSRAM:", C_CYAN, 2);
    snprintf(line, sizeof(line), "%dKB", (int)(free_psram / 1024));
    fb_draw_string(100, y, line, C_WHITE, 2);
    y += 28;
    
    // 电池电量
    fb_draw_string(20, y, "BAT:", C_CYAN, 2);
    snprintf(line, sizeof(line), "%d%%", battery_pct);
    fb_draw_string(100, y, line, C_WHITE, 2);
    y += 28;
    
    // 运行时间
    fb_draw_string(20, y, "UPTIME:", C_CYAN, 2);
    int hours = uptime_sec / 3600;
    int mins = (uptime_sec % 3600) / 60;
    int secs = uptime_sec % 60;
    snprintf(line, sizeof(line), "%d:%02d:%02d", hours, mins, secs);
    fb_draw_string(130, y, line, C_WHITE, 2);
}

// 显示单条系统消息（用于通知）
void gui_show_system_message(const char *msg)
{
    if (!msg) return;
    
    // 在屏幕中央显示消息
    int16_t msg_width = gui_string_width(msg, FONT_16x16);
    int16_t x = (SCREEN_WIDTH - msg_width) / 2;
    if (x < 20) x = 20;
    
    int16_t box_width = msg_width + 40;
    if (box_width > SCREEN_WIDTH - 40) box_width = SCREEN_WIDTH - 40;
    int16_t box_x = (SCREEN_WIDTH - box_width) / 2;
    
    // 绘制消息背景
    fb_fill_rect(box_x, 100, box_width, 36, rgb565(50, 50, 50));
    fb_draw_rect(box_x, 100, box_width, 36, rgb565(80, 80, 80));
    
    // 显示消息文字
    fb_draw_string(x, 107, msg, C_WHITE, 2);
}

// ============================================================================
// 页面 4: 消息气泡 (gui_show_message)
// ============================================================================
void gui_show_message(const char *sender, const char *msg, bool is_me, int16_t y_pos)
{
    // 边界检查：确保气泡在屏幕范围内
    if (y_pos < 40 || y_pos >= SCREEN_HEIGHT - 30) return;
    if (!sender) return;
    
    int16_t margin = 15;
    int16_t bubble_width = 280;  // 固定宽度，更优雅
    int16_t bubble_x = is_me ? (SCREEN_WIDTH - bubble_width - margin) : margin;
    
    // 计算气泡高度（2号字体每行约18像素）
    int16_t msg_lines = (msg ? (strlen(msg) / 25) + 1 : 1);
    int16_t bubble_height = 36 + msg_lines * 18;
    
    // 如果气泡超出屏幕底部，截断高度
    if (y_pos + bubble_height > SCREEN_HEIGHT - 20) {
        bubble_height = SCREEN_HEIGHT - 20 - y_pos;
        if (bubble_height < 30) return;  // 高度不足，不绘制
    }
    
    // 气泡背景色（区分发送者）
    uint16_t bg_color = is_me ? rgb565(0, 90, 60) : rgb565(40, 40, 45);
    uint16_t border_color = is_me ? rgb565(0, 150, 100) : rgb565(70, 70, 75);
    
    // 绘制气泡
    fb_fill_rect(bubble_x, y_pos, bubble_width, bubble_height, bg_color);
    fb_draw_rect(bubble_x, y_pos, bubble_width, bubble_height, border_color);
    
    // 发送者 - 青色，2号字体
    fb_draw_string(bubble_x + 10, y_pos + 6, sender, C_CYAN, 2);
    
    // 消息内容 - 白色，2号字体
    if (msg) {
        fb_draw_string(bubble_x + 10, y_pos + 28, msg, C_WHITE, 2);
    }
}

// ============================================================================
// 页面 5: 日志 (gui_show_log)
// ============================================================================
#define MAX_LOG_LINES  6   // 减少行数以适应大字体
static char s_log_lines[MAX_LOG_LINES][80];
static int s_log_count = 0;

void gui_add_log(const char *tag, const char *message)
{
    if (!tag || !message) return;
    
    // 移动已有日志行（滚动）
    if (s_log_count >= MAX_LOG_LINES) {
        for (int i = 0; i < MAX_LOG_LINES - 1; i++) {
            memmove(s_log_lines[i], s_log_lines[i + 1], sizeof(s_log_lines[0]));
        }
        s_log_count = MAX_LOG_LINES - 1;
    }
    
    // 添加新日志行，限制总长度防止溢出
    int tag_len = strlen(tag);
    int max_msg_len = sizeof(s_log_lines[0]) - tag_len - 4;  // 4 = "[" + "] " + null
    
    if (max_msg_len > 0) {
        snprintf(s_log_lines[s_log_count], sizeof(s_log_lines[0]), "[%s] %.*s", tag, max_msg_len, message);
    } else {
        snprintf(s_log_lines[s_log_count], sizeof(s_log_lines[0]), "[%.10s] %s", tag, message);
    }
    s_log_count++;
}

void gui_show_log(void)
{
    int16_t y = 50;
    
    // 分隔线
    fb_draw_hline(10, SCREEN_WIDTH - 10, 45, C_DGRAY);
    
    // 页面标题
    fb_draw_string(240, 50, "System Logs", COLOR_CYAN, 2);
    y = 80;
    
    // 显示日志行（scale=2 大字体）
    for (int i = 0; i < s_log_count && y < SCREEN_HEIGHT - 20; i++) {
        // 根据tag设置颜色
        uint16_t color = C_WHITE;
        if (strstr(s_log_lines[i], "[ERR]") || strstr(s_log_lines[i], "[error]")) {
            color = COLOR_RED;
        } else if (strstr(s_log_lines[i], "[WARN]") || strstr(s_log_lines[i], "[warning]")) {
            color = COLOR_YELLOW;
        } else if (strstr(s_log_lines[i], "[SYS]")) {
            color = COLOR_CYAN;
        }
        
        fb_draw_string(15, y, s_log_lines[i], color, 2);
        y += 24;  // scale=2, 行高24像素
    }
    
    // 无日志提示
    if (s_log_count == 0) {
        fb_draw_string(180, 120, "(no logs)", C_DGRAY, 2);
    }
    
    // 底部提示
    fb_draw_string(150, 218, "Logs auto-scroll", COLOR_GRAY, 1);
}

void gui_clear_log(void)
{
    s_log_count = 0;
}

// ============================================================================
// 快捷菜单系统
// ============================================================================

void gui_show_quick_menu(const menu_item_t *items, int count, int selected)
{
    if (!items || count <= 0) return;
    
    // 菜单区域 - 屏幕中央
    int16_t menu_w = 320;
    int16_t menu_h = count * 44 + 70;  // 每项44像素 + 标题区 (适应 32x32 emoji)
    int16_t menu_x = (SCREEN_WIDTH - menu_w) / 2;
    int16_t menu_y = (SCREEN_HEIGHT - menu_h) / 2;
    if (menu_y < 40) menu_y = 45;
    
    // 绘制菜单背景
    fb_fill_rect(menu_x - 5, menu_y - 5, menu_w + 10, menu_h + 10, C_DGRAY);
    fb_fill_rect(menu_x, menu_y, menu_w, menu_h, rgb565(30, 30, 35));
    fb_draw_rect(menu_x, menu_y, menu_w, menu_h, rgb565(80, 80, 85));
    
    // 标题
    fb_draw_string(menu_x + 15, menu_y + 10, "Quick Menu", C_CYAN, 2);
    fb_draw_hline(menu_x + 10, menu_x + menu_w - 10, menu_y + 38, C_DGRAY);
    
    // 菜单项
    int16_t item_y = menu_y + 50;
    for (int i = 0; i < count && item_y < menu_y + menu_h - 20; i++) {
        uint16_t text_color = (i == selected) ? C_WHITE : C_LGRAY;
        
        // 选中项高亮背景
        if (i == selected) {
            fb_fill_rect(menu_x + 8, item_y - 4, menu_w - 16, 40, rgb565(50, 50, 55));
        }
        
        // 根据 label 选择 emoji
        emoji_id_t emoji = EMOJI_NONE;
        if (strstr(items[i].label, "Ask") || strstr(items[i].label, "AI")) {
            emoji = EMOJI_ROBOT;
        } else if (strstr(items[i].label, "System") || strstr(items[i].label, "Info")) {
            emoji = EMOJI_INFO;
        } else if (strstr(items[i].label, "Clear") || strstr(items[i].label, "Log")) {
            emoji = EMOJI_CHAT;
        } else if (strstr(items[i].label, "Restart") || strstr(items[i].label, "Reset")) {
            emoji = EMOJI_RESTART;
        } else if (strstr(items[i].label, "Search")) {
            emoji = EMOJI_SEARCH;
        } else if (strstr(items[i].label, "Settings") || strstr(items[i].label, "Setting")) {
            emoji = EMOJI_SETTINGS;
        } else if (strstr(items[i].label, "Home")) {
            emoji = EMOJI_HOME;
        } else if (items[i].icon && items[i].icon[0] == 0x01 && items[i].icon[1] != '\0') {
            // 支持 \x01\xNN 格式的 emoji ID
            emoji = (emoji_id_t)(uint8_t)items[i].icon[1];
        }
        
        // 绘制 16x16 emoji 放大到 32x32
        int16_t text_x = menu_x + 20;
        if (emoji != EMOJI_NONE) {
            gui_draw_emoji(text_x, item_y, emoji, 2);  // 16x16 -> 32x32
            text_x += 38;  // emoji 后留出间距
        } else if (items[i].icon && items[i].icon[0]) {
            // 使用原始 ASCII 图标
            char icon_buf[4] = {items[i].icon[0], ' ', '\0'};
            fb_draw_string(text_x, item_y + 4, icon_buf, text_color, 2);
            text_x += 16;
        }
        
        // 绘制菜单项文字 (与 emoji 垂直居中对齐)
        fb_draw_string(text_x, item_y + 8, items[i].label, text_color, 2);
        item_y += 44;
    }
    
    // 底部提示
    fb_draw_string(menu_x + 20, menu_y + menu_h - 20, "BOOT:Next  Long:Select", C_DGRAY, 1);
}

// ============================================================================
// AOD 常亮显示模式
// ============================================================================

static struct {
    bool enabled;
    int16_t offset_x;
    int16_t offset_y;
    uint32_t last_shift;
} s_aod_state = {0};

void gui_aod_enable(void)
{
    s_aod_state.enabled = true;
    s_aod_state.offset_x = 0;
    s_aod_state.offset_y = 0;
    s_aod_state.last_shift = 0;
}

void gui_aod_disable(void)
{
    s_aod_state.enabled = false;
}

bool gui_aod_is_enabled(void)
{
    return s_aod_state.enabled;
}

void gui_aod_update(const char *time_str, uint8_t battery_pct, bool wifi_ok)
{
    if (!s_aod_state.enabled) return;
    
    // 清屏
    fb_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_BLACK);
    
    // 每60秒移动位置（防止烧屏）
    s_aod_state.offset_x = (s_aod_state.offset_x + 5) % 40 - 20;
    s_aod_state.offset_y = (s_aod_state.offset_y + 3) % 20 - 10;
    
    // 绘制时间（居中+偏移）
    int16_t time_x = SCREEN_WIDTH / 2 - 48 + s_aod_state.offset_x;
    int16_t time_y = SCREEN_HEIGHT / 2 - 20 + s_aod_state.offset_y;
    
    if (time_str && time_str[0]) {
        fb_draw_string(time_x, time_y, time_str, C_WHITE, 3);
    } else {
        fb_draw_string(time_x, time_y, "--:--", COLOR_GRAY, 3);
    }
    
    // 状态图标行
    int16_t status_y = time_y + 35;
    int16_t status_x = SCREEN_WIDTH / 2 - 40;
    
    // WiFi 图标
    if (wifi_ok) {
        fb_draw_string(status_x, status_y, "[W]", COLOR_GRAY, 1);
        status_x += 30;
    }
    
    // 电池图标
    char bat_str[8];
    snprintf(bat_str, sizeof(bat_str), "%d%%", battery_pct);
    fb_draw_string(status_x, status_y, bat_str, COLOR_GRAY, 1);
}

// ============================================================================
// 按钮反馈效果
// ============================================================================

void gui_button_press_feedback(void)
{
    // 边框闪烁效果
    fb_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_WHITE);
    gui_flush();
    
    // 短暂延迟
    vTaskDelay(pdMS_TO_TICKS(30));
    
    // 恢复
    fb_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_BLACK);
    gui_flush();
}

void gui_action_confirm_flash(void)
{
    // 快速闪烁确认
    for (int i = 0; i < 2; i++) {
        fb_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_WHITE);
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(20));
        fb_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_BLACK);
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(20));
    }
}

// ============================================================================
// 动画效果实现
// ============================================================================

// AI 思考动画帧
bool gui_animate_ai_thinking(int frame)
{
    static const char dots[] = {'|', '/', '-', '\\'};
    char anim_char = dots[frame % 4];
    
    // 绘制 AI 思考指示
    char buf[24];
    snprintf(buf, sizeof(buf), "AI %c thinking...", anim_char);
    
    // 在状态栏下方显示
    fb_fill_rect(200, 100, 180, 30, C_BLACK);
    fb_draw_string(210, 105, buf, COLOR_YELLOW, 2);
    
    return true;  // 动画继续
}

// 启动画面动画
bool gui_animate_boot_screen(int frame)
{
    // 简单的进度条动画
    int16_t bar_x = 150;
    int16_t bar_y = 180;
    int16_t bar_w = 236;
    int16_t bar_h = 8;
    
    // 清除进度条区域
    fb_fill_rect(bar_x, bar_y, bar_w, bar_h, C_BLACK);
    fb_draw_rect(bar_x, bar_y, bar_w, bar_h, C_DGRAY);
    
    // 填充进度
    int16_t progress = (frame * 20) % bar_w;
    fb_fill_rect(bar_x + 2, bar_y + 2, progress - 4, bar_h - 4, C_CYAN);
    
    return true;
}

// 消息滑入动画
int16_t gui_animate_message_slide(int16_t target_y, int *anim_state)
{
    if (!anim_state) return -1;
    
    // 初始化动画状态
    if (*anim_state == 0) {
        *anim_state = SCREEN_WIDTH;  // 从右侧开始
    }
    
    // 每帧移动 20 像素
    *anim_state -= 20;
    
    if (*anim_state <= target_y) {
        *anim_state = 0;
        return -1;  // 动画结束
    }
    
    return *anim_state;
}

// ============================================================================
// 增强版页面显示
// ============================================================================

void gui_show_home_page_v2(const char *time_str, const char *date_str, 
                            const char *greeting, bool ai_busy, uint8_t battery_pct,
                            bool wifi_ok, bool tg_ok, int msg_count)
{
    char buf[32];
    (void)wifi_ok;    // 状态栏已显示
    (void)tg_ok;      // 状态栏已显示
    (void)time_str;   // 状态栏已显示
    
    // 状态栏已包含：时间、WiFi、TG、电池%，此处不再重复绘制
    
    int16_t y = 50;
    
    // ===== 日期显示 =====
    if (date_str && date_str[0]) {
        int16_t date_w = strlen(date_str) * 12;
        int16_t date_x = (SCREEN_WIDTH - date_w) / 2;
        fb_draw_string(date_x, y, date_str, COLOR_CYAN, 2);
    }
    
    // ===== 问候语区域 =====
    y += 30;
    if (greeting && greeting[0]) {
        int16_t greet_w = strlen(greeting) * 18;
        int16_t greet_x = (SCREEN_WIDTH - greet_w) / 2;
        fb_draw_string(greet_x, y, greeting, COLOR_ORANGE, 3);
    }
    
    // ===== AI 状态指示（使用 16x16 emoji 放大到 32x32）=====
    y += 45;
    int16_t ai_x = SCREEN_WIDTH / 2 - 90;
    gui_draw_emoji(ai_x, y, EMOJI_ROBOT, 2);  // 16x16 -> 32x32
    const char *ai_text = ai_busy ? "Working..." : "Ready";
    fb_draw_string(ai_x + 38, y + 8, ai_text, ai_busy ? COLOR_YELLOW : COLOR_GREEN, 2);
    
    // ===== 状态摘要卡片（使用 16x16 emoji 放大到 32x32）=====
    y += 45;
    int16_t card_w = SCREEN_WIDTH - 20;
    int16_t card_h = 48;  // 卡片高度适配 32x32 emoji + scale=2 文字
    int16_t card_x = 10;
    
    // 卡片背景
    fb_fill_rect(card_x, y, card_w, card_h, rgb565(25, 25, 30));
    fb_draw_rect(card_x, y, card_w, card_h, C_DGRAY);
    
    // 状态项（使用 32x32 emoji + scale=2 文字）
    int16_t item_x = card_x + 10;
    int16_t item_y = y + 8;  // emoji Y 位置
    
    // 消息计数 (32x32 emoji + 数字)
    gui_draw_emoji(item_x, item_y, EMOJI_MSG, 2);
    snprintf(buf, sizeof(buf), "%d", msg_count);
    fb_draw_string(item_x + 36, item_y + 10, buf, C_WHITE, 2);
    item_x += 125;
    
    // 内存状态
    gui_draw_emoji(item_x, item_y, EMOJI_BRAIN, 2);
    fb_draw_string(item_x + 36, item_y + 10, "OK", COLOR_GREEN, 2);
    item_x += 125;
    
    // AI 状态
    gui_draw_emoji(item_x, item_y, ai_busy ? EMOJI_SPARK : EMOJI_OK, 2);
    fb_draw_string(item_x + 36, item_y + 10, ai_busy ? "..." : "OK", 
                   ai_busy ? COLOR_YELLOW : COLOR_GREEN, 2);
    item_x += 125;
    
    // 电池状态
    gui_draw_emoji(item_x, item_y, 
                   battery_pct > 20 ? EMOJI_BATTERY : EMOJI_BATTERY_LOW, 2);
    snprintf(buf, sizeof(buf), "%d%%", battery_pct);
    fb_draw_string(item_x + 36, item_y + 10, buf, 
                   battery_pct > 20 ? COLOR_GREEN : COLOR_RED, 2);
    
    // ===== 快捷入口提示 =====
    y += card_h + 10;
    fb_draw_string((SCREEN_WIDTH - 180) / 2, y, "Long press for menu", C_DGRAY, 1);
    
    // ===== 底部操作提示 =====
    fb_draw_string(15, SCREEN_HEIGHT - 20, "BOOT:Next  2x:Ask AI  3x:Home", C_DGRAY, 1);
}

void gui_show_status_bar_v2(const char *time_str, bool wifi_ok, bool tg_ok, 
                              uint8_t battery_pct, const char *page_title)
{
    // 调用原有状态栏
    gui_show_status_bar(time_str, wifi_ok, tg_ok, battery_pct, page_title);
}

void gui_show_ai_status_area(bool busy, int anim_frame)
{
    // AI 状态区域（用于动画更新）
    int16_t area_x = 150;
    int16_t area_y = 100;
    int16_t area_w = 236;
    int16_t area_h = 40;
    
    // 清除区域
    fb_fill_rect(area_x, area_y, area_w, area_h, C_BLACK);
    
    // 绘制边框
    fb_draw_rect(area_x, area_y, area_w, area_h, C_DGRAY);
    
    // AI 状态文字
    const char *status = busy ? "AI Thinking..." : "AI Ready";
    uint16_t color = busy ? COLOR_YELLOW : COLOR_GREEN;
    
    int16_t text_w = strlen(status) * 12;
    fb_draw_string(area_x + (area_w - text_w) / 2, area_y + 12, status, color, 2);
    
    // 动画指示器
    if (busy && anim_frame >= 0) {
        static const char spinner[] = {'|', '/', '-', '\\'};
        char spin[2] = {spinner[anim_frame % 4], 0};
        fb_draw_string(area_x + area_w - 25, area_y + 12, spin, COLOR_YELLOW, 2);
    }
}

// ============================================================================
// 设置页面实现
// ============================================================================

void gui_show_settings_page(const setting_item_t *items, int count, int selected)
{
    if (!items || count <= 0) return;
    
    // 清屏
    fb_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, C_BLACK);
    
    // 标题
    fb_draw_string((SCREEN_WIDTH - 144) / 2, 15, "Settings", COLOR_CYAN, 3);
    fb_draw_hline(10, SCREEN_WIDTH - 10, 45, C_DGRAY);
    
    int16_t y = 55;
    
    // 显示设置项
    for (int i = 0; i < count && y < SCREEN_HEIGHT - 30; i++) {
        const setting_item_t *item = &items[i];
        bool is_selected = (i == selected);
        
        // 高亮选中项
        if (is_selected) {
            fb_fill_rect(10, y - 2, SCREEN_WIDTH - 20, 30, rgb565(30, 30, 40));
            fb_draw_rect(10, y - 2, SCREEN_WIDTH - 20, 30, COLOR_CYAN);
        }
        
        // 标签
        fb_draw_string(20, y, item->label, is_selected ? COLOR_WHITE : C_LGRAY, 2);
        
        // 根据类型显示值
        int16_t value_x = SCREEN_WIDTH - 150;
        char val_buf[32];
        
        switch (item->type) {
            case SETTING_TYPE_BOOL:
                snprintf(val_buf, sizeof(val_buf), item->value.bool_val ? "ON" : "OFF");
                fb_draw_string(value_x, y, val_buf, 
                              item->value.bool_val ? COLOR_GREEN : COLOR_RED, 2);
                break;
                
            case SETTING_TYPE_INT:
                snprintf(val_buf, sizeof(val_buf), "%d", item->value.int_val);
                fb_draw_string(value_x, y, val_buf, COLOR_YELLOW, 2);
                break;
                
            case SETTING_TYPE_ACTION:
                fb_draw_string(value_x, y, "[Action]", COLOR_ORANGE, 2);
                break;
        }
        
        y += 35;
    }
    
    // 底部操作提示
    fb_draw_string(15, SCREEN_HEIGHT - 20, 
                   "BOOT:Next  Double:Edit  Long:Back", C_DGRAY, 1);
    
    gui_flush();
}

// ============================================================================
// 页面切换动画实现
// ============================================================================

// 临时缓冲，用于保存旧页面的帧缓冲
static uint16_t *s_old_fb = NULL;

// 分配临时缓冲
static bool alloc_temp_buffer(void)
{
    if (s_old_fb) {
        return true;  // 已经分配过
    }
    
    size_t fb_size = SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t);
    s_old_fb = heap_caps_malloc(fb_size, MALLOC_CAP_SPIRAM);
    if (!s_old_fb) {
        ESP_LOGW(TAG, "Failed to allocate temp buffer in PSRAM, try internal RAM");
        s_old_fb = malloc(fb_size);
    }
    
    if (!s_old_fb) {
        ESP_LOGE(TAG, "Failed to allocate temp buffer for page transition");
        return false;
    }
    
    ESP_LOGI(TAG, "Temp buffer allocated for page transition");
    return true;
}

void gui_animate_page_transition(void (*old_page_render)(void), 
                                  void (*new_page_render)(void),
                                  anim_direction_t direction)
{
    if (!old_page_render || !new_page_render) {
        // 没有回调，直接显示新页面
        if (new_page_render) new_page_render();
        return;
    }
    
    // 分配临时缓冲
    if (!alloc_temp_buffer()) {
        // 分配失败，直接显示新页面
        if (new_page_render) new_page_render();
        return;
    }
    
    // 1. 渲染旧页面到当前帧缓冲
    old_page_render();
    
    // 2. 保存旧页面到临时缓冲
    memcpy(s_old_fb, s_fb, SCREEN_WIDTH * SCREEN_HEIGHT * sizeof(uint16_t));
    
    // 3. 渲染新页面到当前帧缓冲
    new_page_render();
    
    // 4. 执行滑动动画
    int16_t step = 20;  // 每步移动的像素数
    int16_t total_steps = SCREEN_WIDTH / step;
    
    for (int i = 1; i <= total_steps; i++) {
        int16_t offset = i * step;
        
        // 混合绘制两页内容
        for (int16_t y = 0; y < SCREEN_HEIGHT; y++) {
            for (int16_t x = 0; x < SCREEN_WIDTH; x++) {
                uint16_t color;
                
                switch (direction) {
                    case ANIM_DIR_LEFT:
                        if (x < offset) {
                            // 左半部分显示新页面的右半部分
                            color = s_fb[y * SCREEN_WIDTH + (x + SCREEN_WIDTH - offset)];
                        } else {
                            // 右半部分显示旧页面的左半部分
                            color = s_old_fb[y * SCREEN_WIDTH + (x - offset)];
                        }
                        break;
                        
                    case ANIM_DIR_RIGHT:
                        if (x < SCREEN_WIDTH - offset) {
                            // 左半部分显示旧页面的右半部分
                            color = s_old_fb[y * SCREEN_WIDTH + (x + offset)];
                        } else {
                            // 右半部分显示新页面的左半部分
                            color = s_fb[y * SCREEN_WIDTH + (x - (SCREEN_WIDTH - offset))];
                        }
                        break;
                        
                    default:
                        color = s_fb[y * SCREEN_WIDTH + x];
                        break;
                }
                
                fb_set_pixel(x, y, color);
            }
        }
        
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60fps
    }
    
    // 确保最后显示完整的新页面
    new_page_render();
}