# MimiClaw-AMOLED v1.0 改进计划

> 文档版本：1.0
> 更新日期：2025年3月21日
> 硬件版本：v1.0（无触摸功能）

---

## 一、当前状态评估

### 1.1 硬件限制说明

| 限制 | 原因 | 影响 |
|------|------|------|
| **无触摸功能** | v1.0 PCB 未焊接 CST816T 触控芯片 | 交互依赖按钮 |
| **无内置电池** | 需外接电源 | 便携性受限 |
| **单一按钮** | 仅 Boot 按钮可用 | 交互方式有限 |

### 1.2 当前功能现状

```
┌─────────────────────────────────────────────────────────────────┐
│                    当前功能清单                                  │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  ✅ 已实现功能                                                   │
│  ├── AMOLED 显示驱动 (QSPI)                                     │
│  ├── 帧缓冲图形系统 (257KB PSRAM)                               │
│  ├── 多页面 UI (Boot/System/Message/Logs)                       │
│  ├── 状态栏 (时间/WiFi/Telegram/电池)                           │
│  ├── Telegram Bot 集成                                          │
│  ├── 飞书 Bot 集成                                              │
│  ├── 多 LLM 提供商支持                                          │
│  ├── 工具系统 (GPIO/搜索/时间/文件)                              │
│  ├── 会话管理                                                   │
│  ├── 串口 CLI                                                   │
│  └── WiFi 配置                                                  │
│                                                                 │
│  ⚠️ 需改进功能                                                   │
│  ├── 按钮交互逻辑 (较单一)                                       │
│  ├── 信息展示密度 (过于稀疏)                                     │
│  ├── 动画效果 (无)                                              │
│  ├── 常亮显示模式 (无)                                          │
│  └── 文档完善度 (待补充)                                         │
│                                                                 │
│  ❌ 硬件限制 (v1.0 无法实现)                                     │
│  ├── 触摸交互                                                   │
│  ├── 内置电池供电                                               │
│  └── 语音输入/输出                                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

---

## 二、软件层面改进

### 2.1 按钮交互增强

#### 2.1.1 当前按钮行为

| 手势 | 当前行为 | 问题 |
|------|----------|------|
| 单击 | 切换下一页 | ✅ 可用 |
| 双击 | 切换下一页 | 重复功能 |
| 长按 | 重启设备 | ⚠️ 误触风险 |
| 超长按 | 未实现 | - |

#### 2.1.2 建议改进方案

```
┌─────────────────────────────────────────────────────────────────┐
│                    按钮交互改进方案                              │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│  单击 (短按 < 300ms)                                            │
│  ├── 状态栏页面：显示快捷菜单                                    │
│  ├── 消息页面：标记已读/下一条                                   │
│  ├── 日志页面：向下滚动                                          │
│  └── 其他页面：切换下一页                                        │
│                                                                 │
│  双击 (两次单击 < 500ms)                                         │
│  ├── 状态栏页面：快速提问 AI                                     │
│  ├── 消息页面：回复最后一条消息                                  │
│  └── 其他页面：返回首页                                          │
│                                                                 │
│  长按 (500ms - 2000ms)                                          │
│  ├── 状态栏页面：显示系统设置                                    │
│  ├── 消息页面：删除当前消息                                      │
│  └── 其他页面：上下文菜单                                        │
│                                                                 │
│  超长按 (> 3000ms)                                              │
│  ├── 任意页面：重启设备                                          │
│  └── 需要确认动画防止误触                                        │
│                                                                 │
│  组合操作 (长按 + 单击)                                          │
│  ├── 长按状态下单击：亮度调节                                    │
│  └── 可扩展更多组合                                              │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

#### 2.1.3 实现代码示例

