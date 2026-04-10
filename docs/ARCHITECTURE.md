# MimiClaw Architecture

> ESP32-S3 AI Agent firmware — C/FreeRTOS implementation running on bare metal (no Linux).

---

## System Overview

```
Telegram App (User)         Feishu App (User)
    │                           │
    │  HTTPS Long Polling       │  WebSocket
    │                           │
    ▼                           ▼
┌──────────────────────────────────────────────────┐
│               ESP32-S3 (MimiClaw)                │
│                                                  │
│   ┌─────────────┐       ┌──────────────────┐     │
│   │  Telegram    │──────▶│   Inbound Queue  │     │
│   │  Poller      │       │   (depth: 16)    │     │
│   │  (Core 0)    │       └────────┬─────────┘     │
│   └─────────────┘               │                │
│                                  │                │
│   ┌─────────────┐               ▼                │
│   │  Feishu WS   │  ┌────────────────────────┐    │
│   │  Client      │─▶│     Agent Loop          │    │
│   │  (Core 0)    │  │     (Core 1)           │    │
│   └─────────────┘  │                        │    │
│                     │  Context ──▶ LLM Proxy │    │
│   ┌─────────────┐  │  Builder   (Multi-LLM)  │    │
│   │  WebSocket   │─▶│       ▲          │      │    │
│   │  Server      │  │       │     tool_use?   │    │
│   │  (:18789)    │  │       │          ▼      │    │
│   └─────────────┘  │  Tool Results ◀─ Tools  │    │
│                     │   (web_search, files,   │    │
│   ┌─────────────┐  │    cron, gpio, time)    │    │
│   │  Serial CLI  │  └──────────┬─────────────┘    │
│   │  (Core 1)    │             │                  │
│   └─────────────┘       ┌──────▼───────┐          │
│                          │ Outbound Queue│          │
│   ┌─────────────┐       └──────┬───────┘          │
│   │  Cron Task   │             │                  │
│   │  + Heartbeat │       ┌──────▼───────┐          │
│   └─────────────┘       │  Outbound    │          │
│                          │  Dispatch    │          │
│   ┌─────────────┐       │  (Core 0)    │          │
│   │  AMOLED GUI  │       └──┬───┬────┬──┘          │
│   │  Display     │          │   │    │             │
│   └─────────────┘     Telegram Feishu WebSocket    │
│                       sendMsg  reply  send          │
│                                                   │
│   ┌──────────────────────────────────────────┐    │
│   │  SPIFFS (12 MB)                          │    │
│   │  /spiffs/config/  SOUL.md, USER.md       │    │
│   │  /spiffs/memory/  MEMORY.md, YYYY-MM-DD  │    │
│   │  /spiffs/skills/  *.md skill definitions  │    │
│   │  /spiffs/cron.json  scheduled tasks       │    │
│   │  /spiffs/s*.jl  session history files     │    │
│   └──────────────────────────────────────────┘    │
└───────────────────────────────────────────────────┘
         │
         │  Anthropic / OpenAI / Ollama / OpenRouter
         │  + Brave / Tavily Search API (HTTPS)
         ▼
   ┌───────────┐   ┌──────────────┐
   │  LLM API  │   │ Search API   │
   └───────────┘   └──────────────┘
```

---

## Data Flow

```
1. User sends message on Telegram (or WebSocket)
2. Channel poller receives message, wraps in mimi_msg_t
3. Message pushed to Inbound Queue (FreeRTOS xQueue)
4. Agent Loop (Core 1) pops message:
   a. Load session history from SPIFFS (JSONL)
   b. Build system prompt (SOUL.md + USER.md + MEMORY.md + recent notes + tool guidance)
   c. Build cJSON messages array (history + current message)
   d. ReAct loop (max 10 iterations):
      i.   Call Claude API via HTTPS (non-streaming, with tools array)
      ii.  Parse JSON response → text blocks + tool_use blocks
      iii. If stop_reason == "tool_use":
           - Execute each tool (e.g. web_search → Brave Search API)
           - Append assistant content + tool_result to messages
           - Continue loop
      iv.  If stop_reason == "end_turn": break with final text
   e. Save user message + final assistant text to session file
   f. Push response to Outbound Queue
5. Outbound Dispatch (Core 0) pops response:
   a. Route by channel field ("telegram" → sendMessage, "websocket" → WS frame)
6. User receives reply
```

