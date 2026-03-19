#include "display_manager.h"
#include "lcd_init_sequence.h"
#include "esp_log.h"
#include "driver/spi_master.h"
#include "driver/gpio.h"
#include "esp_heap_caps.h"
#include "freertos/semphr.h"
#include <string.h>
#include <stddef.h>
#include <inttypes.h>

static const char *TAG = "display_mgr";

// SPI 互斥锁 - 防止多线程并发访问
static SemaphoreHandle_t spi_mutex = NULL;

// 电源控制 - T-Display-S3 AMOLED 需要控制电源引脚
static void power_enable(bool on)
{
    gpio_reset_pin(AMOLED_PIN_POWER);
    gpio_set_direction(AMOLED_PIN_POWER, GPIO_MODE_OUTPUT);
    gpio_set_level(AMOLED_PIN_POWER, on ? 1 : 0);
}

spi_device_handle_t spi_handle = NULL;
static bool is_initialized = false;
static bool is_sleeping = false;  // 屏幕休眠状态

// 安全的 SPI 传输（带互斥锁保护）
static esp_err_t spi_transmit_safe(spi_transaction_t *t)
{
    if (spi_mutex) xSemaphoreTake(spi_mutex, portMAX_DELAY);
    esp_err_t ret = spi_device_polling_transmit(spi_handle, t);
    if (spi_mutex) xSemaphoreGive(spi_mutex);
    return ret;
}

// 手动控制 CS
static inline void cs_high(void)
{
    gpio_set_level(AMOLED_PIN_CS, 1);
}

static inline void cs_low(void)
{
    gpio_set_level(AMOLED_PIN_CS, 0);
}

// QSPI 发送命令（内部版本，不获取锁）
static esp_err_t qspi_send_cmd_no_lock(uint8_t cmd, const uint8_t *data, uint8_t len)
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

// QSPI 发送命令 (与 Mimiclaw-bak 一致)
static esp_err_t qspi_send_cmd(uint8_t cmd, const uint8_t *data, uint8_t len)
{
    if (spi_mutex) xSemaphoreTake(spi_mutex, portMAX_DELAY);
    esp_err_t ret = qspi_send_cmd_no_lock(cmd, data, len);
    if (spi_mutex) xSemaphoreGive(spi_mutex);
    return ret;
}

// 发送初始化序列
static esp_err_t send_init_sequence(void)
{
    ESP_LOGI(TAG, "Sending RM67162 init sequence...");
    
    // 与 Mimiclaw-bak 一致：初始化 3 次防止初始化失败
    int repeat = 3;
    esp_err_t ret = ESP_OK;
    while (repeat--) {
        for (size_t i = 0; i < RM67162_INIT_SEQUENCE_LENGTH; i++) {
            const lcd_cmd_t *cmd = &rm67162_cmd[i];
            
            uint8_t real_len = cmd->len & 0x7F;
            bool need_delay = (cmd->len & 0x80) != 0;

            ret = qspi_send_cmd(cmd->addr, cmd->param, real_len);
            if (ret != ESP_OK) {
                ESP_LOGE(TAG, "Init cmd 0x%02" PRIX32 " failed: %s", cmd->addr, esp_err_to_name(ret));
                return ret;
            }

            if (need_delay) {
                vTaskDelay(pdMS_TO_TICKS(120));
            }
        }
    }
    
    // 设置横屏模式 MADCTL — 对应 lcd_setRotation(1)
    // 0x60 = TFT_MAD_MX(0x40) | TFT_MAD_MV(0x20) | TFT_MAD_RGB(0x00)
    uint8_t madctl = RM67162_MADCTL_LANDSCAPE;
    ret = qspi_send_cmd(0x36, &madctl, 1);
    
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "MADCTL set failed: %s", esp_err_to_name(ret));
        return ret;
    }
    ESP_LOGI(TAG, "MADCTL set to 0x%02X (landscape mode)", madctl);
    
    return ESP_OK;
}

