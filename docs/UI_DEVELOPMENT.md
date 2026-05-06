# T113Claw UI 二次开发指南

UI 目前分两页：`Chat` 和 `Settings`。Chat 是主界面，所有运行态信息汇聚到这里；Settings 内部分成 `WIFI`、`SERVER LINK`、`SYSTEM` 三个子视图。进入 Settings 有两种方式：点右上角设置图标，或在 Chat 顶栏往下滑（`LV_DIR_BOTTOM` 手势）。顶栏保留 WiFi 图标，同时新增和 WiFi 同风格的服务器状态图标，实时显示远程服务器连通状态；它只显示状态，不再承担页面入口。

x86 和 T113 用的是同一份 UI 代码。`./build.sh -linux` 起 SDL 窗口，鼠标操作效果跟板端触摸一样，布局、事件和状态联动都可以在桌面先跑通，再上板验证。

---

## 启动顺序

`main.c` 先做配置文件加载、data 目录检查、WiFi 服务状态初始化和 message bus 初始化这几件轻量的事，然后立即调 `ui_manager_init()`，让 UI 尽快上屏。

`ui_manager_init()` 依次执行：`lv_init()` → 显示/触摸端口 → 主题样式 → 字体 → `ui_build_shell()`（搭顶栏 + 两个页面容器）→ 启动 1s 周期刷新定时器。之后主线程只跑 `ui_manager_update()`，内部就是 `lv_timer_handler()`。

慢的部分——WiFi 连接、LLM 客户端、语音、飞书通道、cron 等——全放在 `bootstrap_thread_main()` 后台线程里跑，不会卡住首屏刷新。

后台线程不能直接碰 LVGL 对象，所有跨线程的 UI 更新必须走异步桥（见下一节）。

---

## 代码结构

| 文件 | 干什么的 |
|------|----------|
| `src/ui/ui_manager.c` | UI 外壳、顶栏、1s 刷新、异步事件桥、`mc_log_error_sink` |
| `src/ui/ui_manager.h` | 对外接口（notify 函数） |
| `src/ui/ui_private.h` | `ui_context_t` 结构体 + 页间共享接口声明 |
| `src/ui/page_chat.c` | Chat 页所有布局和状态更新逻辑 |
| `src/ui/page_settings.c` | Settings 页 WiFi / Server Link / System 三个子视图 |
| `t113claw_config.h` | 图片路径宏 |

`ui_context_t` 是贯穿整个 UI 的上下文结构体，实例是 `ui_manager.c` 里的静态变量 `s_ui`。里面放的是需要跨文件访问的控件指针（比如顶栏的 `header_wifi_icon`、Chat 的 `chat_feed`），以及运行态状态字符串（`voice_state`、`agent_state`、`chat_error_source/text`）。页面内部的私有控件放在各自文件的静态结构体里，不需要上 `ui_context_t`。

---

## 运行态状态怎么进 UI

这是最需要搞清楚的部分。

所有跨线程的状态更新通过 `ui_manager_notify_*()` 投递，函数内部调 `lv_async_call()`，把事件挂到 LVGL 任务队列，等主线程在 `lv_timer_handler()` 里自然消费，不会有多线程竞争 LVGL 的问题。

对外暴露的接口（`ui_manager.h`）：

```c
void ui_manager_notify_user_message(const char *channel, const char *text);
void ui_manager_notify_assistant_message(const char *channel, const char *text);
void ui_manager_notify_voice_state(const char *state);
void ui_manager_notify_agent_state(const char *state);
```

另外还有 `mc_log_error_sink(tag, message)`，它是 `utils/log.h` 里的弱符号。所有 `LOG_E(...)` 调用都会走到这里，然后投递 `UI_EVT_RUNTIME_ERROR` 事件，Chat 页底部的红色错误字就是这么来的。业务模块只要用 `LOG_E` 记错误，UI 就自动显示，不需要额外适配。

WiFi 图标和服务器图标走的是另一条路：`ui_refresh_timer_cb()` 每秒轮询状态。WiFi 直接调 `wifi_service_poll_state()`；服务器状态则通过后台线程调用 `remote_client_status()`，结果再用 `lv_async_call()` 回到主线程更新顶栏和 Settings。这样能避免在 LVGL 线程里做阻塞网络请求。

---

## Chat 页

Chat 页横向分三列：左侧是头像卡和会话计数，中间是消息流，右侧是 Voice / Agent 状态卡。下方是 CLEAR / TAIL 两个按钮，底部最后一行是运行时错误字。

**消息流**：`page_chat_append(ui, role, channel, text)` 是唯一入口。`role` 有四种：`UI_CHAT_ROLE_USER`（用户气泡）、`UI_CHAT_ROLE_ASSISTANT`（AI 气泡）、`UI_CHAT_ROLE_EVENT`（橙色小徽章）、`UI_CHAT_ROLE_SYSTEM`（灰色小徽章）。历史最多保留 24 条，超出后滚动丢弃最旧的。助手文本在显示前过 `chat_sanitize_assistant_text()`，去掉 Markdown 标记和 4 字节 emoji（LVGL 字体不支持）。

**TAIL 键**：切换 `chat_follow_tail` 标志，控制每次追加消息后是否自动滚到底。不影响消息追加本身。

**CLEAR 键**：清空 `chat_entries[]`，重置消息计数，同时把 `chat_error_source/text` 清空，底部错误字一并隐藏。