---

## Module Map

```
main/
├── mimi.c                  Entry point — app_main() orchestrates init + startup
├── mimi_config.h           All compile-time constants + build-time secrets include
├── mimi_secrets.h          Build-time credentials (gitignored, highest priority)
├── mimi_secrets.h.example  Template for mimi_secrets.h
│
├── bus/
│   ├── message_bus.h       mimi_msg_t struct, queue API
│   └── message_bus.c       Two FreeRTOS queues: inbound + outbound
│
├── wifi/
│   ├── wifi_manager.h      WiFi STA lifecycle API
│   └── wifi_manager.c      Event handler, exponential backoff
│
├── channels/
│   ├── telegram/
│   │   ├── telegram_bot.h  Bot init/start, send_message API
│   │   └── telegram_bot.c  Long polling loop, JSON parsing, message splitting
│   └── feishu/
│       ├── feishu_bot.h    Feishu bot init/start API
│       └── feishu_bot.c    WebSocket client, event card parsing, message routing
│
├── llm/
│   ├── llm_proxy.h         llm_chat() + llm_chat_tools() API, tool_use types
│   └── llm_proxy.c         Multi-provider support (Anthropic/OpenAI/Ollama/OpenRouter/Custom)
│
├── agent/
│   ├── agent_loop.h        Agent task init/start
│   ├── agent_loop.c        ReAct loop: LLM call → tool execution → repeat
│   ├── context_builder.h   System prompt + messages builder API
│   └── context_builder.c   Reads bootstrap files + memory + tool guidance
│
├── tools/
│   ├── tool_registry.h     Tool definition struct, register/dispatch API
│   ├── tool_registry.c     Tool registration, JSON schema builder, dispatch by name
│   ├── tool_web_search.c   Brave/Tavily Search API via HTTPS (direct + proxy)
│   ├── tool_files.c        File read/write/list_dir on SPIFFS
│   ├── tool_cron.c         Cron job add/remove/list via agent tool_use
│   ├── tool_get_time.c     Current time query tool
│   ├── tool_hardware.c     LED control tool
│   ├── tool_gpio.c         GPIO read/write tool
│   └── gpio_policy.c       GPIO allowlist/safety policy
│
├── memory/
│   ├── memory_store.h      Long-term + daily memory API
│   ├── memory_store.c      MEMORY.md read/write, daily .md append/read
│   ├── session_mgr.h       Per-chat session API
│   └── session_mgr.c       JSONL session files, ring buffer history, auto-compaction
│
├── gateway/
│   ├── ws_server.h         WebSocket server API
│   └── ws_server.c         ESP HTTP server with WS upgrade, client tracking
│
├── proxy/
│   ├── http_proxy.h        Proxy connection API
│   └── http_proxy.c        HTTP CONNECT + SOCKS5 tunnel + TLS via esp_tls
│
├── cron/
│   ├── cron_service.h      Cron scheduler API
│   └── cron_service.c      Persistent cron jobs (every/at), SPIFFS storage, .bak recovery
│
├── heartbeat/
│   ├── heartbeat.h         Heartbeat service API
│   └── heartbeat.c         Periodic HEARTBEAT.md check, deferred I/O via cron task
│
├── skills/
│   └── skill_loader.c      Load SKILL.md files from SPIFFS into system prompt
│
├── cli/
│   ├── serial_cli.h        CLI init API
│   └── serial_cli.c        esp_console REPL with config/debug/maintenance commands
│
├── onboard/
│   └── wifi_onboard.c      Captive portal for WiFi provisioning (AP mode)
│
├── display/
│   ├── display_manager.c   RM67162 QSPI driver, DMA transfers
│   ├── lcd_init_sequence.c RM67162 initialization sequence
│   ├── simple_gui.c        Frame buffer graphics (RGB565, fonts, shapes)
│   ├── ui_main.c           Multi-page UI controller (Home/System/Message/Logs)
│   └── touch_cst816s.c     CST816S capacitive touch driver
│
└── peripherals/
    ├── boot_button.c       Button handler (single/double/long press)
    ├── battery_adc.c       Battery voltage ADC reading
    ├── time_sync.c         SNTP time synchronization
    └── health_monitor.c    System health monitoring
```

---

