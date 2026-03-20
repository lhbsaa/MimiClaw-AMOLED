# 时间查询修复报告

## 问题概述

**现象：** LLM在回答时间相关问题时返回错误日期（如2025年7月24日），而实际时间是2026年3月20日。

**影响：** 用户无法获得准确的时间信息，严重影响系统可用性。

---

## 问题分析

### 1. 原始工作流程（正确）

```
用户问时间 → LLM自己决定调用工具 → 工具返回时间 → LLM使用工具结果回答
```

日志证据：
```
I (86521) tools: Executing tool: get_current_time   ← LLM主动调用
I (87223) tool_time: Time fetched: 2026-03-19 19:28:18 CST
LLM回答: 2026年3月19日 19:28:18  ← 正确！
```

### 2. 被"优化"后的问题流程

```
用户问时间 → 预调用工具 → 时间注入系统提示 → LLM忽略系统提示 → LLM编造错误时间
```

日志证据：
```
I (38765) tool_time: Time fetched: 2026-03-20 11:48:25 CST  ← 时间获取正确
LLM回答: 2025年7月24日  ← 完全忽略，编造错误答案！
```

### 3. 根本原因

| 方法 | LLM行为 | 结果 |
|------|---------|------|
| **系统提示注入** | LLM可能忽略，用自己的"知识"回答 | 失败 |
| **用户消息注入** | LLM认为是对话的一部分，必须回应 | 成功 |

**关键洞察：** qwen3.5:4b 模型对系统提示的遵循程度较低，但对用户消息内容会认真处理。

---

## 解决方案

### 核心修改

将时间注入到**用户消息**中，而不是系统提示：

```c
// 修改前（系统提示注入 - 失败）
// 时间信息添加到 system prompt 中

// 修改后（用户消息注入 - 成功）
if (is_time_related_question(msg.content)) {
    // 预调用工具获取时间
    esp_err_t time_err = tool_registry_execute("get_current_time", "{}", time_result, sizeof(time_result));
    
    if (time_err == ESP_OK && strlen(time_result) > 0) {
        // 时间注入到用户消息中
        snprintf(user_content, new_len, 
            "%s\n\n[系统时间服务返回: %s]\n请使用上述时间回答问题。",
            msg.content, time_result);
    }
}
```

### 关键词检测

```c
const char *keywords[] = {
    "时间", "几点", "日期", "几号", "今天", "明天", "昨天",
    "今年", "去年", "明年", "星期", "周几", "哪年",
    "what time", "what date", "what day", "what year",
    "current time", "current date", "now",
    NULL
};
```

---

## 修改文件

| 文件 | 修改内容 |
|------|----------|
| `main/agent/agent_loop.c` | 时间预调用逻辑，消息注入方式 |

---

## 测试结果

### 时间查询测试

| 测试场景 | 模型 | 结果 |
|----------|------|------|
| "查询当前时间" | qwen3.5:4b | 2026年3月20日 12:07:07 |
| "今年是那年？" | qwen3.5:4b | 2026年 |
| "今年是那年？" | qwen3.5:2b | 2026年 |
| "未来24小时行动时间规划？" | qwen3.5:4b | 正确使用时间 |

### 非时间查询测试

| 测试场景 | 工具调用 | 结果 |
|----------|----------|------|
| "搜索国际黄金行情" | web_search | 正常 |
| "探究国际局势" | web_search | 正常 |
| "详述中东危机" | 无（基于上下文） | 正常 |

---

## 模型评估

### 测试模型

| 模型 | 参数量 | 测试次数 |
|------|--------|----------|
| qwen3.5:4b | 4B | 8次 |
| qwen3.5:latest | ~7B | 1次 |
| qwen3.5:2b | 2B | 2次 |

### 能力对比