**运行时错误字**：用 `page_chat_set_runtime_error(ui, source, text)` 写入，`page_chat_clear_runtime_error(ui, source)` 按来源清除。底层就是 `chat_error_source[32]` 和 `chat_error_text[160]` 两个字符串，不为空就显示，为空就隐藏。WiFi 断线时 `ui_manager.c` 写 source=`wifi`；Voice / Agent 恢复健康时，各自的 `set_state` 函数调 clear。

---

## Settings 页

Settings 顶部四个标签按钮：BACK、WIFI、SERVER LINK、SYSTEM。BACK 回 Chat，后三个切换子视图（`settings_switch_view()` 切换对应 panel 的可见性）。

**WIFI 视图**：左侧是 SSID / PASSWORD 两个 `lv_textarea`，右侧是常驻键盘。SAVE+RECONNECT 点击后，如果不在忙（`wifi_apply_busy` 标志），会起一个 `pthread` 跑 `settings_wifi_apply_thread()`，在那个线程里调 `wifi_service_stop()` 再 `wifi_service_start()`，结果通过 `lv_async_call()` 回到 UI 更新提示文字。顶栏 WiFi 图标显示的是链路状态，不代表配置立即生效。

**SERVER LINK 视图**：沿用 WIFI 页同一套布局风格。左侧显示 `SERVER STATE`、HOST / USER、PORT / PASSWORD 两行输入项和 `APPLY` 按钮；探测期间按钮会切成 `APPLYING` 并临时禁用。右侧仍然是常驻键盘。这个视图只负责配置局域网服务器连接信息，不再提供命令输入、TEST 或 RUN。保存后会写回 `data/config/config.ini` 的 `[remote]` 配置，并立即触发一次后台状态探测；连通结果同步显示在顶栏服务器图标和当前页提示文字里。

**SYSTEM 视图**：音量滑杆显示 0-100，实际映射到 60-80 的安全区间（`settings_volume_actual_to_display` / `settings_volume_display_to_actual`）。`RELEASED` 事件触发时才写 `config.ini` 并调 `audio_service_set_volume()`。重启按钮执行 `sync` 再 `reboot`，x86 下被 `#ifndef SIMULATOR_LINUX` 屏蔽。

---

## 图片资源

图片放在 `data/ui/image/`，路径宏定义在 `t113claw_config.h`。使用时用 `UI_FS_SRC(宏名)` 包一层——这个宏加上 `"A:"` 前缀，这是 LVGL 访问文件系统路径的约定。

```c
// 创建图片控件
lv_obj_t *img = ui_create_png_image(parent, UI_FS_SRC(T113CLAW_UI_ICON_WIFI_ON), 220);

// 替换已有控件的图片
ui_set_png_image_src(img, UI_FS_SRC(T113CLAW_UI_ICON_WIFI_OFF), 220);
```

zoom 是 LVGL 的 256 基准值：256 = 原始大小，220 ≈ 86%，192 ≈ 75%。

加一张新图：把 PNG 放进 `data/ui/image/` → 在 `t113claw_config.h` 加宏 → 代码里引用。删图时宏和 PNG 一起删，不然 T113 上会出现图标空白。

本轮新增了服务器相关 PNG 资源：

- `data/ui/image/icon_server_setting_pixel.png`：Settings 中 `SERVER LINK` 视图标签图标
- `data/ui/image/icon_server_on_pixel.png` / `icon_server_off_pixel.png` / `icon_server_connecting_pixel.png` / `icon_server_failed_pixel.png`：顶栏服务器状态图标

---

## 字体

`ui_font_init()` 从 `data/ui/font/SOURCEHANSANSCN_REGULAR.OTF` 加载三个字号（18/24/32pt）。加载失败时静默回退到 LVGL 内置的 Montserrat 和 unscii，界面继续运行但中文会显示方块。

首次部署到 T113 别忘记推字体文件：

```bash
adb push data/ui/font/SOURCEHANSANSCN_REGULAR.OTF /usr/share/t113claw/data/ui/font/
```

x86 开发时直接读 `data/` 目录，不用额外操作。

---

## 样式

公共样式在 `ui_theme_init()` 里定义，挂在 `ui_context_t` 的 style 字段上：

- `panel_style` / `panel_alt_style`：主面板背景（深蓝系 / 深紫系）
- `card_style` / `card_warn_style`：有边框的卡片，warn 是橙色警告边框
- `badge_style` / `badge_alert_style`：小徽章，alert 是橙红色
- `message_user_style` / `message_ai_style`：消息气泡
- `text_dim_style` / `text_value_style`：暗灰色辅助文字 / 亮白主文字

页面内部的局部样式直接用 `lv_obj_set_style_*` 就地设，不用抽成公共样式。

---

## 加新功能 / 删旧功能

**加一个新的运行态**：在 `ui_manager.c` 里加 `UI_EVT_*` 枚举值和对应的 `ui_manager_notify_xxx()` 函数（内部调 `ui_dispatch_async()`），在 `ui_async_apply()` 的 switch 里分发到页面 setter。后台模块只调 notify，不直接碰 LVGL 对象。新控件在 `page_chat_build()` 里创建；如果需要跨文件访问，把对象指针加进 `ui_context_t`（同步更新 `ui_private.h`）；只在 `page_chat.c` 内部用的放在文件顶部的静态变量里。

**删一个旧功能**：从 build 函数里删控件创建 → 从 `ui_context_t` 删字段 → 删 setter 和 notify 函数 → 删没用的图片宏和 PNG。改完跑 `./build.sh -linux` 确认没有悬空引用。

---

## 验证

UI 开发验证顺序：`./build.sh -linux` → SDL 桌面看布局和事件 → 必要时 `./build.sh -t113` 上板确认触摸、旋转和真实设备状态。