## FreeRTOS Task Layout

| Task               | Core | Priority | Stack  | Description                          |
|--------------------|------|----------|--------|--------------------------------------|
| `tg_poll`          | 0    | 5        | 12 KB  | Telegram long polling (30s timeout)  |
| `feishu_ws`        | 0    | 5        | 12 KB  | Feishu WebSocket client              |
| `agent_loop`       | 1    | 6        | 24 KB  | Message processing + LLM API call    |
| `outbound`         | 0    | 4        | 12 KB  | Route responses to channels          |
| `serial_cli`       | 1    | 6        | 4 KB   | USB serial console REPL              |
| `cron`             | any  | 4        | 4 KB   | Scheduled task + heartbeat polling   |
| `gui`              | any  | 5        | 8 KB   | AMOLED display rendering             |
| httpd (internal)   | 0    | 5        | —      | WebSocket server (esp_http_server)   |
| wifi_event (IDF)   | 0    | 8        | —      | WiFi event handling (ESP-IDF)        |

**Core allocation strategy**: Core 0 handles I/O (network, serial, WiFi). Core 1 is dedicated to the agent loop (CPU-bound JSON building + waiting on HTTPS). CLI shares Core 1 at same priority to avoid blocking agent when idle.

---

## Memory Budget

| Purpose                            | Location       | Size     |
|------------------------------------|----------------|----------|
| FreeRTOS task stacks               | Internal SRAM  | ~40 KB   |
| WiFi buffers                       | Internal SRAM  | ~30 KB   |
| TLS connections x2 (Telegram + Claude) | PSRAM      | ~120 KB  |
| JSON parse buffers                 | PSRAM          | ~32 KB   |
| Session history cache              | PSRAM          | ~32 KB   |
| System prompt buffer               | PSRAM          | ~16 KB   |
| LLM response stream buffer         | PSRAM          | ~32 KB   |
| Remaining available                | PSRAM          | ~7.7 MB  |

Large buffers (32 KB+) are allocated from PSRAM via `heap_caps_calloc(1, size, MALLOC_CAP_SPIRAM)`.

---

## Flash Partition Layout

```
Offset      Size      Name        Purpose
─────────────────────────────────────────────
0x009000    24 KB     nvs         ESP-IDF internal use (WiFi calibration etc.)
0x00F000     8 KB     otadata     OTA boot state
0x011000     4 KB     phy_init    WiFi PHY calibration
0x020000     2 MB     ota_0       Firmware slot A
0x220000     2 MB     ota_1       Firmware slot B
0x420000    12 MB     spiffs      Markdown memory, sessions, config
0xFF0000    64 KB     coredump    Crash dump storage
```

Total: 16 MB flash.

---

## Storage Layout (SPIFFS)

SPIFFS is a flat filesystem — no real directories. Files use path-like names.

```
/spiffs/config/SOUL.md          AI personality definition
/spiffs/config/USER.md          User profile
/spiffs/memory/MEMORY.md        Long-term persistent memory
/spiffs/memory/2026-02-05.md    Daily notes (one file per day)
/spiffs/sessions/tg_12345.jsonl Session history (one file per Telegram chat)
```

Session files are JSONL (one JSON object per line):
```json
{"role":"user","content":"Hello","ts":1738764800}
{"role":"assistant","content":"Hi there!","ts":1738764802}
```

---

## Configuration

MimiClaw supports two layers of configuration:

1. **Build-time secrets** (`mimi_secrets.h`): Compiled into firmware, highest priority
2. **Runtime NVS overrides** (via CLI commands): Stored in NVS flash, persist across reboots

