/**
 * @file health_monitor.h
 * @brief 系统健康监控 - 内存、温度、看门狗等
 *
 * 用于长期不间断稳定运行的健康监控模块
 */

#pragma once

#include "esp_err.h"
#include "driver/temperature_sensor.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * @brief 初始化健康监控
 *
 * 创建健康监控任务，定期检查系统状态
 * - 内存使用情况
 * - 芯片温度
 * - 任务状态
 *
 * @return ESP_OK 成功
 */
esp_err_t health_monitor_init(void);

/**
 * @brief 获取系统健康状态摘要
 * @param buf 输出缓冲区
 * @param buf_size 缓冲区大小
 */
void health_monitor_get_summary(char *buf, size_t buf_size);

/**
 * @brief 获取温度传感器句柄（供其他模块复用，避免重复 install）
 * @return 已初始化的句柄，或 NULL（尚未初始化）
 */
temperature_sensor_handle_t health_monitor_get_temp_sensor(void);

#ifdef __cplusplus
}
#endif