```c
// 按钮手势扩展定义
typedef enum {
    BTN_GESTURE_NONE = 0,
    BTN_GESTURE_SINGLE_CLICK,
    BTN_GESTURE_DOUBLE_CLICK,
    BTN_GESTURE_TRIPLE_CLICK,      // 新增：三击
    BTN_GESTURE_LONG_PRESS,        // 500ms - 2000ms
    BTN_GESTURE_VERY_LONG_PRESS,   // > 3000ms
    BTN_GESTURE_PRESS_RELEASE,     // 长按释放
    BTN_GESTURE_HOLDING,           // 持续按住（用于亮度调节等）
} btn_gesture_t;

// 上下文相关回调
typedef void (*btn_context_callback_t)(ui_page_t page, btn_gesture_t gesture);

// 按页面注册回调
void boot_button_register_page_callback(ui_page_t page, btn_context_callback_t cb);
```

### 2.2 信息展示优化

#### 2.2.1 当前问题

```
当前状态栏页面：
┌──────────────────────────────────────┐
│ 时间: 14:30                          │ ← 信息稀疏
│ 日期: 2025-03-21 Saturday            │
│ 问候: Good Morning!                  │
│                                      │
│                                      │ ← 大量空白
│                                      │
│                                      │
│                                      │
└──────────────────────────────────────┘
```

#### 2.2.2 优化方案

```
优化后状态栏页面：
┌──────────────────────────────────────┐
│ ⏰ 14:30    📅 2025-03-21 Sat        │ ← 紧凑状态栏
│ ─────────────────────────────────────│
│                                      │
│   Good Morning! 👋                   │ ← 问候+表情
│                                      │
│   🤖 AI Ready  •  📨 3 unread        │ ← 状态摘要
│   🌡️ 26°C     •  🔋 85%              │ ← 环境信息
│                                      │
│   ┌─────────────────────────────┐   │
│   │ [Ask AI...]  [⚙️]  [📖]    │   │ ← 快捷入口
│   └─────────────────────────────┘   │
│                                      │
│ Press BOOT for menu                  │ ← 操作提示
└──────────────────────────────────────┘
```

#### 2.2.3 信息密度原则

| 原则 | 说明 |
|------|------|
| **一瞥可读** | 核心信息 0.5 秒可理解 |
| **分层展示** | 主要信息大字体，次要信息小字体 |
| **图标化** | 用图标替代文字标签 |
| **动态隐藏** | 非活跃信息自动隐藏 |
| **上下文相关** | 根据页面显示相关信息 |

### 2.3 动画系统

#### 2.3.1 可实现动画列表

| 动画类型 | 性能影响 | 实现难度 | 优先级 |
|----------|----------|----------|--------|
| **消息弹出** | 低 | 简单 | P0 |
| **AI 思考指示** | 极低 | 简单 | P0 |
| **页面切换** | 中 | 中等 | P1 |
| **按钮反馈** | 极低 | 简单 | P1 |
| **启动动画** | 低 | 中等 | P2 |
| **常亮显示** | 极低 | 简单 | P2 |

#### 2.3.2 动画实现框架

```c
// 动画类型定义
typedef enum {
    ANIM_TYPE_NONE = 0,
    ANIM_TYPE_FADE_IN,
    ANIM_TYPE_FADE_OUT,
    ANIM_TYPE_SLIDE_LEFT,
    ANIM_TYPE_SLIDE_RIGHT,
    ANIM_TYPE_SLIDE_UP,
    ANIM_TYPE_SLIDE_DOWN,
    ANIM_TYPE_SCALE,
    ANIM_TYPE_TYPING,       // 打字机效果
    ANIM_TYPE_PULSE,        // 脉冲
    ANIM_TYPE_BOUNCE,       // 弹跳
} anim_type_t;

// 动画配置
typedef struct {
    anim_type_t type;
    uint32_t duration_ms;
    uint32_t delay_ms;
    bool loop;
    easing_func_t easing;   // 缓动函数
    void (*on_complete)(void *ctx);
} animation_t;

// 动画管理器 API
void anim_init(void);
void anim_play(animation_t *anim, void *context);
void anim_stop(animation_t *anim);
void anim_update(uint32_t delta_ms);  // 在主循环中调用

// 预设动画
void anim_message_slide_in(int16_t start_y, int16_t end_y);
void anim_page_transition(ui_page_t from, ui_page_t to, bool left);
void anim_ai_thinking_pulse(void);
```

