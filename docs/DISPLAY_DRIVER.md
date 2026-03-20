# Display Driver Documentation

This document provides technical details about the AMOLED display driver implementation.

## Overview

MimiClaw-AMOLED uses a frame buffer architecture with QSPI (Quad SPI) interface to drive the RM67162 AMOLED display.

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Application   │────▶│   Frame Buffer  │────▶│   QSPI Driver   │
│   (simple_gui)  │     │   (257KB PSRAM) │     │   (DMA)         │
└─────────────────┘     └─────────────────┘     └─────────────────┘
                                                        │
                                                        ▼
                                                ┌─────────────────┐
                                                │   RM67162       │
                                                │   AMOLED        │
                                                └─────────────────┘
```

## Frame Buffer

### Memory Layout

| Parameter | Value |
|-----------|-------|
| Resolution | 536 × 240 pixels |
| Color Format | RGB565 (16-bit) |
| Buffer Size | 257,280 bytes (~257KB) |
| Location | PSRAM (MALLOC_CAP_SPIRAM) |

### Pixel Format (RGB565)

```
Bit:  15 14 13 12 11 10 9  8  7  6  5  4  3  2  1  0
      │─── R (5-bit) ───│─── G (6-bit) ───│─── B (5-bit) ───│
```

**注意**: 帧缓冲中的像素数据需要交换高低字节，因为 SPI 传输顺序与显示期望顺序不同。

```c
// RGB565 颜色生成并交换字节 
static inline uint16_t rgb565(uint8_t r, uint8_t g, uint8_t b)
{
    uint16_t c = ((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3);
    return (c >> 8) | (c << 8);  // 交换高低字节
}
```

## QSPI Communication (关键实现)

### SPI 配置参数

| Parameter | Value |
|-----------|-------|
| Clock Speed | 40 MHz |
| Mode | SPI Mode 0 |
| Data Width | 4-bit (QSPI) |
| Max Transfer | 536 pixels per line (AMOLED_WIDTH) |

### 初始化序列

RM67162 需要特定的初始化序列，详见 `display_manager.c` 的 `send_init_sequence()` 函数。

关键命令：
- `0x11` - Sleep Out
- `0x3A` - Set Pixel Format (0x55 = 16-bit)
- `0x36` - Memory Access Control (rotation)
- `0x29` - Display ON

### 像素数据传输 (关键！)

**这是本次调试发现的核心问题所在！**

正确的 SPI 传输实现必须与 `display_manager_clear()` 保持一致：

```c
void gui_flush(void)
{
    // 1. 获取 SPI 锁
    display_manager_spi_lock();
    
    // 2. 设置显示窗口 (0x2A = 列地址, 0x2B = 行地址)
    uint8_t ca[4] = {0, 0, (SCREEN_WIDTH-1) >> 8, (SCREEN_WIDTH-1) & 0xFF};
    uint8_t ra[4] = {0, 0, (SCREEN_HEIGHT-1) >> 8, (SCREEN_HEIGHT-1) & 0xFF};
    
    // 发送 0x2A 命令 - CS 低->传输->CS高
    spi_transaction_t t;
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = 0x2A << 8;
    t.tx_buffer = ca;
    t.length = 32;
    cs_low();
    spi_device_polling_transmit(spi_handle, &t);
    cs_high();
    
    // 发送 0x2B 命令 - CS 低->传输->CS高
    t.addr = 0x2B << 8;
    t.tx_buffer = ra;
    cs_low();
    spi_device_polling_transmit(spi_handle, &t);
    cs_high();
    
    // 3. 发送像素数据 (0x2C = Memory Write)
    // 使用 spi_transaction_ext_t 结构，支持变长命令/地址
    bool first_send = true;
    uint32_t total_pixels = SCREEN_WIDTH * SCREEN_HEIGHT;
    uint16_t *p = s_fb;
    
    cs_low();  // 整个像素传输期间 CS 保持低
    while (total_pixels > 0) {
        uint32_t chunk = (total_pixels > AMOLED_WIDTH) ? AMOLED_WIDTH : total_pixels;
        
        spi_transaction_ext_t t_ext = {0};
        
        if (first_send) {
            // 第一个包：发送 RAMWR 命令 (0x2C)
            t_ext.base.flags = SPI_TRANS_MODE_QIO;
            t_ext.base.cmd = 0x32;      // 命令模式: 0x32 = 4线命令
            t_ext.base.addr = 0x002C00; // 地址包含命令 0x2C
            first_send = false;
        } else {
            // 后续包：继续发送像素数据，无需命令
            t_ext.base.flags = SPI_TRANS_MODE_QIO | 
                               SPI_TRANS_VARIABLE_CMD | 
                               SPI_TRANS_VARIABLE_ADDR | 
                               SPI_TRANS_VARIABLE_DUMMY;
            t_ext.command_bits = 0;
            t_ext.address_bits = 0;
            t_ext.dummy_bits = 0;
        }
        
        t_ext.base.tx_buffer = p;
        t_ext.base.length = chunk * 16;  // 每像素16位
        
        spi_device_polling_transmit(spi_handle, (spi_transaction_t *)&t_ext);
        
        total_pixels -= chunk;
        p += chunk;
    }
    cs_high();  // 像素传输结束，CS 拉高
    
    // 4. 释放 SPI 锁
    display_manager_spi_unlock();
}
```

### 为什么之前不工作？

**错误实现**：
- 使用了错误的分块大小 (`SPI_MAX_PIXELS_PER_TRANS = 16384`)
- 没有正确设置 `spi_transaction_ext_t` 结构

**正确实现**：
- 分块大小 = `AMOLED_WIDTH` (536 像素，即一行)
- 必须使用 `spi_transaction_ext_t` 并正确配置 flags

## Thread Safety

### SPI Mutex

SPI 总线是共享资源，必须使用互斥锁保护：

```c
// 获取锁
void display_manager_spi_lock(void) {
    if (spi_mutex) xSemaphoreTake(spi_mutex, portMAX_DELAY);
}

// 释放锁
void display_manager_spi_unlock(void) {
    if (spi_mutex) xSemaphoreGive(spi_mutex);
}
```

**重要**：在持有锁期间，不能再调用会获取锁的函数，否则会死锁！

例如：`gui_flush()` 已获取锁，内部调用的函数必须直接操作 SPI，不能再调用 `qspi_send_cmd()`（它会再次获取锁）。

## 页面切换机制

### 问题：定时器上下文限制

**错误做法**：在 ESP Timer 回调中直接执行 `ui_show_page()` 和 `gui_flush()`

```c
// 错误！定时器任务栈空间有限，SPI 操作可能失败
static void splash_timer_callback(void *arg) {
    ui_show_page(PAGE_STATUS_BAR);  // 可能导致问题
}
```

**正确做法**：定时器只设置标志，任务上下文执行实际操作

```c
// 定时器回调：只设置标志
static void splash_timer_callback(void *arg) {
    if (s_current_page == PAGE_BOOT) {
        s_splash_done = true;  // 设置标志
    }
}

// 任务上下文：检测标志并执行操作
static void status_update_task(void *arg) {
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        
        if (s_splash_done && s_current_page == PAGE_BOOT) {
            ui_show_page(PAGE_STATUS_BAR);  // 安全执行
        }
    }
}
```

### 为什么？

- ESP Timer 任务栈空间有限（默认 4KB）
- SPI 操作和 `gui_flush()` 需要更多栈空间
- FreeRTOS 任务栈可配置（如 4096 字节）

## Graphics Primitives

### 绘图函数

| Function | Description |
|----------|-------------|
| `gui_clear_screen(color)` | 清屏 |
| `gui_draw_pixel(x, y, color)` | 画点 |
| `gui_draw_hline(x1, x2, y, color)` | 画水平线 |
| `gui_fill_rect(x, y, w, h, color)` | 画填充矩形 |
| `gui_draw_string(x, y, str, color, scale)` | 画字符串 |

### 字体缩放

| Scale | Character Size |
|-------|----------------|
| 1 | 6 × 8 pixels |
| 2 | 12 × 16 pixels |
| 3 | 18 × 24 pixels |
| 4 | 24 × 32 pixels |

## Power Management

### Sleep Mode

```c
void display_manager_sleep(void) {
    // 发送 Sleep In 命令 (0x10)
    qspi_send_cmd(0x10, NULL, 0);
}

