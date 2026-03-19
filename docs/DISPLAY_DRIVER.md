# Display Driver Documentation

This document provides technical details about the AMOLED display driver implementation.

## Overview

MimiClaw-AMOLED uses a frame buffer architecture with 8080 parallel interface (4-bit data bus) to drive the RM67162 AMOLED display.

```
┌─────────────────┐     ┌─────────────────┐     ┌─────────────────┐
│   Application   │────▶│   Frame Buffer  │────▶│   8080 Driver   │
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

### Color Macros

```c
#define RGB565(r, g, b)  (((r) << 11) | ((g) << 5) | (b))

// Predefined colors
#define COLOR_WHITE   0xFFFF
#define COLOR_BLACK   0x0000
#define COLOR_RED     0xF800
#define COLOR_GREEN   0x07E0
#define COLOR_BLUE    0x001F
#define COLOR_CYAN    0x07FF
#define COLOR_YELLOW  0xFFE0
```

## QSPI Communication

### Timing Parameters

| Parameter | Value |
|-----------|-------|
| Clock Speed | 40 MHz |
| Mode | SPI Mode 0 |
| Data Width | 4-bit (QSPI) |
| Max Transfer | 16,384 pixels per transaction |

### Initialization Sequence

The RM67162 requires a specific initialization sequence:

```c
// Key commands
0x11  // Sleep out
0x3A  // Set color format (16-bit)
0x36  // Memory access control (rotation)
0x29  // Display on
```

Full sequence in `lcd_init_sequence.c`:

| Command | Parameters | Purpose |
|---------|------------|---------|
| 0x11 | - | Exit sleep mode |
| 0x3A | 0x55 | 16-bit color |
| 0x36 | 0x60 | Landscape mode |
| 0x2A | x1, x2 | Column address |
| 0x2B | y1, y2 | Row address |
| 0x29 | - | Display on |

### SPI Transfer Limits

ESP32-S3 SPI DMA has a maximum transfer size. Large frame buffers must be chunked:

```c
#define SPI_MAX_PIXELS_PER_TRANS  16384

// Chunked transfer
while (total_pixels > 0) {
    uint32_t chunk = (total_pixels > SPI_MAX_PIXELS_PER_TRANS) ? 
                      SPI_MAX_PIXELS_PER_TRANS : total_pixels;
    push_pixels(p, chunk, first_send);
    total_pixels -= chunk;
    first_send = false;
}
```

## Graphics Primitives

### Drawing Functions

| Function | Description |
|----------|-------------|
| `fb_clear(color)` | Clear frame buffer with color |
| `fb_draw_pixel(x, y, color)` | Draw single pixel |
| `fb_draw_hline(x1, x2, y, color)` | Draw horizontal line |
| `fb_draw_rect(x, y, w, h, color)` | Draw filled rectangle |
| `fb_draw_char(x, y, c, color, scale)` | Draw character |
| `fb_draw_string(x, y, str, color, scale)` | Draw string |

### Font Rendering

| Scale | Character Size |
|-------|----------------|
| 1 | 6 × 8 pixels |
| 2 | 12 × 16 pixels |
| 3 | 18 × 24 pixels |
| 4 | 24 × 32 pixels |

## Display Window

### Address Window Setup

Before writing pixels, set the display window:

```c
// Set column address (X range)
write_cmd(0x2A);
write_data(x_start >> 8);
write_data(x_start & 0xFF);
write_data(x_end >> 8);
write_data(x_end & 0xFF);

// Set row address (Y range)
write_cmd(0x2B);
write_data(y_start >> 8);
write_data(y_start & 0xFF);
write_data(y_end >> 8);
write_data(y_end & 0xFF);

// Write pixels
write_cmd(0x2C);  // Memory write
```

### Full Screen Update

```c
void gui_flush(void) {
    set_window(0, SCREEN_WIDTH - 1, 0, SCREEN_HEIGHT - 1);
    push_pixels(frame_buffer, SCREEN_WIDTH * SCREEN_HEIGHT);
}
```

## Thread Safety

### SPI Mutex

The SPI bus is shared and protected by a mutex:

```c
static SemaphoreHandle_t s_spi_mutex;

void spi_lock(void) {
    xSemaphoreTake(s_spi_mutex, portMAX_DELAY);
}

void spi_unlock(void) {
    xSemaphoreGive(s_spi_mutex);
}
```

### Usage Pattern

```c
spi_lock();
qspi_send_cmd(...);
qspi_send_data(...);
spi_unlock();
```

## Power Management

### Sleep Mode

```c
void display_manager_sleep(void) {
    qspi_send_cmd(0x10, NULL, 0);  // Enter sleep mode
    // Optional: reduce backlight or turn off
}

void display_manager_wakeup(void) {
    qspi_send_cmd(0x11, NULL, 0);  // Exit sleep mode
    vTaskDelay(pdMS_TO_TICKS(120)); // Wait for display
}
```

### Backlight Control

```c
// GPIO 38 - Backlight with PWM
ledc_set_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0, duty);
ledc_update_duty(LEDC_LOW_SPEED_MODE, LEDC_CHANNEL_0);
```

## Performance Optimization

### Memory Allocation

```c
// Frame buffer - use PSRAM (larger, slower)
s_fb = heap_caps_calloc(size, 1, MALLOC_CAP_SPIRAM);

// Line buffer - use DMA-capable memory (faster)
line_buf = heap_caps_malloc(width * 2, MALLOC_CAP_DMA);
```

### Transfer Optimization

1. **Chunk transfers** to avoid DMA overflow
2. **Use DMA memory** for line buffers
3. **Minimize window changes** by batching draws

## Common Issues

| Issue | Cause | Solution |
|-------|-------|----------|
| Blank screen | Wrong initialization | Check init sequence |
| Garbled display | Timing issues | Reduce SPI clock |
| Transfer error | Buffer too large | Reduce chunk size |
| Tearing | No vsync | Consider double buffering |

## Touch Controller

> **Note**: T-Display-S3 AMOLED v1.0 does **not** include touch functionality. The CST816S driver code (`touch_cst816s.c`) is included for future v1.1 compatibility but is not used.

## File References

| File | Purpose |
|------|---------|
| `display_manager.c` | QSPI driver, initialization |
| `lcd_init_sequence.c` | RM67162 init commands |
| `simple_gui.c` | Frame buffer, graphics |
| `ui_main.c` | UI controller |

## Related Documentation

- [Hardware Reference](HARDWARE.md) - Pin definitions
- [RM67162 Datasheet](../LilyGo-AMOLED-Series-1.2.4/datasheet/RM67162%20DataSheet.pdf)