#### 2.3.3 具体动画效果

**消息弹出动画**：
```c
void animate_message_in(const char *sender, const char *msg) {
    // 从右侧滑入
    for (int16_t x = SCREEN_WIDTH; x >= MSG_X; x -= 20) {
        gui_clear_screen(COLOR_BLACK);
        gui_show_status_bar(...);
        gui_show_message_at(sender, msg, x, MSG_Y);
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(16));  // ~60fps
    }
}
```

**AI 思考动画**：
```c
void animate_ai_thinking(void) {
    static const char dots[] = "⠋⠙⠹⠸⠼⠴⠦⠧⠇⠏";
    static int frame = 0;
    
    // 只更新状态指示区域（局部刷新）
    gui_fill_rect(200, 100, 136, 30, COLOR_BLACK);
    
    char buf[16];
    snprintf(buf, sizeof(buf), "AI %c thinking", dots[frame % 10]);
    gui_draw_string(210, 108, buf, COLOR_CYAN, FONT_16x16);
    
    gui_flush_region(200, 100, 136, 30);
    frame++;
}
```

**页面切换动画**：
```c
void animate_page_slide(ui_page_t to, bool left) {
    uint16_t *old_fb = s_fb;  // 保存当前帧
    uint16_t *new_fb = alloc_temp_buffer();  // 分配临时缓冲
    
    // 渲染新页面到临时缓冲
    render_page_to_buffer(to, new_fb);
    
    // 滑动过渡
    int step = left ? -20 : 20;
    for (int offset = 0; abs(offset) < SCREEN_WIDTH; offset += step) {
        // 绘制两页内容
        // ...实现混合绘制
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(16));
    }
    
    // 释放临时缓冲
    free_temp_buffer(new_fb);
}
```

### 2.4 常亮显示 (AOD) 模式

#### 2.4.1 设计方案

```
AOD 模式显示内容：
┌──────────────────────────────────────┐
│                                      │
│         ⏰ 14:30                     │ ← 仅时间
│                                      │
│         🔋 85%  📶                   │ ← 关键状态
│                                      │
└──────────────────────────────────────┘

特点：
1. 仅显示时间 + 关键状态图标
2. 每分钟位置微移（防止烧屏）
3. 黑色背景（AMOLED 省电）
4. 按钮唤醒完整界面
```

#### 2.4.2 实现代码

```c
// AOD 模式控制
typedef struct {
    bool enabled;
    uint16_t offset_x;      // 位置偏移（防烧屏）
    uint16_t offset_y;
    uint32_t last_shift;    // 上次位置偏移时间
} aod_state_t;

static aod_state_t s_aod = {
    .enabled = false,
    .offset_x = 0,
    .offset_y = 0,
};

void aod_enable(void) {
    s_aod.enabled = true;
    display_manager_set_brightness(50);  // 降低亮度
    aod_update();
}

void aod_disable(void) {
    s_aod.enabled = false;
    display_manager_set_brightness(175);  // 恢复亮度
    ui_show_page(s_current_page);
}

void aod_update(void) {
    if (!s_aod.enabled) return;
    
    // 每 60 秒移动位置（防止烧屏）
    if (esp_timer_get_time() - s_aod.last_shift > 60000000) {
        s_aod.offset_x = (s_aod.offset_x + 10) % 40 - 20;
        s_aod.offset_y = (s_aod.offset_y + 5) % 20 - 10;
        s_aod.last_shift = esp_timer_get_time();
    }
    
    // 清屏
    gui_clear_screen(COLOR_BLACK);
    
    // 绘制时间（居中+偏移）
    char time_str[8];
    get_current_time_str(time_str, sizeof(time_str));
    int16_t x = (SCREEN_WIDTH - 96) / 2 + s_aod.offset_x;  // 96 = 时间宽度
    int16_t y = SCREEN_HEIGHT / 2 - 20 + s_aod.offset_y;
    gui_draw_string(x, y, time_str, COLOR_WHITE, FONT_24x24);
    
    // 绘制状态图标
    int16_t icon_y = y + 40;
    if (wifi_connected) {
        gui_draw_string(SCREEN_WIDTH/2 - 30, icon_y, "📶", COLOR_GRAY, FONT_16x16);
    }
    // 电池图标
    char bat_str[8];
    snprintf(bat_str, sizeof(bat_str), "🔋%d%%", battery_level);
    gui_draw_string(SCREEN_WIDTH/2 + 10, icon_y, bat_str, COLOR_GRAY, FONT_16x16);
    
    gui_flush();
}
```