void display_manager_wakeup(void) {
    // 发送 Sleep Out 命令 (0x11)
    qspi_send_cmd(0x11, NULL, 0);
    vTaskDelay(pdMS_TO_TICKS(120));  // 等待显示唤醒
    qspi_send_cmd(0x29, NULL, 0);    // Display ON
}
```

## 常见问题与解决方案

| 问题 | 原因 | 解决方案 |
|------|------|----------|
| 屏幕不更新 | SPI 传输结构配置错误 | 使用与 `display_manager_clear()` 一致的实现 |
| 页面切换无效 | 在定时器上下文执行 SPI 操作 | 使用标志+任务上下文方式 |
| 死锁 | 嵌套获取 SPI 锁 | 检查函数调用链，避免重复获取锁 |
| 屏幕花屏 | 字节序错误 | RGB565 数据需交换高低字节 |
| 显示撕裂 | 无垂直同步 | 考虑双缓冲 |

## File References

| File | Purpose |
|------|---------|
| `display_manager.c` | QSPI 驱动、显示初始化 |
| `display_manager.h` | 公共接口定义 |
| `simple_gui.c` | 帧缓冲、图形绘制、`gui_flush()` |
| `simple_gui.h` | GUI 公共接口 |
| `ui_main.c` | UI 控制器、页面切换逻辑 |

## Related Documentation

- [Hardware Reference](HARDWARE.md) - 引脚定义
- [RM67162 Datasheet](../LilyGo-AMOLED-Series-1.2.4/datasheet/RM67162%20DataSheet.pdf)