esp_err_t display_manager_init(void)
{
    if (is_initialized) {
        ESP_LOGW(TAG, "Display already initialized");
        return ESP_OK;
    }
    
    // Create SPI mutex for thread-safe access
    spi_mutex = xSemaphoreCreateMutex();
    if (!spi_mutex) {
        ESP_LOGE(TAG, "Failed to create SPI mutex");
        return ESP_ERR_NO_MEM;
    }
    
    ESP_LOGI(TAG, "Initializing AMOLED display...");
    ESP_LOGI(TAG, "Resolution: %dx%d", AMOLED_WIDTH, AMOLED_HEIGHT);
    
    // 开启 AMOLED 电源
    power_enable(true);
    vTaskDelay(pdMS_TO_TICKS(50));
    
    // 配置 CS 为 GPIO 手动控制
    gpio_reset_pin(AMOLED_PIN_CS);
    gpio_set_direction(AMOLED_PIN_CS, GPIO_MODE_OUTPUT);
    cs_high();
    
    // 配置 SPI 总线
    spi_bus_config_t buscfg = {
        .data0_io_num = AMOLED_PIN_DATA0,
        .data1_io_num = AMOLED_PIN_DATA1,
        .sclk_io_num = AMOLED_PIN_SCK,
        .data2_io_num = AMOLED_PIN_DATA2,
        .data3_io_num = AMOLED_PIN_DATA3,
        .max_transfer_sz = (SEND_BUF_SIZE * 16) + 8,
        .flags = SPICOMMON_BUSFLAG_MASTER | SPICOMMON_BUSFLAG_GPIO_PINS,
    };
    
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI bus init failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 配置 SPI 设备 - 与 Mimiclaw-bak 完全一致
    spi_device_interface_config_t devcfg = {
        .command_bits = 8,
        .address_bits = 24,
        .dummy_bits = 0,
        .mode = 0,  // QSPI mode=0 (关键！mode=3 导致无法显示)
        .clock_speed_hz = 75000000,  // 75MHz
        .spics_io_num = -1,
        .flags = SPI_DEVICE_HALFDUPLEX,
        .queue_size = 17,
    };
    
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &spi_handle);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "SPI device add failed: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }
    
    // 配置 GPIO
    gpio_config_t rst_conf = {
        .pin_bit_mask = (1ULL << AMOLED_PIN_RST),
        .mode = GPIO_MODE_OUTPUT,
        .pull_up_en = GPIO_PULLUP_DISABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&rst_conf);
    
    // 复位显示屏
    gpio_set_level(AMOLED_PIN_RST, 0);
    vTaskDelay(pdMS_TO_TICKS(300));
    gpio_set_level(AMOLED_PIN_RST, 1);
    vTaskDelay(pdMS_TO_TICKS(200));
    
    // 发送初始化命令
    ret = send_init_sequence();
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "Init sequence failed: %s", esp_err_to_name(ret));
        spi_bus_free(SPI2_HOST);
        return ret;
    }
    
    // 清屏为白色
    display_manager_clear(0xFFFF);
    
    is_initialized = true;
    ESP_LOGI(TAG, "AMOLED display initialized successfully");
    return ESP_OK;
}

esp_err_t display_manager_deinit(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }
    
    // 关闭显示
    spi_transaction_t t = {0};
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = 0x28 << 8;
    
    cs_low();
    spi_transmit_safe(&t);
    cs_high();
    vTaskDelay(pdMS_TO_TICKS(10));
    
    t.addr = 0x10 << 8;
    cs_low();
    spi_transmit_safe(&t);
    cs_high();
    vTaskDelay(pdMS_TO_TICKS(120));
    
    // 关闭 AMOLED 电源
    power_enable(false);
    
    // 释放资源
    spi_bus_free(SPI2_HOST);
    
    is_initialized = false;
    ESP_LOGI(TAG, "Display deinitialized");
    return ESP_OK;
}