### 2.5 快捷菜单系统

#### 2.5.1 设计方案

```
单击按钮弹出快捷菜单：
┌──────────────────────────────────────┐
│                                      │
│   ┌─────────────────────────────┐   │
│   │  🔍 Search                  │   │
│   │  💬 Ask AI                  │   │
│   │  ⚙️ Settings                │   │
│   │  📋 Clipboard               │   │
│   │  🔋 Battery Info            │   │
│   │  ℹ️ About                   │   │
│   └─────────────────────────────┘   │
│                                      │
│   BOOT: Select  Long: Cancel         │
└──────────────────────────────────────┘

交互方式：
- 单击：选择下一项
- 长按：确认选择
- 超长按：取消/返回
```

#### 2.5.2 实现代码

```c
// 快捷菜单项
typedef struct {
    const char *label;
    const char *icon;
    void (*action)(void);
} menu_item_t;

static const menu_item_t s_quick_menu[] = {
    {"Search", "🔍", action_search},
    {"Ask AI", "💬", action_ask_ai},
    {"Settings", "⚙️", action_settings},
    {"Clipboard", "📋", action_clipboard},
    {"Battery", "🔋", action_battery_info},
    {"About", "ℹ️", action_about},
};
#define QUICK_MENU_COUNT (sizeof(s_quick_menu)/sizeof(s_quick_menu[0]))

static int s_menu_index = 0;
static bool s_menu_active = false;

void quick_menu_show(void) {
    s_menu_active = true;
    s_menu_index = 0;
    quick_menu_render();
}

void quick_menu_hide(void) {
    s_menu_active = false;
    ui_show_page(s_current_page);
}

void quick_menu_next(void) {
    s_menu_index = (s_menu_index + 1) % QUICK_MENU_COUNT;
    quick_menu_render();
}

void quick_menu_select(void) {
    if (s_menu_index >= 0 && s_menu_index < QUICK_MENU_COUNT) {
        s_quick_menu[s_menu_index].action();
    }
    quick_menu_hide();
}

void quick_menu_render(void) {
    gui_clear_screen(COLOR_BLACK);
    
    // 标题
    gui_draw_string(20, 40, "Quick Menu", COLOR_CYAN, FONT_24x24);
    gui_draw_hline(20, SCREEN_WIDTH - 20, 70, COLOR_DARK_GRAY);
    
    // 菜单项
    int16_t y = 85;
    for (int i = 0; i < QUICK_MENU_COUNT && y < SCREEN_HEIGHT - 40; i++) {
        uint16_t color = (i == s_menu_index) ? COLOR_WHITE : COLOR_GRAY;
        uint16_t bg = (i == s_menu_index) ? COLOR_DARK_GRAY : COLOR_BLACK;
        
        // 高亮选中项
        if (i == s_menu_index) {
            gui_fill_rect(15, y - 2, SCREEN_WIDTH - 30, 24, bg);
        }
        
        char buf[32];
        snprintf(buf, sizeof(buf), "%s %s", s_quick_menu[i].icon, s_quick_menu[i].label);
        gui_draw_string(25, y, buf, color, FONT_16x16);
        y += 28;
    }
    
    // 底部提示
    gui_draw_string(20, SCREEN_HEIGHT - 25, "BOOT:Next  Long:Select", COLOR_DARK_GRAY, FONT_8x8);
    
    gui_flush();
}
```

