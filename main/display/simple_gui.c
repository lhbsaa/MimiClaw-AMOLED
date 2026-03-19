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
#include <string.h>
#include <stdarg.h>
#include <math.h>

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

// 设置显示窗口
static void set_window(uint16_t x0, uint16_t y0, uint16_t x1, uint16_t y1)
{
    uint8_t ca[4] = {x0 >> 8, x0 & 0xFF, x1 >> 8, x1 & 0xFF};
    uint8_t ra[4] = {y0 >> 8, y0 & 0xFF, y1 >> 8, y1 & 0xFF};

    qspi_send_cmd(0x2A, ca, 4);  /* Column Address Set */
    qspi_send_cmd(0x2B, ra, 4);  /* Row Address Set */
    qspi_send_cmd(0x2C, NULL, 0); /* RAMWR */
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

// 刷新帧缓冲到屏幕
void gui_flush(void)
{
    if (!s_fb || !s_initialized) return;
    
    // 获取 SPI 锁（整个刷新操作期间持有）
    display_manager_spi_lock();
    
    // 设置整个屏幕为显示区域
    set_window(0, 0, SCREEN_WIDTH - 1, SCREEN_HEIGHT - 1);
    
    // 分块推送像素数据（受限于 SPI DMA 单次传输大小）
    bool first_send = true;
    uint32_t total_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    uint16_t *p = s_fb;
    
    cs_low();
    while (total_pixels > 0) {
        uint32_t chunk = (total_pixels > SPI_MAX_PIXELS_PER_TRANS) ? 
                          SPI_MAX_PIXELS_PER_TRANS : total_pixels;
        push_pixels(p, chunk, first_send);
        first_send = false;
        p += chunk;
        total_pixels -= chunk;
    }
    cs_high();
    
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
    
    set_window(x, y, x + w - 1, y + h - 1);
    
    // 逐行传输（每行像素数远小于 SPI 限制，安全）
    bool first_send = true;
    cs_low();
    for (uint16_t row = y; row < y + h; row++) {
        push_pixels(&s_fb[row * SCREEN_WIDTH + x], w, first_send);
        first_send = false;
    }
    cs_high();
    
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
        right_x -= 15;  // 间距加大
    }
    
    // 显示 Telegram 状态 - 黑色文字
    if (telegram_connected) {
        right_x -= 24;  // "TG" 宽度 (2字符 * 12像素)
        fb_draw_string(right_x, 10, "TG", C_BLACK, 2);
        right_x -= 15;  // 间距加大
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
// 页面 3: 系统消息 (gui_show_system_message)
// ============================================================================
void gui_show_system_message(const char *msg)
{
    if (!msg) return;
    
    // 计算居中位置
    int16_t msg_width = gui_string_width(msg, FONT_16x16);
    int16_t x = (SCREEN_WIDTH - msg_width) / 2;
    if (x < 20) x = 20;
    
    int16_t box_width = msg_width + 40;
    int16_t box_x = (SCREEN_WIDTH - box_width) / 2;
    
    // 绘制消息背景（深灰圆角感）
    fb_fill_rect(box_x, 95, box_width, 36, rgb565(50, 50, 50));
    fb_draw_rect(box_x, 95, box_width, 36, rgb565(80, 80, 80));
    
    // 显示消息文字
    fb_draw_string(x, 102, msg, C_WHITE, 2);
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
#define MAX_LOG_LINES  8
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
    // 格式: "[tag] message"，预留 4 字节给 "[] " 和 null终止符
    int tag_len = strlen(tag);
    int max_msg_len = sizeof(s_log_lines[0]) - tag_len - 5;  // 5 = "[] " + null
    
    if (max_msg_len > 0) {
        snprintf(s_log_lines[s_log_count], sizeof(s_log_lines[0]), "[%s] %.*s", tag, max_msg_len, message);
    } else {
        // tag 太长，截断
        snprintf(s_log_lines[s_log_count], sizeof(s_log_lines[0]), "[%.10s] %s", tag, message);
    }
    s_log_count++;
}

void gui_show_log(void)
{
    int16_t y = 45;
    
    // 分隔线
    fb_draw_hline(10, SCREEN_WIDTH - 10, y, C_DGRAY);
    y += 10;
    
    // 显示日志行
    for (int i = 0; i < s_log_count && y < SCREEN_HEIGHT - 25; i++) {
        fb_draw_string(15, y, s_log_lines[i], C_LGRAY, 1);
        y += 16;
    }
    
    // 无日志提示
    if (s_log_count == 0) {
        fb_draw_string(15, y, "(empty)", C_DGRAY, 1);
    }
}

void gui_clear_log(void)
{
    s_log_count = 0;
}