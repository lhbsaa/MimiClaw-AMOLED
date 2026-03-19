# Hardware Reference

This document provides complete hardware specifications and pin definitions for MimiClaw-AMOLED.

## Target Hardware

**LILYGO T-Display-S3 AMOLED 1.91" v1.0**

| Component | Specification |
|-----------|---------------|
| MCU | ESP32-S3 @ 240MHz dual-core |
| Flash | 16MB |
| PSRAM | 8MB (OPI/Octal) |
| Display | 1.91" AMOLED, 536x240, RM67162 |
| Touch | ❌ Not available (v1.0) |
| Battery | Li-Po connector with ADC monitoring |
| USB | Type-C (native USB Serial/JTAG) |

## Pin Definitions

### AMOLED Display (RM67162)

| Function | GPIO | Description |
|----------|------|-------------|
| DATA0 | GPIO 18 | Data line 0 |
| DATA1 | GPIO 7 | Data line 1 |
| DATA2 | GPIO 48 | Data line 2 |
| DATA3 | GPIO 5 | Data line 3 |
| SCK | GPIO 47 | Clock |
| CS | GPIO 6 | Chip Select |
| RST | GPIO 17 | Reset |
| TE | GPIO 9 | Tearing Effect |
| POWER | GPIO 38 | Power control |

> **Note**: Uses 8080 parallel interface (4-bit data bus), not standard QSPI.

### Touch Controller (CST816S)

> **Not available on v1.0** - Touch functionality is only present on v1.1 boards.

| Function | GPIO | Description |
|----------|------|-------------|
| SDA | - | I2C Data (v1.1 only) |
| SCL | - | I2C Clock (v1.1 only) |
| RST | - | Reset (v1.1 only) |
| INT | - | Interrupt (v1.1 only) |

### Battery ADC

| Function | GPIO | Description |
|----------|------|-------------|
| ADC Input | GPIO 4 | Battery voltage (via divider) |

**Voltage Divider**: 2:1 ratio
- ADC reading × 2 = actual battery voltage
- Range: 0-3.3V ADC = 0-6.6V battery

### User Interface

| Function | GPIO | Description |
|----------|------|-------------|
| Boot Button | GPIO 0 | Built-in boot button |
| LED | GPIO 21 | Status LED (if available) |

## Power Supply

| Source | Voltage | Notes |
|--------|---------|-------|
| USB-C | 5V | Primary power |
| Battery | 3.7V Li-Po | Optional backup |

**Power Consumption**:
- Active: ~150-250mA
- Display ON: ~50mA additional
- Sleep: ~10mA (WiFi connected)

## Display Specifications

| Parameter | Value |
|-----------|-------|
| Size | 1.91 inch |
| Resolution | 536 × 240 pixels |
| Technology | AMOLED |
| Controller | RM67162 |
| Interface | QSPI (4-line SPI) |
| Color Depth | 16-bit RGB565 |
| Max Brightness | 100% (PWM controlled) |

## Battery Specifications

| Parameter | Value |
|-----------|-------|
| Type | Li-Po |
| Nominal Voltage | 3.7V |
| Full Charge | 4.2V |
| Empty | 3.3V |
| Charging Threshold | > 4.25V (indicates USB power) |

## Memory Map

| Region | Size | Purpose |
|--------|------|---------|
| Internal SRAM | ~512KB | Stack, heap, critical data |
| PSRAM | 8MB | Frame buffer, large buffers |
| Flash | 16MB | Firmware, SPIFFS, NVS |

**Frame Buffer**: 257KB (536 × 240 × 2 bytes) in PSRAM

## Block Diagram

```
                    ┌─────────────────────────────────────┐
                    │           ESP32-S3                   │
                    │                                       │
   USB-C ───────────┤ USB Serial/JTAG                      │
                    │                                       │
   Battery ─────────┤ GPIO4 (ADC)                          │
                    │                                       │
   Boot Button ─────┤ GPIO0                                 │
                    │                                       │
                    │   ┌─────────────────────────────┐    │
                    │   │      8080 Parallel Bus      │    │
                    │   │  GPIO 5,6,7,9,17,18,47,48  │    │
                    │   └──────────┬──────────────────┘    │
                    │              │                        │
                    └──────────────┼────────────────────────┘
                                   │
                    ┌──────────────▼────────────────────────┐
                    │         RM67162 AMOLED                 │
                    │         536×240 Display                │
                    └───────────────────────────────────────┘
```

## Hardware Versions

| Version | Touch | Changes |
|---------|-------|---------|
| v1.0 | ❌ No | Initial release |
| v1.1 | ✅ Yes | Touch sensor added |

> **Important**: This project targets **v1.0** without touch functionality. Navigation is done via the Boot button only.

## Related Documentation

- [Display Driver](DISPLAY_DRIVER.md) - Technical details of display implementation
- [Troubleshooting](TROUBLESHOOTING.md) - Common hardware issues
- [LilyGo Wiki](https://github.com/Xinyuan-LilyGO/LilyGo-AMOLED-Series) - Official documentation