---

## 三、UI/UX 改进

### 3.1 页面布局优化

#### 3.1.1 统一设计语言

| 元素 | 规范 |
|------|------|
| **状态栏高度** | 35px，绿色背景 |
| **内容边距** | 15px 左右 |
| **分隔线** | COLOR_DARK_GRAY，1px |
| **主标题** | FONT_24x24，白色 |
| **副标题** | FONT_16x16，青色 |
| **正文** | FONT_16x16，白色 |
| **提示文字** | FONT_8x8，灰色 |

#### 3.1.2 各页面优化方案

**Home 页面优化**：
```
┌──────────────────────────────────────────────────────────────┐
│ ⏰ 14:30  │         Home          │  WiFi  TG  85%          │
│───────────┴────────────────────────┴─────────────────────────│
│                                                              │
│     ███████╗██╗███╗   ███╗██╗    ██╗                         │
│     ██╔════╝██║████╗ ████║██║    ██║   MimiClaw              │
│     █████╗  ██║██╔████╔██║██║ █╗ ██║   AMOLED                │
│     ██╔══╝  ██║██║╚██╔╝██║██║███╗██║                         │
│     ██║     ██║██║ ╚═╝ ██║╚███╔███╔██╝                        │
│     ╚═╝     ╚═╝╚═╝     ╚═╝ ╚══╝╚══╝╚═╝                        │
│                                                              │
│   ┌─────────────────────────────────────────────────────┐   │
│   │  14:30    Saturday, March 21                        │   │
│   └─────────────────────────────────────────────────────┘   │
│                                                              │
│   Good Morning! 👋                                           │
│                                                              │
│   🤖 AI Ready    📨 3 messages    🔋 85%                     │
│                                                              │
│   ┌─────────────────────────────────────────────────────┐   │
│   │  [Ask AI...]              [⚙️ Settings]             │   │
│   └─────────────────────────────────────────────────────┘   │
│                                                              │
│   BOOT: Menu                                                │
└──────────────────────────────────────────────────────────────┘
```

**消息页面优化**：
```
┌──────────────────────────────────────────────────────────────┐
│ ⏰ 14:30  │       Messages        │  WiFi  TG  85%           │
│───────────┴────────────────────────┴─────────────────────────│
│                                                              │
│   ┌─────────────────────────────────────────────────────┐   │
│   │ 📨 Telegram Bot                                     │   │
│   │    "今天下午3点开会，请准时参加..."                   │   │
│   │    14:25                              [👆] [📋]      │   │
│   └─────────────────────────────────────────────────────┘   │
│                                                              │
│   ┌─────────────────────────────────────────────────────┐   │
│   │ 🤖 AI Assistant                                     │   │
│   │    "已为您设置下午3点的会议提醒..."                   │   │
│   │    14:26                              [👆] [📋]      │   │
│   └─────────────────────────────────────────────────────┘   │
│                                                              │
│   ┌─────────────────────────────────────────────────────┐   │
│   │ 👤 You                                               │   │
│   │    "帮我查一下明天的天气"                             │   │
│   │    14:28                              [👆] [📋]      │   │
│   └─────────────────────────────────────────────────────┘   │
│                                                              │
│   BOOT: Scroll  Long: Actions                               │
└──────────────────────────────────────────────────────────────┘
```

### 3.2 状态指示优化

#### 3.2.1 AI 状态指示

| 状态 | 图标 | 颜色 | 说明 |
|------|------|------|------|
| **Ready** | 🤖 | 绿色 | AI 待命 |
| **Thinking** | 🤖⏳ | 黄色+动画 | AI 处理中 |
| **Error** | 🤖❌ | 红色 | AI 错误 |
| **Offline** | 🤖💤 | 灰色 | LLM 服务不可用 |

#### 3.2.2 连接状态指示

| 状态 | WiFi 图标 | 说明 |
|------|-----------|------|
| **已连接** | 📶 绿色 | 信号强度可显示 |
| **连接中** | 📶 黄色闪烁 | 正在连接 |
| **断开** | 📶 红色 | 已断开 |
| **禁用** | 📶 灰色 | WiFi 关闭 |

