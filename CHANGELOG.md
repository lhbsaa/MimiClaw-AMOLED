# Changelog

All notable changes to MimiClaw-AMOLED will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.0] - 2026-03-19

### Added
- **AMOLED Display Support**
  - QSPI driver for RM67162 1.91" AMOLED display (536x240)
  - Frame buffer rendering in PSRAM (257KB)
  - Hardware-accelerated graphics primitives
  - SPI DMA chunked transfers for stability

- **User Interface**
  - Multi-page navigation system (Home, System, Messages, Logs)
  - Status bar with WiFi, Telegram, Time, and Battery indicators
  - Chat bubble display for Telegram messages
  - Boot button navigation (single/double/long press)
  - Splash screen on boot

- **Battery Monitoring**
  - ADC-based battery voltage reading (GPIO4)
  - Real-time battery percentage display
  - Voltage divider support (2:1 ratio)
  - 5-second update interval

- **Graphics Engine**
  - RGB565 color format support
  - Font rendering (scalable, sizes 1-4)
  - Rectangle and line drawing
  - Horizontal line optimization
  - String drawing with multiple font sizes

### Changed
- Adapted from original MimiClaw for T-Display-S3 AMOLED hardware
- Status bar layout optimized for 536px width
- Message display uses chat bubble style

### Fixed
- SPI transfer exceeding hardware max supported length
- Message persistence on page switching
- Status bar element spacing and overlap
- Boundary checking for message display

### Technical Details

#### Display Driver
- QSPI interface at 40MHz
- RM67162 controller initialization sequence
- Landscape mode (rotation 1)
- Double-buffered rendering

#### Memory Usage
- Frame buffer: 257KB PSRAM
- Display stack: 8KB
- GUI task priority: 5

#### GPIO Configuration
| Function | GPIO |
|----------|------|
| AMOLED CS | 6 |
| AMOLED SCK | 7 |
| AMOLED SDA | 8 |
| AMOLED DC | 9 |
| AMOLED RST | 17 |
| AMOLED BL | 38 |
| Touch SDA | 6 |
| Touch SCL | 7 |
| Touch RST | 13 |
| Touch INT | 14 |
| Battery ADC | 4 |
| Boot Button | 0 |

---

## Original MimiClaw Changelog

For changes in the original MimiClaw project, see:
https://github.com/memovai/mimiclaw/releases