| 评估维度 | qwen3.5:4b | qwen3.5:latest | qwen3.5:2b |
|----------|-----------|----------------|------------|
| 时间查询准确性 | 100% | 100% | 100% |
| 工具调用能力 | 正常 | 正常 | 正常 |
| 回答质量 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| 响应速度 | 快(1-2秒) | 中等(2-3秒) | 快(1-2秒) |

### 推荐结论

| 排名 | 模型 | 综合评分 | 推荐 |
|------|------|----------|------|
| 1 | qwen3.5:4b | ⭐⭐⭐⭐⭐ | **首选** |
| 2 | qwen3.5:latest | ⭐⭐⭐⭐ | 备选 |
| 3 | qwen3.5:2b | ⭐⭐⭐ | 轻量场景 |

**qwen3.5:4b 是当前最佳选择**，在嵌入式设备上实现了性能、速度、稳定性的最佳平衡。

---

## 方案对比

| 方式 | 控制方 | 可靠性 | 模型依赖 |
|------|--------|--------|----------|
| **预调用+注入**（当前） | 系统 | 高 | 低 |
| **LLM主动调用**（原始） | LLM | 取决于模型 | 高 |

当前方式更健壮，适合在资源受限的嵌入式设备上使用各种大小的模型。

---

## 经验教训

1. **不要过度依赖系统提示** - 小型模型对系统提示的遵循程度有限
2. **对话历史优先级更高** - LLM会认真处理用户消息中的信息
3. **保持简单** - 原始的LLM主动调用工具流程是正确的，"优化"反而引入了问题
4. **充分测试** - 多模型测试确保方案的普适性

---

## LLM Provider 配置

### 支持的 Provider 类型

| Provider | 说明 | API格式 | 需要API Key |
|----------|------|---------|-------------|
| **ollama** | 本地Ollama服务 | OpenAI兼容 | 否 |
| **openrouter** | OpenRouter API | OpenAI兼容 | 是 |
| **openai** | OpenAI官方API | OpenAI格式 | 是 |
| **anthropic** | Claude API | Anthropic格式 | 是 |
| **custom** | 自定义API | OpenAI兼容 | 取决于服务 |

### 切换 Provider

```bash
# 切换到 Ollama 本地模型
set_model_provider ollama

# 切换到 OpenRouter API
set_model_provider openrouter

# 切换到自定义 API
set_model_provider custom
```

**注意：** 切换后需要重启生效。

### Ollama 本地模型配置

```bash
# 设置 Ollama 服务地址和模型
set_ollama <host> <port> <model>

# 示例（替换为实际IP地址）
set_ollama 192.168.x.x 11434 qwen3.5:4b
```

配置完成后自动切换到 ollama provider。

### 自定义 API 配置

```bash
# 设置自定义 API URL
set_api_url <url>

# 示例
set_api_url https://your-api-server.com/v1/chat/completions
```

配置完成后自动切换到 custom provider。

### 配置存储

所有配置保存在 NVS 中，重启后保持：
- Provider: `MIMI_NVS_KEY_PROVIDER`
- API URL: `MIMI_NVS_KEY_API_URL`
- Model: `MIMI_NVS_KEY_MODEL`

---

## 多模型测试评估

### Ollama 本地模型测试

#### 测试环境
- **Ollama 服务：** 局域网服务器（端口 11434）
- **网络：** 局域网
- **设备：** ESP32-S3 (MimiClaw-AMOLED)

#### 测试模型

| 模型 | 参数量 | 内存占用 | 测试次数 |
|------|--------|----------|----------|
| qwen3.5:4b | 4B | ~4GB | 8次 |
| qwen3.5:latest | ~7B | ~7GB | 1次 |
| qwen3.5:2b | 2B | ~2GB | 2次 |

#### 性能对比

| 评估维度 | qwen3.5:4b | qwen3.5:latest | qwen3.5:2b |
|----------|-----------|----------------|------------|
| 时间查询准确性 | 100% | 100% | 100% |
| 工具调用能力 | 正常 | 正常 | 正常 |
| 回答质量 | ⭐⭐⭐⭐ | ⭐⭐⭐⭐⭐ | ⭐⭐⭐ |
| 响应延迟 | 1-2秒 | 2-3秒 | 1-2秒 |
| 首字响应 | 快 | 中等 | 快 |

