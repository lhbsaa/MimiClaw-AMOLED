# MimiClaw-AMOLED: AI Assistant with Display

<p align="center">
  <img src="assets/banner.png" alt="MimiClaw-AMOLED" width="500" />
</p>

<p align="center">
  <a href="LICENSE"><img src="https://img.shields.io/badge/License-MIT-yellow.svg" alt="License: MIT"></a>
  <a href="https://www.lilygo.cc/products/t-display-s3-amoled"><img src="https://img.shields.io/badge/Hardware-T--Display--S3--AMOLED-blue.svg" alt="Hardware"></a>
  <a href="https://github.com/memovai/mimiclaw"><img src="https://img.shields.io/badge/Based%20on-MimiClaw-green.svg" alt="Based on MimiClaw"></a>
</p>

<p align="center">
  <strong><a href="README.md">English</a> | <a href="README_CN.md">中文</a></strong>
</p>

**MimiClaw with AMOLED Display - Visual AI Assistant on ESP32-S3**

MimiClaw-AMOLED extends the original [MimiClaw](https://github.com/memovai/mimiclaw) project with a beautiful 1.91" AMOLED display, bringing visual feedback to your pocket AI assistant. See messages, status, and battery level at a glance.

## What's New

Based on the original MimiClaw, this version adds:

| Feature | Description |
|---------|-------------|
| **AMOLED Display** | 1.91" 536x240 QSPI AMOLED (RM67162) |
| **Frame Buffer** | Hardware-accelerated graphics in PSRAM |
| **Multi-Page UI** | Home, System, Messages, Logs pages |
| **Status Bar** | WiFi, Telegram, Time, Battery indicators |
| **Message Bubbles** | Visual chat bubbles for Telegram messages |
| **Battery Monitor** | Real-time battery percentage via ADC |
| **Touch Button** | Boot button navigation between pages |

## Hardware Requirements

- **LILYGO T-Display-S3 AMOLED 1.91"** 
  - ESP32-S3 @ 240MHz dual-core
  - 16MB Flash + 8MB PSRAM
  - 1.91" 536x240 AMOLED (RM67162 QSPI)
  - CST816S capacitive touch
  - Battery connector with ADC monitoring
- USB Type-C cable
- Telegram Bot Token
- Anthropic or OpenAI API Key

## Quick Start

### Prerequisites

- ESP-IDF v5.5+ installed
- Python 3.10+

### Build & Flash

```bash
# Clone repository
git clone https://github.com/lhbsaa/MimiClaw-AMOLED.git
cd MimiClaw-AMOLED

# Configure target
idf.py set-target esp32s3

# Copy and edit secrets
cp main/mimi_secrets.h.example main/mimi_secrets.h
# Edit main/mimi_secrets.h with your WiFi, Telegram, and API credentials

# Build and flash
idf.py -p PORT flash monitor
```

### Configuration

Edit `main/mimi_secrets.h`:

```c
#define MIMI_SECRET_WIFI_SSID       "YourWiFiName"
#define MIMI_SECRET_WIFI_PASS       "YourWiFiPassword"
#define MIMI_SECRET_TG_TOKEN        "123456:ABC-DEF..."
#define MIMI_SECRET_API_KEY         "sk-ant-api03-..."
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"  // or "openai"
```

## Display Features

### Status Bar
The status bar shows real-time information:
- **WiFi** - Connection status
- **TG** - Telegram connection status  
- **Time** - Current time (when synchronized)
- **Battery** - Battery percentage

### Page Navigation
Press the **Boot button** to navigate between pages:

| Page | Content |
|------|---------|
| **Home** | Welcome screen with device status |
| **System** | System messages and status |
| **Message** | Telegram chat bubbles |
| **Logs** | System logs for debugging |

### Message Display
Telegram messages appear as chat bubbles:
- Incoming messages: Left-aligned
- Outgoing messages: Right-aligned
- Sender name displayed above message

## Battery Monitoring

The battery level is read via ADC on GPIO4 (voltage divider 2:1):
- 3.3V = 0%
- 4.2V = 100%
- Updates every 5 seconds

## CLI Commands

Connect via serial (115200 baud) for configuration and debugging.

### Connection

- **Windows**: Use PuTTY or serial terminal to connect to COM port
- **Linux/Mac**: `screen /dev/ttyUSB0 115200` or `minicom -D /dev/ttyUSB0`

### Network Configuration

| Command | Description |
|---------|-------------|
| `set_wifi <ssid> <password>` | Set WiFi network and password |
| `wifi_status` | Show WiFi connection status |
| `wifi_scan` | Scan nearby WiFi networks |
| `set_proxy <host> <port>` | Set HTTP proxy |
| `clear_proxy` | Clear proxy settings |

### LLM Configuration

| Command | Description |
|---------|-------------|
| `set_api_key <key>` | Set LLM API key |
| `set_model <model>` | Set model name (e.g., gpt-4o, claude-sonnet-4) |
| `set_model_provider <provider>` | Set provider: anthropic/openai/openrouter/ollama/custom |
| `set_ollama <host> [port] [model]` | Configure Ollama local server |
| `set_api_url <url> [host]` | Configure custom API URL |

### Multi-Model Support

MimiClaw-AMOLED supports multiple LLM providers with easy switching:

| Provider | API Format | API Key Required | Use Case |
|----------|------------|------------------|----------|
| **ollama** | OpenAI-compatible | No | Local deployment, development |
| **openrouter** | OpenAI-compatible | Yes | Multiple model access |
| **openai** | OpenAI format | Yes | Production (GPT models) |
| **anthropic** | Anthropic format | Yes | Production (Claude models) |
| **custom** | OpenAI-compatible | Depends | Self-hosted services |

**Switch Provider Example:**
```bash
# Switch to Ollama local model
mimi> set_ollama 192.168.x.x 11434 qwen3.5:4b
mimi> restart

# Switch to OpenRouter
mimi> set_model_provider openrouter
mimi> set_model openai/gpt-4o-mini
mimi> set_api_key your-api-key
mimi> restart
```

**Recommended Ollama Models:**
| Model | Params | Quality | Speed | Recommended For |
|-------|--------|---------|-------|-----------------|
| qwen3.5:4b | 4B | ⭐⭐⭐⭐ | Fast | Daily use, balanced |
| qwen3.5:2b | 2B | ⭐⭐⭐ | Fastest | Resource-limited |
| qwen3.5:latest | ~7B | ⭐⭐⭐⭐⭐ | Medium | Deep analysis |

### Telegram/Feishu Configuration

| Command | Description |
|---------|-------------|
| `set_tg_token <token>` | Set Telegram Bot Token |
| `set_feishu_creds <app_id> <app_secret>` | Set Feishu app credentials |
| `feishu_send <open_id> "message"` | Send Feishu message |

### Search Configuration

| Command | Description |
|---------|-------------|
| `set_search_key <key>` | Set Brave Search API Key |
| `set_tavily_key <key>` | Set Tavily Search API Key |

### Memory & Sessions

| Command | Description |
|---------|-------------|
| `memory_read` | Read long-term memory content |
| `memory_write "content"` | Write to long-term memory |
| `session_list` | List all chat sessions |
| `session_clear <id>` | Clear specified session |

### Skill Management

| Command | Description |
|---------|-------------|
| `skill_list` | List installed skills |
| `skill_show <name>` | Display skill content |

### System Maintenance

| Command | Description |
|---------|-------------|
| `config_show` | Display current configuration (masked) |
| `config_reset` | Reset configuration to defaults |
| `heap_info` | Display memory usage |
| `heartbeat_trigger` | Manually trigger heartbeat check |
| `cron_start` | Start cron scheduler |
| `restart` | Reboot device |

### Debug Commands

| Command | Description |
|---------|-------------|
| `ask "question"` | Ask AI directly |
| `web_search "query"` | Execute web search |
| `tool_exec <tool> [args]` | Manually execute tool |

## Project Structure

```
MimiClaw-AMOLED/
├── main/
│   ├── display/
│   │   ├── display_manager.c   # QSPI display driver
│   │   ├── simple_gui.c        # Frame buffer graphics
│   │   └── ui_main.c           # UI controller
│   ├── peripherals/
│   │   ├── boot_button.c       # Button handler
│   │   └── battery_adc.c       # Battery monitor
│   └── mimi_config.h           # Configuration
├── docs/
│   ├── HARDWARE.md             # Hardware reference
│   ├── DISPLAY_DRIVER.md       # Display driver docs
│   └── TROUBLESHOOTING.md      # Troubleshooting guide
└── spiffs_data/                # Memory files
```

## Developer Documentation

| Document | Description |
|----------|-------------|
| [HARDWARE.md](docs/HARDWARE.md) | Hardware reference: pin definitions, specifications |
| [DISPLAY_DRIVER.md](docs/DISPLAY_DRIVER.md) | Display driver: frame buffer, QSPI, graphics API |
| [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | Troubleshooting: common issues and solutions |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | System architecture: modules, data flow |
| [TIME_QUERY_FIX.md](docs/TIME_QUERY_FIX.md) | Time query fix: root cause, solution, model evaluation |
| [CHANGELOG.md](CHANGELOG.md) | Change log: version history |

## Acknowledgments

- **MimiClaw** by [Ziboyan Wang](https://github.com/memovai/mimiclaw) - Original AI assistant framework
- **LILYGO** - T-Display-S3 AMOLED hardware
- **LilyGo-AMOLED-Series** - Display driver reference

## License

MIT License - See [LICENSE](LICENSE) for details.

This project is a derivative work of [MimiClaw](https://github.com/memovai/mimiclaw) with additional contributions for AMOLED display support.