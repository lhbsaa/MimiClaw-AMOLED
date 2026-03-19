#include "touch_cst816s.h"
#include "display_manager.h"
#include "driver/i2c.h"
#include "driver/gpio.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "touch_cst816s";

// I2C 配置
#define I2C_MASTER_NUM          I2C_NUM_0
#define I2C_MASTER_FREQ_HZ      400000
#define I2C_MASTER_TIMEOUT_MS   1000

// CST816S 寄存器
#define CST816S_REG_GESTURE_ID  0x01
#define CST816S_REG_FINGER_NUM  0x02
#define CST816S_REG_X_POS_H     0x03
#define CST816S_REG_X_POS_L     0x04
#define CST816S_REG_Y_POS_H     0x05
#define CST816S_REG_Y_POS_L     0x06
#define CST816S_REG_CHIP_ID     0xA7
#define CST816S_REG_FW_VERSION  0xA9
#define CST816S_REG_SLEEP_MODE  0xE5

// 触控数据
static int16_t last_x = 0;
static int16_t last_y = 0;
static bool is_pressed = false;
static bool is_initialized = false;

// 回调函数
static void (*touch_callback)(int16_t x, int16_t y, cst816s_event_t event) = NULL;

// I2C 写入
static esp_err_t i2c_write(uint8_t reg, const uint8_t *data, size_t len)
{
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (CST816S_ADDR << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    if (data && len > 0) {
        i2c_master_write(cmd, (uint8_t *)data, len, true);
    }
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

// I2C 读取
static esp_err_t i2c_read(uint8_t reg, uint8_t *data, size_t len)
{
    if (reg != 0xFF) {
        i2c_cmd_handle_t cmd = i2c_cmd_link_create();
        i2c_master_start(cmd);
        i2c_master_write_byte(cmd, (CST816S_ADDR << 1) | I2C_MASTER_WRITE, true);
        i2c_master_write_byte(cmd, reg, true);
        i2c_master_stop(cmd);
        esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
        i2c_cmd_link_delete(cmd);
        if (ret != ESP_OK) {
            return ret;
        }
    }
    
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (CST816S_ADDR << 1) | I2C_MASTER_READ, true);
    if (len > 1) {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, data + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_MASTER_NUM, cmd, pdMS_TO_TICKS(I2C_MASTER_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t touch_cst816s_init(void)
{
    if (is_initialized) {
        return ESP_OK;
    }
    
    ESP_LOGI(TAG, "Initializing CST816S touch controller...");
    
    // 配置 I2C
    i2c_config_t conf = {
        .mode = I2C_MODE_MASTER,
        .sda_io_num = CST816S_SDA_PIN,
        .scl_io_num = CST816S_SCL_PIN,
        .sda_pullup_en = GPIO_PULLUP_ENABLE,
        .scl_pullup_en = GPIO_PULLUP_ENABLE,
        .master.clk_speed = I2C_MASTER_FREQ_HZ,
    };
    
    esp_err_t ret = i2c_param_config(I2C_MASTER_NUM, &conf);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C param config failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    ret = i2c_driver_install(I2C_MASTER_NUM, conf.mode, 0, 0, 0);
    if (ret != ESP_OK) {
        ESP_LOGE(TAG, "I2C driver install failed: %s", esp_err_to_name(ret));
        return ret;
    }
    
    // 配置中断引脚
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << CST816S_IRQ_PIN),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE,
    };
    gpio_config(&io_conf);
    
    // 读取芯片 ID
    uint8_t chip_id = 0;
    ret = i2c_read(CST816S_REG_CHIP_ID, &chip_id, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CST816S Chip ID: 0x%02X", chip_id);
    } else {
        ESP_LOGW(TAG, "Failed to read chip ID: %s", esp_err_to_name(ret));
    }
    
    // 读取固件版本
    uint8_t fw_version = 0;
    ret = i2c_read(CST816S_REG_FW_VERSION, &fw_version, 1);
    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "CST816S FW Version: 0x%02X", fw_version);
    }
    
    is_initialized = true;
    ESP_LOGI(TAG, "CST816S initialized successfully");
    return ESP_OK;
}

esp_err_t touch_cst816s_deinit(void)
{
    if (!is_initialized) {
        return ESP_OK;
    }
    
    i2c_driver_delete(I2C_MASTER_NUM);
    is_initialized = false;
    
    ESP_LOGI(TAG, "CST816S deinitialized");
    return ESP_OK;
}

bool touch_cst816s_read(int16_t *x, int16_t *y, cst816s_event_t *event)
{
    if (!is_initialized) {
        return false;
    }
    
    // 读取触控数据
    uint8_t data[6];
    esp_err_t ret = i2c_read(CST816S_REG_FINGER_NUM, data, 6);
    if (ret != ESP_OK) {
        return false;
    }
    
    // 解析数据
    uint8_t finger_num = data[0] & 0x0F;
    uint8_t event_type = (data[0] >> 4) & 0x0F;
    
    if (finger_num == 0) {
        is_pressed = false;
        if (event) *event = CST816S_EVT_UP;
        return false;
    }
    
    // 计算坐标
    int16_t x_pos = ((data[1] & 0x0F) << 8) | data[2];
    int16_t y_pos = ((data[3] & 0x0F) << 8) | data[4];
    
    // 坐标转换（根据屏幕方向调整）
    // T-Display-S3 AMOLED: 240x536
    // 触控坐标可能需要映射
    last_x = x_pos;
    last_y = y_pos;
    is_pressed = true;
    
    if (x) *x = last_x;
    if (y) *y = last_y;
    if (event) {
        switch (event_type) {
            case 0: *event = CST816S_EVT_DOWN; break;
            case 1: *event = CST816S_EVT_UP; break;
            case 2: *event = CST816S_EVT_CONTACT; break;
            default: *event = CST816S_EVT_NONE; break;
        }
    }
    
    // 调用回调
    if (touch_callback && event) {
        touch_callback(last_x, last_y, *event);
    }
    
    return true;
}

bool touch_cst816s_is_pressed(void)
{
    if (!is_initialized) {
        return false;
    }
    
    // 检查中断引脚状态
    // CST816S 在触控时拉低 IRQ 引脚
    return gpio_get_level(CST816S_IRQ_PIN) == 0;
}

void touch_cst816s_set_callback(void (*callback)(int16_t x, int16_t y, cst816s_event_t event))
{
    touch_callback = callback;
}

void touch_cst816s_sleep(void)
{
    if (!is_initialized) return;
    
    // 进入休眠模式
    uint8_t sleep_cmd = 0x03;
    i2c_write(CST816S_REG_SLEEP_MODE, &sleep_cmd, 1);
    
    ESP_LOGI(TAG, "CST816S entered sleep mode");
}

void touch_cst816s_wakeup(void)
{
    if (!is_initialized) return;
    
    // 唤醒触控芯片
    // 通常通过发送任意命令或触摸屏幕唤醒
    uint8_t dummy;
    i2c_read(CST816S_REG_CHIP_ID, &dummy, 1);
    
    ESP_LOGI(TAG, "CST816S wakeup");
}

// 兼容层函数（供 LVGL 使用）
bool touch_read(int16_t *x, int16_t *y)
{
    cst816s_event_t event;
    return touch_cst816s_read(x, y, &event);
}