| Define                       | CLI Command                  | Description                             |
|------------------------------|------------------------------|-----------------------------------------|
| `MIMI_SECRET_WIFI_SSID`     | `set_wifi <ssid> <pass>`    | WiFi SSID                               |
| `MIMI_SECRET_WIFI_PASS`     |                              | WiFi password                           |
| `MIMI_SECRET_TG_TOKEN`      | `set_tg_token <token>`      | Telegram Bot API token                  |
| `MIMI_SECRET_API_KEY`       | `set_api_key <key>`         | LLM API key                             |
| `MIMI_SECRET_MODEL`         | `set_model <model>`         | Model ID (e.g. claude-sonnet-4, gpt-4o) |
| `MIMI_SECRET_MODEL_PROVIDER`| `set_model_provider <p>`    | Provider: anthropic/openai/ollama/openrouter/custom |
| `MIMI_SECRET_PROXY_HOST`    | `set_proxy <host> <port>`   | HTTP/SOCKS5 proxy hostname/IP (optional)|
| `MIMI_SECRET_PROXY_PORT`    |                              | Proxy port (optional)                   |
| `MIMI_SECRET_SEARCH_KEY`    | `set_search_key <key>`      | Brave Search API key (optional)         |
| `MIMI_SECRET_TAVILY_KEY`    | `set_tavily_key <key>`      | Tavily Search API key (optional)        |
| `MIMI_SECRET_FEISHU_APP_ID` | `set_feishu_creds <id> <s>` | Feishu app ID (optional)                |

NVS values always override build-time secrets at runtime. Use `config_show` to display current (masked) configuration, `config_reset` to clear all NVS overrides.

---

## Message Bus Protocol

The internal message bus uses two FreeRTOS queues carrying `mimi_msg_t`:

```c
typedef struct {
    char channel[16];   // "telegram", "websocket", "cli"
    char chat_id[32];   // Telegram chat ID or WS client ID
    char *content;      // Heap-allocated text (ownership transferred)
} mimi_msg_t;
```

- **Inbound queue**: channels → agent loop (depth: 16)
- **Outbound queue**: agent loop → dispatch → channels (depth: 16)
- Content string ownership is transferred on push; receiver must `free()`.

---

## WebSocket Protocol

Port: **18789**. Max clients: **4**.

**Client → Server:**
```json
{"type": "message", "content": "Hello", "chat_id": "ws_client1"}
```

**Server → Client:**
```json
{"type": "response", "content": "Hi there!", "chat_id": "ws_client1"}
```

Client `chat_id` is auto-assigned on connection (`ws_<fd>`) but can be overridden in the first message.

---

## LLM API Integration

MimiClaw supports multiple LLM providers:

| Provider    | API Format   | Endpoint                                    |
|-------------|-------------|---------------------------------------------|
| anthropic   | Anthropic   | `https://api.anthropic.com/v1/messages`     |
| openai      | OpenAI      | `https://api.openai.com/v1/chat/completions`|
| ollama      | OpenAI      | `http://<host>:<port>/v1/chat/completions`  |
| openrouter  | OpenAI      | `https://openrouter.ai/api/v1/chat/completions` |
| custom      | OpenAI      | User-configured URL                         |

### Anthropic Format

Endpoint: `POST https://api.anthropic.com/v1/messages`

Request format (Anthropic-native, non-streaming, with tools):
```json
{
  "model": "claude-opus-4-6",
  "max_tokens": 4096,
  "system": "<system prompt>",
  "tools": [
    {
      "name": "web_search",
      "description": "Search the web for current information.",
      "input_schema": {"type": "object", "properties": {"query": {"type": "string"}}, "required": ["query"]}
    }
  ],
  "messages": [
    {"role": "user", "content": "Hello"},
    {"role": "assistant", "content": "Hi!"},
    {"role": "user", "content": "What's the weather today?"}
  ]
}
```

Key difference from OpenAI: `system` is a top-level field, not inside the `messages` array.

Non-streaming JSON response:
```json
{
  "id": "msg_xxx",
  "type": "message",
  "role": "assistant",
  "content": [
    {"type": "text", "text": "Let me search for that."},
    {"type": "tool_use", "id": "toolu_xxx", "name": "web_search", "input": {"query": "weather today"}}
  ],
  "stop_reason": "tool_use"
}
```

When `stop_reason` is `"tool_use"`, the agent loop executes each tool and sends results back:
```json
{"role": "assistant", "content": [<text + tool_use blocks>]}
{"role": "user", "content": [{"type": "tool_result", "tool_use_id": "toolu_xxx", "content": "..."}]}
```

The loop repeats until `stop_reason` is `"end_turn"` (max 10 iterations).

---

## Startup Sequence