### 3.3 交互反馈优化

#### 3.3.1 按钮反馈

```c
// 按钮按下时的视觉反馈
void on_button_press_feedback(void) {
    // 边框闪烁效果
    gui_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_WHITE);
    gui_flush();
    vTaskDelay(pdMS_TO_TICKS(50));
    gui_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BLACK);
    gui_flush();
}

// 操作确认动画
void on_action_confirm(void) {
    // 快速闪烁确认
    for (int i = 0; i < 2; i++) {
        gui_fill_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_WHITE);
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(30));
        gui_clear_screen(COLOR_BLACK);
        gui_flush();
        vTaskDelay(pdMS_TO_TICKS(30));
    }
}
```

#### 3.3.2 错误提示

```c
// 错误提示弹窗
void show_error_popup(const char *message, uint32_t duration_ms) {
    // 绘制错误弹窗
    int16_t box_w = 400;
    int16_t box_h = 80;
    int16_t box_x = (SCREEN_WIDTH - box_w) / 2;
    int16_t box_y = (SCREEN_HEIGHT - box_h) / 2;
    
    // 背景
    gui_fill_rect(box_x - 5, box_y - 5, box_w + 10, box_h + 10, COLOR_RED);
    gui_fill_rect(box_x, box_y, box_w, box_h, COLOR_BLACK);
    
    // 图标和文字
    gui_draw_string(box_x + 10, box_y + 10, "❌ Error", COLOR_RED, FONT_16x16);
    gui_draw_string(box_x + 10, box_y + 35, message, COLOR_WHITE, FONT_16x16);
    
    gui_flush();
    
    // 自动消失
    vTaskDelay(pdMS_TO_TICKS(duration_ms));
    
    // 恢复原页面
    ui_show_page(s_current_page);
}
```

---

## 四、性能优化

### 4.1 帧缓冲优化

| 优化项 | 当前状态 | 优化方案 | 预期收益 |
|--------|----------|----------|----------|
| **全屏刷新** | 每次都刷新全屏 | 局部刷新 | 减少 90% 传输 |
| **双缓冲** | 无 | 实现双缓冲 | 消除闪烁 |
| **脏区域** | 无 | 跟踪变化区域 | 减少绘制量 |

### 4.2 内存优化

| 优化项 | 当前状态 | 优化方案 | 预期收益 |
|--------|----------|----------|----------|
| **日志缓冲** | 固定 6 行 80 字符 | 动态分配 | 节省 ~0.5KB |
| **消息缓存** | 固定 8 条 | 按需分配 | 节省 ~1KB |
| **字体数据** | 内置 5x7 | 可选大字体 | 减少 Flash |

### 4.3 功耗优化

| 场景 | 当前功耗 | 优化方案 | 预期功耗 |
|------|----------|----------|----------|
| **待机** | ~100mA | AOD 模式 | ~30mA |
| **显示** | ~150mA | 降低亮度 | ~100mA |
| **AI 处理** | ~300mA | 无法优化 | ~300mA |

---

## 五、实施计划

### 5.1 阶段划分

```
Phase 1: 基础改进 (1-2 周)
├── 按钮交互增强
│   ├── 实现上下文相关按钮行为
│   ├── 添加三击手势
│   └── 优化长按逻辑
├── 信息展示优化
│   ├── 优化 Home 页面布局
│   ├── 增加状态摘要
│   └── 添加快捷入口
└── 代码整理
    ├── 添加注释
    └── 重构冗余代码

Phase 2: 动画系统 (2-3 周)
├── 动画框架
│   ├── 动画类型定义
│   ├── 动画管理器
│   └── 缓动函数
├── 具体动画
│   ├── 消息弹出动画
│   ├── AI 思考动画
│   ├── 按钮反馈动画
│   └── 页面切换动画
└── 性能优化
    ├── 局部刷新
    └── 帧率控制

Phase 3: 高级功能 (2-3 周)
├── 快捷菜单系统
├── AOD 常亮显示
├── 设置页面
└── 更多工具集成

Phase 4: 测试和文档 (1 周)
├── 功能测试
├── 性能测试
├── 文档更新
└── 发布准备
```

