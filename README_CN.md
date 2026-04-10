# MimiClaw-AMOLED: 带屏幕的 AI 助手

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

**MimiClaw 的 AMOLED 显示版本 - ESP32-S3 上的可视化 AI 助手**

MimiClaw-AMOLED 在原版 [MimiClaw](https://github.com/memovai/mimiclaw) 项目基础上增加了精美的 1.91" AMOLED 显示屏，让你的口袋 AI 助手具备视觉反馈能力。一目了然地查看消息、状态和电池电量。

## 新增功能

基于原版 MimiClaw，此版本新增：

| 功能 | 说明 |
|------|------|
| **AMOLED 显示屏** | 1.91" 536x240 QSPI AMOLED (RM67162) |
| **帧缓冲** | PSRAM 中的硬件加速图形 |
| **多页面 UI** | 主页、系统、消息、日志页面 |
| **状态栏** | WiFi、Telegram、时间、电池指示器 |
| **消息气泡** | Telegram 消息的可视化聊天气泡 |
| **电池监控** | 通过 ADC 实时监测电池电量 |
| **按键导航** | Boot 键切换页面 |

## 硬件要求

- **LILYGO T-Display-S3 AMOLED 1.91"**
  - ESP32-S3 @ 240MHz 双核
  - 16MB Flash + 8MB PSRAM
  - 1.91" 536x240 AMOLED (RM67162 QSPI)
  - CST816S 电容触摸
  - 带ADC监测的电池接口
- USB Type-C 数据线
- Telegram Bot Token
- Anthropic 或 OpenAI API Key

## 快速开始

### 前提条件

- 已安装 ESP-IDF v5.3+（已测试 v5.3.2）
- Python 3.10+

### 编译与烧录

```bash
# 克隆仓库
git clone https://github.com/lhbsaa/MimiClaw-AMOLED.git
cd MimiClaw-AMOLED

# 配置目标
idf.py set-target esp32s3

# 复制并编辑配置文件
cp main/mimi_secrets.h.example main/mimi_secrets.h
# 编辑 main/mimi_secrets.h，填入你的 WiFi、Telegram 和 API 凭证

# 编译并烧录
idf.py -p PORT flash monitor
```

### 配置

编辑 `main/mimi_secrets.h`：

```c
#define MIMI_SECRET_WIFI_SSID       "你的WiFi名称"
#define MIMI_SECRET_WIFI_PASS       "你的WiFi密码"
#define MIMI_SECRET_TG_TOKEN        "123456:ABC-DEF..."
#define MIMI_SECRET_API_KEY         "sk-ant-api03-..."
#define MIMI_SECRET_MODEL_PROVIDER  "anthropic"  // 或 "openai"
```

## 显示功能

### 状态栏
状态栏显示实时信息：
- **WiFi** - 连接状态
- **TG** - Telegram 连接状态
- **时间** - 当前时间（同步后）
- **电池** - 电池电量百分比

### 页面导航
按下 **Boot 键** 在页面间切换：

| 页面 | 内容 |
|------|------|
| **主页** | 欢迎屏幕和设备状态 |
| **系统** | 系统消息和状态 |
| **消息** | Telegram 聊天气泡 |
| **日志** | 调试系统日志 |

### 消息显示
Telegram 消息以聊天气泡形式显示：
- 收到的消息：左对齐
- 发出的消息：右对齐
- 发送者名称显示在消息上方

## 电池监控

通过 GPIO4 的 ADC 读取电池电量（分压比 2:1）：
- 3.3V = 0%
- 4.2V = 100%
- 每 5 秒更新一次

## CLI 命令

通过串口（115200 波特率）连接进行配置和调试。

### 连接方式

- **Windows**: 使用 PuTTY 或串口工具连接 COM 口
- **Linux/Mac**: `screen /dev/ttyUSB0 115200` 或 `minicom -D /dev/ttyUSB0`

### 网络配置

| 命令 | 说明 |
|------|------|
| `set_wifi <ssid> <password>` | 设置 WiFi 网络和密码 |
| `wifi_status` | 查看 WiFi 连接状态 |
| `wifi_scan` | 扫描附近的 WiFi 网络 |
| `set_proxy <host> <port>` | 设置 HTTP 代理 |
| `clear_proxy` | 清除代理设置 |

### LLM 配置

| 命令 | 说明 |
|------|------|
| `set_api_key <key>` | 设置 LLM API 密钥 |
| `set_model <model>` | 设置模型名称（如 gpt-4o, claude-sonnet-4） |
| `set_model_provider <provider>` | 设置提供商：anthropic/openai/openrouter/ollama/custom |
| `set_ollama <host> [port] [model]` | 配置 Ollama 本地服务 |
| `set_api_url <url> [host]` | 配置自定义 API 地址 |

### 多模型支持

MimiClaw-AMOLED 支持多种 LLM 提供商，可轻松切换：

| 提供商 | API 格式 | 需要 API Key | 适用场景 |
|--------|----------|--------------|----------|
| **ollama** | OpenAI 兼容 | 否 | 本地部署、开发测试 |
| **openrouter** | OpenAI 兼容 | 是 | 多模型访问 |
| **openai** | OpenAI 格式 | 是 | 生产环境（GPT 系列） |
| **anthropic** | Anthropic 格式 | 是 | 生产环境（Claude 系列） |
| **custom** | OpenAI 兼容 | 取决于服务 | 自建服务 |

**切换提供商示例：**
```bash
# 切换到 Ollama 本地模型
mimi> set_ollama 192.168.x.x 11434 qwen3.5:4b
mimi> restart

# 切换到 OpenRouter
mimi> set_model_provider openrouter
mimi> set_model openai/gpt-4o-mini
mimi> set_api_key your-api-key
mimi> restart
```

**推荐的 Ollama 模型：**
| 模型 | 参数量 | 质量 | 速度 | 推荐场景 |
|------|--------|------|------|----------|
| qwen3.5:4b | 4B | ⭐⭐⭐⭐ | 快 | 日常使用，综合最佳 |
| qwen3.5:2b | 2B | ⭐⭐⭐ | 最快 | 资源受限场景 |
| qwen3.5:latest | ~7B | ⭐⭐⭐⭐⭐ | 中等 | 深度分析 |

### Telegram/飞书配置

| 命令 | 说明 |
|------|------|
| `set_tg_token <token>` | 设置 Telegram Bot Token |
| `set_feishu_creds <app_id> <app_secret>` | 设置飞书应用凭证 |
| `feishu_send <open_id> "message"` | 发送飞书消息 |

### 搜索配置

| 命令 | 说明 |
|------|------|
| `set_search_key <key>` | 设置 Brave Search API Key |
| `set_tavily_key <key>` | 设置 Tavily Search API Key |

### 记忆与会话

| 命令 | 说明 |
|------|------|
| `memory_read` | 读取长期记忆内容 |
| `memory_write "content"` | 写入长期记忆 |
| `session_list` | 列出所有聊天会话 |
| `session_clear <id>` | 清除指定会话 |

### 技能管理

| 命令 | 说明 |
|------|------|
| `skill_list` | 列出已安装技能 |
| `skill_show <name>` | 显示技能内容 |

### 系统维护

| 命令 | 说明 |
|------|------|
| `config_show` | 显示当前配置（脱敏） |
| `config_reset` | 重置配置为默认值 |
| `heap_info` | 显示内存使用情况 |
| `heartbeat_trigger` | 手动触发心跳检查 |
| `cron_start` | 启动定时任务调度器 |
| `restart` | 重启设备 |

### 调试命令

| 命令 | 说明 |
|------|------|
| `ask "question"` | 直接向 AI 提问 |
| `web_search "query"` | 执行网页搜索 |
| `tool_exec <tool> [args]` | 手动执行工具 |

## 项目结构

```
MimiClaw-AMOLED/
├── main/
│   ├── agent/
│   │   ├── agent_loop.c         # ReAct 代理循环
│   │   └── context_builder.c    # 系统提示词构建
│   ├── bus/
│   │   └── message_bus.c        # 入站/出站消息队列
│   ├── channels/
│   │   ├── telegram/telegram_bot.c  # Telegram 长轮询
│   │   └── feishu/feishu_bot.c      # 飞书 WebSocket
│   ├── cron/
│   │   └── cron_service.c       # 定时任务调度
│   ├── display/
│   │   ├── display_manager.c    # QSPI 显示驱动
│   │   ├── simple_gui.c         # 帧缓冲图形
│   │   └── ui_main.c            # UI 控制器
│   ├── gateway/
│   │   └── ws_server.c          # WebSocket 服务器
│   ├── heartbeat/
│   │   └── heartbeat.c          # 周期心跳
│   ├── llm/
│   │   └── llm_proxy.c          # 多提供商 LLM API
│   ├── memory/
│   │   ├── memory_store.c       # 长期记忆
│   │   └── session_mgr.c        # 按会话管理
│   ├── peripherals/
│   │   ├── boot_button.c        # 按键处理
│   │   ├── battery_adc.c        # 电池监控
│   │   └── time_sync.c          # NTP 时间同步
│   ├── proxy/
│   │   └── http_proxy.c         # HTTP/SOCKS5 代理
│   ├── tools/
│   │   ├── tool_registry.c      # 工具分发
│   │   ├── tool_web_search.c    # Brave/Tavily 搜索
│   │   ├── tool_files.c         # 文件读写列目录
│   │   ├── tool_cron.c          # 定时任务管理
│   │   └── tool_hardware.c      # GPIO/LED 控制
│   ├── skills/
│   │   └── skill_loader.c       # SPIFFS 技能加载器
│   └── mimi_config.h            # 配置
├── docs/                        # 开发者文档
├── spiffs_data/                 # SPIFFS 初始数据
│   ├── config/                  # SOUL.md, USER.md
│   ├── memory/                  # MEMORY.md
│   └── skills/                  # 技能定义文件
└── .github/workflows/           # CI/CD
```

## 开发者文档

| 文档 | 说明 |
|------|------|
| [HARDWARE.md](docs/HARDWARE.md) | 硬件参考：引脚定义、规格参数 |
| [DISPLAY_DRIVER.md](docs/DISPLAY_DRIVER.md) | 显示驱动：帧缓冲、QSPI、图形API |
| [TROUBLESHOOTING.md](docs/TROUBLESHOOTING.md) | 故障排除：常见问题及解决方案 |
| [ARCHITECTURE.md](docs/ARCHITECTURE.md) | 系统架构：模块设计、数据流 |
| [TIME_QUERY_FIX.md](docs/TIME_QUERY_FIX.md) | 时间查询修复：根因分析、解决方案、模型评估 |
| [CHANGELOG.md](CHANGELOG.md) | 变更日志：版本历史 |

## 致谢

- **MimiClaw** - [Ziboyan Wang](https://github.com/memovai/mimiclaw) 原版 AI 助手框架
- **LILYGO** - T-Display-S3 AMOLED 硬件
- **LilyGo-AMOLED-Series** - 显示驱动参考

## 许可证

MIT License - 详见 [LICENSE](LICENSE)。

本项目是 [MimiClaw](https://github.com/memovai/mimiclaw) 的衍生作品，增加了 AMOLED 显示支持。