void display_manager_clear(uint16_t color)
{
    if (!is_initialized) return;
    
    // 分配行缓冲区
    uint16_t *line_buf = heap_caps_malloc(AMOLED_WIDTH * sizeof(uint16_t), MALLOC_CAP_DMA);
    if (!line_buf) {
        ESP_LOGE(TAG, "Failed to allocate line buffer");
        return;
    }
    
    // 填充颜色
    for (int i = 0; i < AMOLED_WIDTH; i++) {
        line_buf[i] = color;
    }
    
    // 获取 SPI 锁（整个清屏操作期间持有）
    if (spi_mutex) xSemaphoreTake(spi_mutex, portMAX_DELAY);
    
    // 设置列地址 (0x2A)
    uint8_t ca[4] = {0, 0, (uint8_t)((AMOLED_WIDTH - 1) >> 8), (uint8_t)((AMOLED_WIDTH - 1) & 0xFF)};
    qspi_send_cmd_no_lock(0x2A, ca, 4);
    
    // 设置行地址 (0x2B)
    uint8_t ra[4] = {0, 0, (uint8_t)((AMOLED_HEIGHT - 1) >> 8), (uint8_t)((AMOLED_HEIGHT - 1) & 0xFF)};
    qspi_send_cmd_no_lock(0x2B, ra, 4);
    
    // 发送像素数据 - 与 Mimiclaw-bak amoled_push_colors 一致
    bool first_send = true;
    size_t len = AMOLED_WIDTH * AMOLED_HEIGHT;
    uint16_t *p = line_buf;
    
    cs_low();  // 在循环开始前 CS 低
    while (len > 0) {
        size_t chunk_size = (len > AMOLED_WIDTH) ? AMOLED_WIDTH : len;
        
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
        t_ext.base.length = chunk_size * 16;
        
        esp_err_t ret = spi_device_polling_transmit(spi_handle, (spi_transaction_t *)&t_ext);
        if (ret != ESP_OK) {
            ESP_LOGE(TAG, "Failed to push %zu pixels", chunk_size);
            break;
        }
        
        len -= chunk_size;
        p += chunk_size;
    }
    cs_high();  // 循环结束后 CS 高
    
    // 释放 SPI 锁
    if (spi_mutex) xSemaphoreGive(spi_mutex);
    
    heap_caps_free(line_buf);
}

void display_manager_set_brightness(uint8_t brightness)
{
    if (!is_initialized) return;
    
    spi_transaction_t t = {0};
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = 0x51 << 8;
    t.tx_buffer = &brightness;
    t.length = 8;
    
    cs_low();
    spi_transmit_safe(&t);
    cs_high();
    
    ESP_LOGI(TAG, "Brightness set to %d", brightness);
}

void display_manager_sleep(void)
{
    if (!is_initialized || is_sleeping) return;
    
    spi_transaction_t t = {0};
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = 0x28 << 8;
    
    cs_low();
    spi_transmit_safe(&t);
    cs_high();
    vTaskDelay(pdMS_TO_TICKS(10));
    
    t.addr = 0x10 << 8;
    cs_low();
    spi_transmit_safe(&t);
    cs_high();
    vTaskDelay(pdMS_TO_TICKS(120));
    
    is_sleeping = true;
    ESP_LOGI(TAG, "Display sleep");
}

void display_manager_wakeup(void)
{
    if (!is_initialized) return;
    
    // 如果屏幕已经唤醒，直接返回
    if (!is_sleeping) return;
    
    spi_transaction_t t = {0};
    memset(&t, 0, sizeof(t));
    t.flags = SPI_TRANS_MULTILINE_CMD | SPI_TRANS_MULTILINE_ADDR;
    t.cmd = 0x02;
    t.addr = 0x11 << 8;
    
    cs_low();
    spi_transmit_safe(&t);
    cs_high();
    vTaskDelay(pdMS_TO_TICKS(120));
    
    t.addr = 0x29 << 8;
    cs_low();
    spi_transmit_safe(&t);
    cs_high();
    vTaskDelay(pdMS_TO_TICKS(20));
    
    is_sleeping = false;
    ESP_LOGI(TAG, "Display wakeup");
}

void display_manager_show_boot_screen(void)
{
    if (!is_initialized) return;
    
    // 清屏为黑色
    display_manager_clear(0x0000);
    
    ESP_LOGI(TAG, "Boot screen displayed");
}

void display_manager_show_wifi_status(bool connected)
{
    ESP_LOGI(TAG, "WiFi status: %s", connected ? "connected" : "disconnected");
    // TODO: 在屏幕上显示 WiFi 图标
}

void display_manager_show_telegram_status(bool connected)
{
    ESP_LOGI(TAG, "Telegram status: %s", connected ? "connected" : "disconnected");
    // TODO: 在屏幕上显示 Telegram 图标
}

void display_manager_show_system_message(const char *msg)
{
    ESP_LOGI(TAG, "System message: %s", msg);
    // TODO: 在屏幕上显示消息
}

void *display_manager_get_spi_handle(void)
{
    return spi_handle;
}

void display_manager_spi_lock(void)
{
    if (spi_mutex) xSemaphoreTake(spi_mutex, portMAX_DELAY);
}

void display_manager_spi_unlock(void)
{
    if (spi_mutex) xSemaphoreGive(spi_mutex);
}

void display_manager_get_panel_config(uint16_t *width, uint16_t *height)
{
    if (width) *width = AMOLED_WIDTH;
    if (height) *height = AMOLED_HEIGHT;
}