### 5.2 优先级排序

| 优先级 | 功能 | 理由 | 工作量 |
|--------|------|------|--------|
| **P0** | 按钮交互增强 | 提升可用性 | 2 天 |
| **P0** | 信息展示优化 | 提升价值感 | 2 天 |
| **P1** | AI 思考动画 | 用户体验关键 | 1 天 |
| **P1** | 消息弹出动画 | 视觉效果提升 | 1 天 |
| **P1** | 快捷菜单 | 功能入口 | 2 天 |
| **P2** | 页面切换动画 | 视觉效果 | 2 天 |
| **P2** | AOD 模式 | 功耗优化 | 1 天 |
| **P2** | 设置页面 | 可配置性 | 2 天 |

---

## 六、v2.0 硬件规划

### 6.1 建议硬件升级

| 升级项 | 当前 | v2.0 建议 | 理由 |
|--------|------|-----------|------|
| **触摸** | 无 | CST816T 焊接 | 核心交互升级 |
| **电池** | 无 | 500-1000mAh 内置 | 便携性 |
| **充电** | 无 | TP4056 + 保护 | 充电管理 |
| **按钮** | 1 个 | 2-3 个 | 更多操作 |
| **震动** | 无 | 可选震动马达 | 触觉反馈 |

### 6.2 v2.0 功能规划

```
v2.0 核心功能：
├── 触摸交互
│   ├── 点击选择
│   ├── 滑动导航
│   ├── 长按菜单
│   └── 手势识别
├── 内置电池
│   ├── 充电指示
│   ├── 电量管理
│   └── 低功耗模式
├── 多按钮
│   ├── 导航按钮
│   ├── 确认按钮
│   └── 返回按钮
└── 可选模块
    ├── 震动反馈
    ├── 麦克风（语音输入）
    └── 扬声器（语音输出）
```

### 6.3 PCB 设计建议

| 要点 | 说明 |
|------|------|
| **触摸芯片位置** | 靠近 FPC 接口，减少走线 |
| **电池接口** | JST 1.25 或焊接点 |
| **充电 IC** | TP4056 或 SY6970 |
| **按键布局** | 侧面或顶部，便于操作 |
| **天线布局** | 远离显示屏，减少干扰 |

---

## 七、总结

### 7.1 v1.0 改进重点

1. **软件层面**：按钮交互、信息展示、动画系统
2. **用户体验**：视觉反馈、状态指示、快捷菜单
3. **性能优化**：局部刷新、功耗控制

### 7.2 预期效果

| 改进项 | 当前状态 | 改进后 |
|--------|----------|--------|
| **按钮交互** | 单一切换 | 上下文相关、多手势 |
| **信息展示** | 稀疏 | 紧凑、信息丰富 |
| **动画效果** | 无 | 基础动画、流畅反馈 |
| **用户体验** | 功能性 | 体验级 |

### 7.3 下一步行动

1. 实施按钮交互增强
2. 优化信息展示布局
3. 添加基础动画效果
4. 完善文档和测试

---

## 附录

### A. 文件修改清单

| 文件 | 修改内容 |
|------|----------|
| `main/peripherals/boot_button.c` | 添加三击手势、上下文回调 |
| `main/display/ui_main.c` | 页面优化、快捷菜单 |
| `main/display/simple_gui.c` | 动画函数、优化布局 |
| `main/display/simple_gui.h` | 新增 API 声明 |

### B. API 变更

```c
// 新增 API
void quick_menu_show(void);
void quick_menu_hide(void);
void aod_enable(void);
void aod_disable(void);
void anim_play(animation_t *anim, void *ctx);

// 修改 API
void boot_button_register_page_callback(ui_page_t page, btn_context_callback_t cb);
```
