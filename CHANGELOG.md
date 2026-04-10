# Changelog

All notable changes to MimiClaw-AMOLED will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [1.0.1] - 2026-04-08

### Security
- **Use-after-free fix** in `telegram_bot.c`: `tg_response_is_ok()` now copies description into caller-provided buffer instead of returning pointer to freed cJSON memory
- **Buffer overflow prevention** in `http_proxy.c`: CONNECT request buffer expanded 256→512 bytes for long hostnames; `strcpy` → `strncpy` with null termination for proxy type
- **HTTP response validation** in `http_proxy.c`: strict HTTP status line parsing replacing loose `strstr("200")` check
- **SOCKS5 response validation** in `http_proxy.c`: minimum response length corrected to 4 bytes (VER+REP+RSV+ATYP)
- **cJSON NULL safety** in `session_mgr.c` and `llm_proxy.c`: added `cJSON_IsString()` type checks before all `->valuestring` dereferences
- **Volatile qualifiers** added to multi-task global state variables (`s_connected`, `s_ws_connected`, etc.) to prevent compiler optimization issues

### Added
- **LLM retry logic** in `agent_loop.c`: single automatic retry with 3-second delay on first LLM call failure
- **WDT management** across long-blocking tasks: temporary unsubscription before LLM calls (30-120s) and Telegram long polling (30s), re-subscription after completion
- **Feishu WebSocket tuning**: `pingpong_timeout_sec = 30` for faster dead connection detection
- **Cron backup recovery** in `cron_service.c`: atomic `.bak` file creation before saves; auto-recovery from backup if main file is missing
- **Session write optimization** in `session_mgr.c`: write counter replaces per-append file scanning for compaction checks

### Changed
- Documentation updates: ARCHITECTURE.md, TODO.md, README.md, README_CN.md corrected to reflect actual codebase state (multi-provider LLM, implemented features, accurate task parameters)

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
