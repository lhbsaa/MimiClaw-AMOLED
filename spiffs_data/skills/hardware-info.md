# Hardware Info

Hardware specifications and GPIO pin map for the T-Display-S3 AMOLED 1.91" V1.0 board running MimiClaw.

## When to use
When the user asks about hardware, sensors, pins, GPIO, peripherals, battery, display, or the development board.

## Board Overview
- Board: LILYGO T-Display-S3 AMOLED 1.91" **V1.0** (no touch)
- MCU: ESP32-S3 (Xtensa LX7 dual-core, 240MHz)
- Flash: 16MB
- PSRAM: 8MB (octal SPI)
- Display: 1.91" AMOLED, 536x240 pixels, RGB565, RM67162 controller (QSPI interface, 75MHz)
- Touch: **NOT available on V1.0** (V2.0+ has CST816T)
- Battery: Li-Po via JST connector, voltage read through 2:1 divider on GPIO4
- USB: Type-C, used for programming (JTAG) and serial console (115200 baud)
- Buttons: Boot button (GPIO0, active low)
- WiFi: 802.11 b/g/n
- Bluetooth: BLE 5.0

## GPIO Pin Map

### AMOLED Display (RM67162 QSPI)
| Function | GPIO | Notes |
|----------|------|-------|
| QSPI D0 (SDA) | 18 | Data line 0 |
| QSPI D1 | 7 | Data line 1 |
| QSPI D2 | 48 | Data line 2 |
| QSPI D3 | 5 | Data line 3 |
| QSPI SCK | 47 | SPI clock |
| QSPI CS | 6 | Chip select |
| RST | 17 | Display reset |
| TE | 9 | Tearing effect sync |
| POWER | 38 | Display power enable |

### Battery ADC
| Function | GPIO | Notes |
|----------|------|-------|
| BAT_ADC | 4 | 2:1 voltage divider, ADC1_CH3 |

Voltage mapping:
- 3300mV = 0% (empty)
- 4200mV = 100% (full)
- >4250mV = USB charging

### Boot Button
| Function | GPIO | Notes |
|----------|------|-------|
| BOOT | 0 | Active low, supports click/double-click/long-press |

### USB (built-in)
| Function | GPIO | Notes |
|----------|------|-------|
| USB D+ | 20 | USB data positive |
| USB D- | 19 | USB data negative |

## Built-in Sensors
1. **Temperature sensor** - ESP32-S3 internal die temperature (8-bit Sigma-Delta ADC)
2. **Battery voltage** - via ADC on GPIO4, read with get_battery tool

## Available GPIO (free for user projects)
Since V1.0 has no touch controller, the following GPIOs are freed:
- GPIO 2: available (was touch SCL on V2.0)
- GPIO 3: available (was touch SDA on V2.0)
- GPIO 21: available (was touch IRQ on V2.0)

## Pins NOT Available for General Use
These GPIOs are occupied by onboard hardware:
- GPIO 0: Boot button
- GPIO 4: Battery ADC
- GPIO 5, 6, 7: Display QSPI (D3, CS, D1)
- GPIO 9: Display TE
- GPIO 17: Display RST
- GPIO 18: Display QSPI D0
- GPIO 19, 20: USB
- GPIO 38: Display power
- GPIO 47, 48: Display QSPI (SCK, D2)

## MimiClaw Software Architecture
- FreeRTOS tasks: WiFi, Agent Loop, Serial CLI, UI Navigation
- Display: framebuffer in PSRAM, rendered to AMOLED via QSPI DMA
- UI pages: Splash -> System -> AI Status -> Message (cycle with Boot button)
- LLM: OpenRouter API (OpenAI-compatible), model configurable via CLI
- Tools: get_current_time, get_battery, web_search, read_file, write_file, edit_file, list_dir, cron_add/list/remove
- Memory: SPIFFS persistent storage (MEMORY.md, sessions, skills)
- Communication: Serial CLI, Telegram bot, WebSocket gateway
