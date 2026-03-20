# MimiClaw-AMOLED 硬件配置说明

## 硬件版本

| 版本 | 说明 |
|------|------|
| v1.0 | 基础版本，无触摸功能 |
| v1.1+ | 带触摸功能 (CST816S) |

本文档主要针对 v1.0 版本。

---

## 核心芯片

- **MCU**: ESP32-S3-WROOM-1
- **Flash**: 16MB QIO
- **PSRAM**: 8MB Octal (GPIO 26-37)

---

## 显示屏

| 参数 | 值 |
|------|-----|
| 类型 | AMOLED |
| 分辨率 | 536 x 240 |
| 驱动IC | RM67162 |
| 接口 | QSPI (4线SPI) |
| 颜色深度 | RGB565 (16-bit) |

### 显示引脚

| GPIO | 功能 | 说明 |
|------|------|------|
| 5 | QSPI D3 | 数据线3 |
| 6 | CS | 片选 |
| 7 | QSPI D1 | 数据线1 |
| 8 | SDO | MISO (可选) |
| 9 | TE | 帧同步 |
| 17 | RST | 复位 |
| 18 | QSPI D0 | 数据线0 |
| 47 | SCK | 时钟 |
| 48 | QSPI D2 | 数据线2 |

---

## 电源管理

### 电池

| 参数 | 值 |
|------|-----|
| ADC引脚 | GPIO 4 |
| 分压比 | 2:1 |
| 空电电压 | 3300 mV (0%) |
| 满电电压 | 4200 mV (100%) |
| 充电阈值 | >4250 mV |

### 电源控制

| GPIO | 功能 |
|------|------|
| 38 | LED + LCD电源控制 |

---

## 用户输入

| GPIO | 功能 | 触发方式 |
|------|------|----------|
| 0 | Boot按钮 | 低电平有效 |

### 按键事件

| 事件 | 触发条件 |
|------|----------|
| 单击 | 按下 < 500ms |
| 双击 | 两次单击间隔 < 300ms |
| 长按 | 按下 > 700ms |
| 超长按 | 按下 > 3000ms |

---

## 可用GPIO引脚 (v1.0)

v1.0 版本无触摸芯片，以下引脚可供用户扩展使用：

| GPIO | 说明 | 备注 |
|------|------|------|
| 1 | 通用IO | Strapping pin，启动时保持高电平 |
| 2 | 通用IO | 触摸I2C焊盘 (v1.0未使用) |
| 3 | 通用IO | 触摸I2C焊盘 (v1.0未使用) |
| 10 | 通用IO | - |
| 11 | 通用IO | - |
| 12 | 通用IO | - |
| 13 | 通用IO | - |
| 14 | 通用IO | - |
| 15 | 通用IO | - |
| 16 | 通用IO | - |
| 21 | 通用IO | 触摸IRQ焊盘 (v1.0未使用) |
| 46 | 通用IO | - |

**共计 12 个可用引脚**

---

## 禁用引脚

以下引脚已被占用，不可用于GPIO工具：

| GPIO | 用途 |
|------|------|
| 0 | Boot按钮 |
| 4 | 电池ADC |
| 5-9 | LCD QSPI |
| 17-18 | LCD控制 |
| 19-20 | USB Serial/JTAG (内部) |
| 26-37 | Octal PSRAM |
| 38 | LED/LCD电源 |
| 47-48 | LCD QSPI |

---

## 触摸控制器 (v1.1+)

v1.0 版本未焊接触摸芯片。v1.1+ 版本使用 CST816S：

| GPIO | 功能 |
|------|------|
| 2 | I2C SCL |
| 3 | I2C SDA |
| 21 | 触摸中断 |

**注意**: 使用触摸功能的版本，GPIO 2、3、21 不可用于其他用途。

---

## USB接口

- **类型**: USB-C
- **控制台**: USB Serial/JTAG (GPIO 19/20)
- **波特率**: 默认 115200

---

## LLM工具调用示例

### 读取电池状态

```json
{"name": "battery_status", "input": {}}
// 返回: "Battery: 3950mV (59%), discharging"
```

### 控制LED

```json
{"name": "led_control", "input": {"state": "toggle"}}
// 返回: "LED is now ON (GPIO38)"
```

### 读取芯片温度

```json
{"name": "chip_temperature", "input": {}}
// 返回: "Chip temperature: 45.2°C"
```

### GPIO操作

```json
// 设置GPIO 10 为高电平
{"name": "gpio_write", "input": {"pin": 10, "state": 1}}

// 读取GPIO 10 状态
{"name": "gpio_read", "input": {"pin": 10}}

// 读取所有可用GPIO
{"name": "gpio_read_all", "input": {}}
```

---

## 电气参数

| 参数 | 最小 | 典型 | 最大 |
|------|------|------|------|
| 工作电压 | 3.0V | 3.3V | 3.6V |
| 工作温度 | -10°C | 25°C | 60°C |
| GPIO输出电流 | - | - | 40mA |

---

## 参考文档

- [ESP32-S3 技术手册](https://www.espressif.com/sites/default/files/documentation/esp32-s3_technical_reference_manual_en.pdf)
- [RM67162 数据手册](../../LilyGo-AMOLED-Series-1.2.4/datasheet/RM67162%20DataSheet.pdf)
- [CST816S 寄存器手册](../../LilyGo-AMOLED-Series-1.2.4/datasheet/CST816_Register.pdf)