#### 行为特点

**qwen3.5:4b（推荐）**
```
✅ 时间查询准确
✅ 回答简洁实用
✅ 格式化良好
✅ 工具调用可靠
⚠️ 回答长度适中
```

**qwen3.5:latest**
```
✅ 回答更详细全面
✅ 分析更深入
⚠️ 响应时间较长
⚠️ 回答可能超出屏幕显示
```

**qwen3.5:2b**
```
✅ 响应速度快
✅ 内存占用最小
⚠️ 可能跑题（混入上下文内容）
⚠️ 信息组织略松散
```

### 场景推荐

| 使用场景 | 推荐模型 | 理由 |
|----------|----------|------|
| **日常交互** | qwen3.5:4b | 性能平衡最佳 |
| **深度分析** | qwen3.5:latest | 信息更全面 |
| **资源受限** | qwen3.5:2b | 占用最小 |
| **快速查询** | qwen3.5:4b | 响应快且准确 |

### API Provider 对比

| Provider | 延迟 | 可靠性 | 成本 | 适用场景 |
|----------|------|--------|------|----------|
| **ollama** | 低（局域网） | 高 | 免费 | 开发测试、本地部署 |
| **openrouter** | 中 | 高 | 按量付费 | 多模型切换 |
| **openai** | 中 | 高 | 按量付费 | 生产环境 |
| **anthropic** | 中 | 高 | 按量付费 | Claude系列 |
| **custom** | 取决于服务 | 取决于服务 | 取决于服务 | 自建服务 |

---

## 切换示例

### 从 OpenRouter 切换到 Ollama

```bash
# 当前使用 OpenRouter，切换到 Ollama 本地模型
mimi> set_ollama <host> <port> <model>

# 示例
mimi> set_ollama 192.168.x.x 11434 qwen3.5:4b

# 重启生效
mimi> restart
```

**说明：** `set_ollama` 命令会自动将 provider 切换为 ollama。

### 从 Ollama 切换到 OpenRouter

```bash
# 当前使用 Ollama，切换到 OpenRouter API
mimi> set_model_provider openrouter
mimi> set_model <model-name>
mimi> set_api_key <your-api-key>

# 重启生效
mimi> restart
```

**说明：** OpenRouter 支持多种模型，如 `openai/gpt-4o-mini`、`anthropic/claude-3-haiku` 等。

### 从 Ollama 切换到自定义 API

```bash
# 当前使用 Ollama，切换到自定义 API
mimi> set_api_url <api-url>
mimi> set_model <model-name>
mimi> set_api_key <api-key>  # 如果需要

# 重启生效
mimi> restart
```

**说明：** `set_api_url` 命令会自动将 provider 切换为 custom。

---

## 关键代码位置

| 文件 | 功能 |
|------|------|
| `main/cli/serial_cli.c` | CLI命令处理 (set_model_provider, set_ollama, set_api_url) |
| `main/llm/llm_proxy.c` | LLM API代理，provider切换逻辑 |
| `main/agent/agent_loop.c` | 时间预调用逻辑 |

### Provider 判断逻辑

```c
// 判断是否为 OpenAI 兼容格式
static bool provider_uses_openai_format(void) {
    return provider_is_openai() || provider_is_ollama() || provider_is_custom();
}

// API 端点选择
static const char *get_api_endpoint(void) {
    return provider_uses_openai_format() ? "/v1/chat/completions" : "/v1/messages";
}
```

---

## 文档信息

- **创建日期：** 2026-03-20
- **修复版本：** 当前版本
- **测试状态：** 通过
- **测试模型：** qwen3.5:4b, qwen3.5:latest, qwen3.5:2b
- **测试Provider：** ollama, openrouter