```
app_main()
  ├── init_nvs()                    NVS flash init (erase if corrupted)
  ├── esp_event_loop_create_default()
  ├── init_spiffs()                 Mount SPIFFS at /spiffs
  ├── message_bus_init()            Create inbound + outbound queues
  ├── memory_store_init()           Verify SPIFFS paths
  ├── session_mgr_init()
  ├── wifi_manager_init()           Init WiFi STA mode + event handlers
  ├── http_proxy_init()             Load proxy config from build-time secrets
  ├── telegram_bot_init()           Load bot token from build-time secrets
  ├── llm_proxy_init()              Load API key + model from build-time secrets
  ├── tool_registry_init()          Register tools, build tools JSON
  ├── agent_loop_init()
  ├── serial_cli_init()             Start REPL (works without WiFi)
  │
  ├── wifi_manager_start()          Connect using build-time credentials
  │   └── wifi_manager_wait_connected(30s)
  │
  └── [if WiFi connected]
      ├── telegram_bot_start()      Launch tg_poll task (Core 0)
      ├── agent_loop_start()        Launch agent_loop task (Core 1)
      ├── ws_server_start()         Start httpd on port 18789
      └── outbound_dispatch task    Launch outbound task (Core 0)
```

If WiFi credentials are missing or connection times out, the CLI remains available for diagnostics.

---

## Serial CLI Commands

The CLI provides debug, maintenance, and runtime configuration commands via USB serial console.

| Command                          | Description                            |
|----------------------------------|----------------------------------------|
| `set_wifi <ssid> <pass>`         | Set WiFi credentials (NVS)            |
| `set_tg_token <token>`           | Set Telegram bot token (NVS)          |
| `set_api_key <key>`              | Set LLM API key (NVS)                 |
| `set_model <model>`              | Set model ID (NVS)                    |
| `set_model_provider <provider>`  | Set LLM provider (NVS)               |
| `set_proxy <host> <port>`        | Set HTTP/SOCKS5 proxy (NVS)          |
| `set_search_key <key>`           | Set Brave Search API key (NVS)       |
| `set_tavily_key <key>`           | Set Tavily Search API key (NVS)      |
| `set_feishu_creds <id> <secret>` | Set Feishu app credentials (NVS)     |
| `config_show`                    | Display current config (masked)       |
| `config_reset`                   | Clear all NVS overrides               |
| `wifi_status`                    | Show connection status and IP         |
| `memory_read`                    | Print MEMORY.md contents              |
| `memory_write <CONTENT>`         | Overwrite MEMORY.md                   |
| `session_list`                   | List all session files                |
| `session_clear <CHAT_ID>`        | Delete a session file                 |
| `heap_info`                      | Show internal + PSRAM free bytes      |
| `restart`                        | Reboot the device                     |
| `help`                           | List all available commands            |

---

## Nanobot Reference Mapping

| Nanobot Module              | MimiClaw Equivalent            | Notes                        |
|-----------------------------|--------------------------------|------------------------------|
| `agent/loop.py`             | `agent/agent_loop.c`           | ReAct loop with tool use     |
| `agent/context.py`          | `agent/context_builder.c`      | Loads SOUL.md + USER.md + memory + tool guidance |
| `agent/memory.py`           | `memory/memory_store.c`        | MEMORY.md + daily notes      |
| `session/manager.py`        | `memory/session_mgr.c`         | JSONL per chat, ring buffer  |
| `channels/telegram.py`      | `channels/telegram/telegram_bot.c` | Raw HTTP, no python-telegram-bot |
| `bus/events.py` + `queue.py`| `bus/message_bus.c`            | FreeRTOS queues vs asyncio   |
| `providers/litellm_provider.py` | `llm/llm_proxy.c`         | Multi-provider (Anthropic/OpenAI/Ollama/OpenRouter/Custom) |
| `config/schema.py`          | `mimi_config.h` + `mimi_secrets.h` | Build-time + NVS runtime override |
| `cli/commands.py`           | `cli/serial_cli.c`             | esp_console REPL             |
| `agent/tools/*`             | `tools/tool_registry.c` + `tool_*.c` | web_search, files, cron, gpio, time, hardware |
| `agent/subagent.py`         | *(not yet implemented)*        | See TODO.md                  |
| `agent/skills.py`           | `skills/skill_loader.c`        | Loads SKILL.md files from SPIFFS |
| `cron/service.py`           | `cron/cron_service.c`          | every/at scheduling, persistent, .bak recovery |
| `heartbeat/service.py`      | `heartbeat/heartbeat.c`       | Periodic HEARTBEAT.md check  |
