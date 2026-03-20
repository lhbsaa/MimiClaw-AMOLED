#pragma once

#include <stdbool.h>
#include <stddef.h>

/*
 * GPIO Pin Allocation for MimiClaw-AMOLED v1.0 (ESP32-S3)
 * Hardware v1.0: NO touch controller (CST816S not populated)
 *
 * === USED PINS (DO NOT MODIFY) ===
 * GPIO 0     - Boot Button
 * GPIO 4     - Battery ADC
 * GPIO 5     - LCD QSPI D3
 * GPIO 6     - LCD CS
 * GPIO 7     - LCD QSPI D1
 * GPIO 8     - LCD SDO (MISO)
 * GPIO 9     - LCD TE
 * GPIO 17    - LCD RST
 * GPIO 18    - LCD QSPI D0
 * GPIO 19    - USB Serial/JTAG (internal)
 * GPIO 20    - USB Serial/JTAG (internal)
 * GPIO 26-37 - Octal PSRAM (8MB)
 * GPIO 38    - LED + LCD Power
 * GPIO 47    - LCD SCK
 * GPIO 48    - LCD QSPI D2
 *
 * === AVAILABLE PINS (for user GPIO tools) ===
 * GPIO 1     - General purpose (Strapping, avoid during boot)
 * GPIO 2, 3  - General purpose (Touch I2C pads, but no touch chip on v1.0)
 * GPIO 10-16 - General purpose (7 pins available)
 * GPIO 21    - General purpose (Touch IRQ pad, but no touch chip on v1.0)
 * GPIO 46    - General purpose
 *
 * NOTE: GPIO 1 is a strapping pin. Keep HIGH during boot for normal boot mode.
 */

#define MIMI_GPIO_MIN_PIN       1
#define MIMI_GPIO_MAX_PIN       48

/* Available pins for user GPIO operations on v1.0 (no touch) */
#define MIMI_GPIO_ALLOWED_CSV   "1,2,3,10,11,12,13,14,15,16,21,46"

/**
 * Check if a pin is allowed for user GPIO operations.
 * Validates against the allowlist or default range, and blocks
 * pins reserved for flash/PSRAM on ESP32.
 */
bool gpio_policy_pin_is_allowed(int pin);

/**
 * Write a human-readable hint if the pin is forbidden for a known reason.
 * Returns true if a hint was written (and the caller should return the error).
 */
bool gpio_policy_pin_forbidden_hint(int pin, char *result, size_t result_len);
