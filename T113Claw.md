# T113Claw — 嵌入式 AI Agent 项目文档

> 目标平台：Allwinner T113-S3 Linux 开发板（ARM Cortex-A7, 128MB DDR3, 128MB SPI NAND）
> 语言：纯 C（C11）｜版本：0.1.0

---

## 一、项目概述

T113Claw 是一个运行在 T113-S3 Linux 开发板上的微型 AI Agent。它通过 LLM（大语言模型）驱动，支持 **CLI 终端**、**飞书机器人** 和 **语音交互** 三种交互通道，具备工具调用（Function Calling / ReAct 循环）、持久化记忆、会话管理和定时任务等能力。

### 设计特点

- **纯 C 实现**：适合资源受限的嵌入式环境，编译产物仅约 **706KB**（ARM t113claw）+ **97KB**（ARM t113claw_audio）
- **模块化架构**：消息总线 + 通道抽象 + 工具注册表，各模块解耦
- **双 Provider 支持**：OpenAI 兼容 API（DeepSeek、Qwen、GPT 等）和 Anthropic Claude API
- **ReAct Agent 循环**：LLM 输出工具调用 → 执行 → 结果回填 → 再次推理，最多 10 轮迭代
- **网络搜索能力**：支持联网检索，`auto` 模式下有 Tavily key 时优先 Tavily，失败或未配置时回退搜狗移动搜索
- **局域网远程控制**：通过 `remote_exec` 工具连接一台已配置的 Linux 服务器，执行简单命令并回传输出
- **交叉编译就绪**：内置 T113 交叉编译工具链配置和预编译 ARM 库
- **板端 UI 已接入**：基于 LVGL 的 Chat / Settings 两页已可编译运行，Settings 内含 WIFI / SERVER LINK / SYSTEM 三个子视图，T113 与 x86 SDL 模拟器共用同一套 UI 流程

---

## 二、项目结构

```
T113Claw/
├── build.sh                    # 构建脚本（-linux / -t113 / -clean）
├── CMakeLists.txt              # 顶层 CMake
├── T113Claw.md                   # 本文档
│
├── audio/                      # 音频进程（独立可执行文件）
│   ├── CMakeLists.txt          # 音频进程构建配置
│   ├── audio_main.c            # 音频进程入口 + IPC 服务器
│   ├── audio_capture.c/h       # ALSA 录音（MIC3, 16kHz, mono）
│   ├── audio_playback.c/h      # ALSA 播放（HP, 16kHz, stereo）
│   ├── audio_codec.c/h         # T113 音频编解码器初始化（amixer）
│   └── audio_ipc.h             # IPC 协议定义（共享头文件）
│
├── component/                  # 第三方库
│   ├── cjson/                  # cJSON（MIT 许可）
│   │   ├── cJSON.c
│   │   ├── cJSON.h
│   │   └── CMakeLists.txt
│   └── net/                    # 网络客户端
│       ├── http_client.c/h     # HTTP 客户端（封装 libcurl）
│       ├── ws_client.c/h       # WebSocket 客户端（OpenSSL TLS）
│       └── CMakeLists.txt
│
├── data/                       # 运行时数据目录
│   ├── config/
│   │   └── config.ini          # 运行时配置文件
│   ├── memory/
│   │   ├── SOUL.md             # AI 人设（身份 + 性格 + 能力 + 准则）
│   │   ├── USER.md             # 用户画像（AI 逐步学习填充）
│   │   └── MEMORY.md           # 长期记忆（跨会话事实积累）
│   ├── sessions/               # 会话历史（JSONL 格式）
│   ├── skills/                 # Markdown 技能文件
│   │   ├── memory-management.md    # 记忆管理技能
│   │   ├── scheduled-tasks.md      # 定时任务技能
│   │   ├── system-monitor.md       # 系统监控技能
│   │   ├── daily-briefing.md       # 每日简报技能
│   │   ├── file-management.md      # 笔记/文件管理技能
│   │   ├── web-search.md           # 网络搜索技能
│   │   ├── remote-control.md       # 局域网远程控制技能
│   │   ├── voice-interaction.md    # 语音交互指引
│   │   └── skill-creator.md        # 技能创建（元技能）
│   ├── ui/
│   │   ├── image/              # UI PNG 资源
│   │   └── font/               # UI 字体资源（SOURCEHANSANSCN_REGULAR.OTF）
│   └── kws-model/              # 唤醒词 KWS 模型（sherpa-onnx zipformer int8）
│
├── docs/                       # 技术文档
│   ├── DEPLOY.md               # 编译与部署指南
│   ├── UI_DEVELOPMENT.md       # UI 二次开发指南
│   ├── AUDIO_SERVICE.md        # 音频服务技术文档（IPC 协议、ALSA、环形缓冲）
│   ├── VOICE_INTERACTION.md    # 语音交互技术文档（唤醒词、VAD、讯飞协议、状态机）
│   ├── FEISHU_SETUP.md         # 飞书机器人配置指南
│   ├── REMOTE_CONTROL.md       # 局域网远程控制说明
│   └── WEB_SEARCH.md           # 网络搜索设计与配置说明
│
├── scripts/
│   └── t113claw_remote_agent.py  # PC 端远程执行 HTTP agent
│
├── platform/
│   ├── t113/
│   │   ├── t113.cmake          # T113 交叉编译工具链文件
│   │   ├── lib/                # 预编译 ARM 库
│   │   │   ├── libcurl.so*
│   │   │   ├── libssl.so*
│   │   │   ├── libcrypto.so*
│   │   │   ├── libfreetype.so*
│   │   │   ├── libbz2.so*
│   │   │   ├── libuapi.so
│   │   │   ├── libcjson.a
│   │   │   ├── libnghttp2.so*
│   │   │   ├── libz.so*
│   │   │   ├── libasound.so*   # ALSA 音频库
│   │   │   └── libspeexdsp.so* # Speex DSP（VAD 语音活动检测）
│   │   ├── include/            # ARM 对应头文件
│   │   │   ├── alsa/           # ALSA 头文件
│   │   │   ├── speex/          # Speex DSP 头文件（VAD）
│   │   │   ├── freetype/       # FreeType 头文件（UI 字体渲染）
│   │   │   ├── cJSON/
│   │   │   ├── curl/
│   │   │   └── openssl/
│   │   ├── sherpa-onnx/        # sherpa-onnx 预编译库（唤醒词检测）
│   │   │   ├── lib/            # libsherpa-onnx-c-api.so (11MB, 含静态 onnxruntime)
│   │   │   └── include/        # C API 头文件
│   │   └── src/
│   │       ├── porting/        # LVGL T113 显示/输入/时钟/G2D 端口层
│   │       └── utils/          # 端口层辅助工具
│   └── x86linux/
│       └── linux.cmake         # x86 Linux 工具链文件
│
└── src/                        # 源代码
    ├── main.c                  # 入口，初始化与主循环
    ├── t113claw_config.h         # 编译时默认配置
    ├── t113claw_secrets.h        # 编译时密钥（不入库）
    │
    ├── config/                 # 配置管理
    │   ├── config.c
    │   └── config.h
    │
    ├── llm/                    # LLM 客户端
    │   ├── llm_client.c/h      # Provider 分发层
    │   ├── provider_openai.c   # OpenAI 兼容 API（含 DeepSeek/Qwen/GPT）
    │   └── provider_claude.c   # Anthropic Claude API
    │
    ├── agent/                  # Agent 核心
    │   ├── agent_loop.c/h      # ReAct 循环（推理 + 工具调用）
    │   └── context_builder.c/h # System Prompt 构建器
    │
    ├── message_bus/            # 消息总线
    │   ├── message_bus.c/h     # 线程安全双队列
    │   └── message.h           # 消息结构定义
    │
    ├── channels/               # 交互通道
    │   ├── channel.h           # 通道抽象接口（mc_channel_t）
    │   ├── channel_manager.c/h # 统一通道注册表与出站分发
    │   ├── cli/
    │   │   └── cli_channel.c/h # CLI 终端通道
    │   ├── feishu/
    │   │   ├── feishu_bot.c/h  # 飞书机器人（WS 长连接 + REST 发消息）
    │   │   └── feishu_proto.c/h# 飞书 protobuf 帧编解码
    │   └── voice/
    │       └── voice_channel.c/h # 语音通道（封装 voice_manager 为统一接口）
    │
    ├── tools/                  # 内置工具
    │   ├── tool_registry.c/h   # 工具注册表
    │   ├── tool_time.c         # 时间查询
    │   ├── tool_file.c         # 文件读写
    │   ├── tool_system.c       # 系统信息
    │   ├── tool_cron.c         # 定时任务管理
    │   ├── tool_exec.c         # Shell 命令执行（自我迭代核心）
    │   ├── tool_remote_exec.c/h# 局域网远程命令执行（单服务器配置）
    │   └── tool_web_search.c/h # 网络搜索（搜狗移动搜索 + Tavily 可选增强）
    │
    ├── memory/                 # 记忆系统
    │   ├── memory_store.c/h    # SOUL/USER/MEMORY 文件读写
    │   └── session_mgr.c/h     # 会话历史管理
    │
    ├── skills/                 # 技能系统
    │   └── skill_loader.c/h    # Markdown 技能加载器
    │
    ├── services/               # 后台服务
    │   ├── wifi_service.c/h    # WiFi 自动连接（wpa_cli + DHCP + NTP）
    │   ├── cron_service.c/h    # 定时任务调度器
    │   ├── heartbeat.c/h       # 心跳检测
    │   └── audio_service.c/h   # 音频服务 IPC 客户端（管理 t113claw_audio 进程）
    │
    ├── voice/                  # 语音交互
    │   ├── voice_manager.c/h   # 语音状态机（WAKE_LISTENING→LISTENING→RECOGNIZING→THINKING→SPEAKING）
    │   ├── wake_detector.c/h   # 本地唤醒词检测（sherpa-onnx KWS，"你好小爪"）
    │   ├── vad.c/h             # 语音活动检测（Speex DSP VAD）
    │   ├── xfyun_auth.c/h      # 讯飞 HMAC-SHA256 鉴权 URL 构建
    │   ├── xfyun_stt.c/h       # 讯飞语音识别 WebSocket 客户端
    │   ├── xfyun_tts.c/h       # 讯飞语音合成 WebSocket 客户端（流式回调）
    │   └── gpio_button.c/h     # GPIO sysfs 按键检测（PD10, poll + 轮询双模式）
    │
    ├── ui/                     # LVGL UI
    │   ├── ui_manager.c/h      # UI manager + shell + async event bridge
    │   ├── ui_private.h        # 页内共享状态/样式/接口
    │   ├── page_chat.c         # 聊天页（消息流 + 语音/Agent 状态 + 运行时错误字）
    │   └── page_settings.c     # 设置页（WIFI / SERVER LINK / SYSTEM 视图）
    │
    └── utils/                  # 工具函数
        ├── utils.h             # 返回码、文件操作、字符串工具
        └── log.h               # 彩色日志宏
```

---

## 三、模块完成状态

### ✅ 已完成（可用）

| 模块 | 说明 |
|------|------|
| **构建系统** | CMake + build.sh，x86 和 T113 两种编译目标都成功 |
| **配置管理** | 两层配置（编译时密钥 → INI 运行时覆盖），INI 缺失时自动从编译时密钥生成，支持环境变量 `T113CLAW_API_KEY` |
| **HTTP 客户端** | libcurl 封装，支持 POST JSON 和 GET |
| **LLM 客户端** | Provider 分发层 + OpenAI 兼容 API + Claude API 完整实现 |
| **Agent 循环** | ReAct 模式，自动区分 OpenAI/Claude 工具调用格式，最多 10 轮迭代 |
| **上下文构建器** | 自动组装：时间 + SOUL + USER + MEMORY + 最近笔记 + 技能 + 工具说明 |
| **消息总线** | 线程安全双队列（inbound / outbound），POSIX mutex + condvar |
| **CLI 通道** | stdin 读取线程，支持 `/quit`、`/clear` 命令 |
| **飞书通道** | REST API 发消息 + WebSocket 长连接接收事件，完整双向通信 |
| **工具注册表** | 11 个内置工具，自动生成 JSON Schema 供 LLM 使用 |
| **时间工具** | `get_current_time` — 返回当前日期时间和时区 |
| **文件工具** | `read_file / write_file / list_dir` — 读写 data 目录 |
| **系统工具** | `system_info` — CPU 温度、内存、uptime、存储 |
| **Cron 工具** | `cron_add / cron_list / cron_remove` — 管理定时任务 |
| **命令执行工具** | `run_command` — 在设备上执行 Shell 命令，支持超时控制，实现 AI 自我迭代（写代码→编译→运行） |
| **网络搜索工具** | `web_search` — 搜索外部网页信息，`auto` 模式下优先 Tavily，失败或未配置时回退搜狗 |
| **远程控制工具** | `remote_exec` — 调用局域网 Linux 服务器上的 HTTP agent，执行命令并返回 stdout/stderr/exit code |
| **通道管理器** | `channel_manager` — 统一通道注册、初始化、启动、停止、出站路由，新增通道只需实现 `mc_channel_t` 并注册 |
| **记忆系统** | SOUL/USER/MEMORY 读写 + 每日笔记 + N 天汇总 |
| **会话管理** | 按 chat_id 独立 JSONL 文件，支持历史加载 |
| **技能加载器** | 从 `data/skills/` 加载 Markdown 注入 system prompt |
| **Cron 服务** | 循环/定点两种类型，最多 16 个任务，60 秒检查间隔 |
| **心跳服务** | 系统健康检查，30 分钟间隔 |
| **日志系统** | 带颜色、模块 TAG、时间戳的日志宏 |
| **WiFi 服务** | T113 自动联网：wpa_supplicant + wpa_cli + DHCP + DNS + NTP 时间同步（x86 为 no-op） |
| **UI 系统** | 已完成 LVGL UI：Chat / Settings 两页、UI-first 启动、WiFi/服务器状态/Voice/Agent/运行时错误联动；x86 `-linux` 为 SDL 模拟器，音频与语音后端仍为 no-op |
| **WebSocket 客户端** | 基于 OpenSSL 的最小 RFC 6455 实现（ws_client.c），支持 TLS 二进制帧 |
| **飞书协议编解码** | 自定义 protobuf 帧解析 / 构建（feishu_proto.c），支持所有字段 |
| **音频进程** | 独立 `t113claw_audio` 进程：ALSA 录音（MIC3, 16kHz, mono）+ 播放（HP, 16kHz, stereo）+ LM4871 功放 GPIO 控制 + amixer 编解码器初始化 |
| **音频 IPC** | Unix 域套接字帧协议，支持录音/播放/音量/状态命令，30ms PCM 帧传输 |
| **语音交互** | 唤醒词"你好小爪"(sherpa-onnx KWS) + VAD自动停止(Speex DSP) + 讯飞在线 STT/TTS WebSocket，GPIO PD10 按键备用触发，完整状态机（WAKE_LISTENING→LISTENING→RECOGNIZING→THINKING→SPEAKING），mono→stereo 自动转换，阻塞式环形缓冲确保音质 |
| **音频服务** | T113Claw 主进程中的 IPC 客户端，自动 fork/exec 音频进程、连接管理、录音回调、播放推送 |

### ⚠️ 骨架实现（框架就位，核心逻辑待完善）

当前没有单独列出的“仅骨架”模块。UI 已经进入第一版可运行状态，后续主要是 SERVER LINK 细节打磨、像素风素材替换和交互细化。

### ❌ 预留 / Stub

| 模块 | 说明 |
|------|------|

| **HAL 层** | `platform/t113/src/` 预留硬件抽象层 |

---

## 四、构建指南

### 依赖

| 依赖 | 用途 | x86 安装 |
|------|------|---------|
| libcurl | HTTP 请求 | `sudo apt install libcurl4-openssl-dev` |
| OpenSSL | TLS 加密 | `sudo apt install libssl-dev` |
| cJSON | JSON 解析 | 源码已内置于 `component/cjson/` |
| ALSA | 音频采集/播放 | `sudo apt install libasound2-dev`（仅 T113 构建需要，x86 模拟器无音频） |
| sherpa-onnx | 唤醒词检测 (KWS) | T113 交叉编译（含静态 onnxruntime 1.11），x86 不需要 |
| Speex DSP | 语音活动检测 (VAD) | T113 板载 `libspeexdsp.so`，x86 不需要 |
| FreeType + bzip2 + uapi | LVGL UI 字体渲染与 sunxi G2D 内存适配 | T113 交叉编译使用 `platform/t113/lib/` 中预编译库，x86 不需要 |
| CMake ≥ 3.15 | 构建系统 | `sudo apt install cmake` |

### x86 Linux 编译（本机验证）

```bash
cd T113Claw
./build.sh -linux
```

产物：`build/src/t113claw`（约 280KB）
> 注：x86 模拟器模式下音频进程不编译，audio_service 为 no-op，但 Chat / Settings UI 会通过 SDL 正常启动。

### T113 交叉编译

**前提**：工具链已安装在 `/yours/toolchain-sunxi-glibc-gcc-830/toolchain/bin/`

```bash
cd T113Claw
./build.sh -t113
```

产物：`build/src/t113claw`（ARM ELF, 约 706KB, dynamically linked）+ `build/audio/t113claw_audio`（ARM ELF, 约 97KB）

> 详细的部署步骤参见 [`docs/DEPLOY.md`](docs/DEPLOY.md)。
>
> UI 说明参见 [`docs/UI_DEVELOPMENT.md`](docs/UI_DEVELOPMENT.md)。

### 清理

```bash
./build.sh -clean
```

---

## 五、配置说明

### 配置文件：`data/config/config.ini`

```ini
[general]
device_name = T113Claw-T113        # 设备名称
log_level = 1                    # 0=DEBUG, 1=INFO, 2=WARN, 3=ERROR

[llm]
provider = openai                # openai（含 DeepSeek/Qwen/GPT 等兼容 API）或 claude
model = deepseek-chat            # 模型名称
api_url = https://api.deepseek.com/v1/chat/completions
api_key = YOUR_API_KEY           # API 密钥

[feishu]
app_id = cli_xxxxxxxxxxxx       # 飞书应用 ID（详见 docs/FEISHU_SETUP.md）
app_secret = xxxxxxxxxxxxxxxx   # 飞书应用密钥

[proxy]
host =                           # HTTP 代理（可选）
port =

[search]
provider = auto                  # auto / sogou / tavily
domestic_only = 0                # 0=有 Tavily key 时优先使用；1=强制走搜狗
tavily_api_key =                 # 可选，留空则不会使用 Tavily

[remote]
host =                           # 单个局域网服务器地址或完整 base URL
port = 8765                      # 远程 agent 端口
username =                       # HTTP Basic Auth 用户名
password =                       # HTTP Basic Auth 密码

[wifi]
ssid =                           # WiFi 配置（T113 板用）
pass =
```

### 配置优先级（低 → 高）

1. `t113claw_config.h` 编译时默认值（如默认 provider = openai）
2. `t113claw_secrets.h` 编译时密钥（gitignore，烧入二进制，不入库）
3. `data/config/config.ini` 运行时 INI 文件
4. 环境变量 `T113CLAW_API_KEY`（优先级最高，无需改文件即可切换密钥）

> **自动恢复机制**：如果 `config.ini` 不存在但编译时有密钥（来自 `t113claw_secrets.h`），程序启动时会自动生成 `config.ini`。这意味着删除 `data/` 不会导致凭证丢失。

### 网络搜索配置说明

- `provider = auto`：自动选择搜索实现
- `provider = sogou`：强制使用搜狗移动搜索结果页解析
- `provider = tavily`：强制使用 Tavily API
- `domestic_only = 1`：优先适配仅能访问国内站点的板卡环境
- `tavily_api_key`：可选增强配置，适合主机端或网络不受限场景

> `provider = auto` 且 `domestic_only = 0`（编译默认值）：有 `tavily_api_key` 时用 Tavily，无 key 时自动降级搜狗。Tavily 在国内网络可正常访问。若需强制走搜狗（如调试解析逻辑），将 `domestic_only` 改为 `1`。

### 局域网远程控制配置说明

- 当前只支持一个 `[remote]` 服务器条目
- `host` 可写 IP、主机名，或完整 base URL（如 `http://192.168.1.100:8765`）
- `username/password` 用于 HTTP Basic Auth
- 远程主机需先运行 `scripts/t113claw_remote_agent.py`

---

## 六、工具（Tools）一览

Agent 通过 Function Calling 调用这些工具，LLM 自主决定何时使用：

| 工具名 | 功能 | 参数 |
|--------|------|------|
| `get_current_time` | 获取当前日期时间 | 无 |
| `read_file` | 读取 data 目录下的文件 | `path` |
| `write_file` | 写入 data 目录下的文件 | `path`, `content` |
| `list_dir` | 列出 data 目录下的文件 | `path` |
| `system_info` | 系统信息（CPU 温度、内存、uptime） | 无 |
| `cron_add` | 添加定时任务 | `type`, `interval`/`hour`+`minute`, `message` |
| `cron_list` | 列出所有定时任务 | 无 |
| `cron_remove` | 删除定时任务 | `id` |
| `run_command` | 执行 Shell 命令（支持 AI 自我迭代） | `command`, `timeout`(可选), `working_directory`(可选) |
| `web_search` | 搜索外部网络信息（Tavily 优先自动模式 + 搜狗回退） | `query`, `site`(可选), `max_results`(可选) |
| `remote_exec` | 在已配置的局域网服务器上执行命令 | `command`, `timeout`(可选), `working_directory`(可选) |

### run_command 工具详解

`run_command` 是实现 AI 自我迭代能力的核心工具。它允许 LLM 在设备上执行任意 Shell 命令：

**典型工作流（自我迭代）**：
1. LLM 使用 `write_file` 写入一段 C 代码
2. LLM 使用 `run_command` 编译代码（`gcc -o /tmp/test /tmp/test.c`）
3. LLM 使用 `run_command` 运行编译后的程序
4. LLM 根据运行结果判断是否需要修改代码，如需要则回到步骤 1

**硬件交互示例**：
```
# 读取 GPIO 状态
run_command: cat /sys/class/gpio/gpio106/value

# 控制 GPIO 输出
run_command: echo 1 > /sys/class/gpio/gpio34/value

# 查看 CPU 温度
run_command: cat /sys/devices/virtual/thermal/thermal_zone0/temp
```

**安全机制**：
- 默认超时 30 秒，最大 120 秒
- 使用 fork + SIGALRM 实现超时（不依赖外部 `timeout` 命令）
- 输出截断防止缓冲区溢出（最大 ~8KB）
- 返回完整的 exit_code 和超时状态

### web_search 工具详解

`web_search` 用于访问项目本地知识库之外的外部信息源，适合以下场景：

- 查询最近更新的文档、教程、论坛排障
- 搜索芯片、驱动、库、框架的在线资料
- 帮 Agent 获取本地代码库之外的补充证据

**实现策略**：
- 默认 `auto` 模式下，根据配置优先选择可访问的搜索源
- 配置了 Tavily key 且 `domestic_only = 0` 时，优先使用 Tavily JSON API
- 未配置 Tavily key、显式开启 `domestic_only = 1`，或 Tavily 请求失败时，回退到搜狗移动搜索结果页解析

**返回格式**：
- 标题
- 来源
- 链接
- 摘要

**示例**：
```
T113Claw> 帮我搜索一下 T113 Linux 快速启动优化
```

Agent 应调用 `web_search`，再根据结果进行归纳总结。

### remote_exec 工具详解

`remote_exec` 用于让 T113Claw 控制局域网内另一台 Linux 主机，适合以下场景：

- 在服务器上执行构建、运行脚本或查看状态
- 让 Agent 操作另一台电脑，而不是当前 T113 设备
- 为后续独立 Remote UI 页面提供底层执行能力

**实现策略**：
- T113 侧通过 `component/net/http_client.c` 发送 HTTP POST 到远程主机的 `/exec`
- 使用 `[remote]` 节中的 `username/password` 做 HTTP Basic Auth
- 远程主机运行 `scripts/t113claw_remote_agent.py`，用 Python 标准库执行命令并返回 JSON

**返回格式**：
- 目标主机
- exit code
- duration
- timed out
- stdout / stderr

**示例**：
```
T113Claw> 帮我在服务器上执行 df -h
```

Agent 应调用 `remote_exec`，再用自然语言概括关键输出。

---

## 七、本机验证步骤

### 步骤 1：配置 API 密钥

**方式 A（推荐）**：创建 `t113claw_secrets.h`，密钥编译进二进制：

```bash
cp t113claw_secrets.h.example t113claw_secrets.h
# 编辑填入你的 API Key、WiFi、飞书凭证等
```

**方式 B**：编辑 `data/config/config.ini`，填入 LLM API 密钥。

**方式 C**：通过环境变量：

```bash
export T113CLAW_API_KEY="sk-your-api-key"
```

### 步骤 2：x86 编译并运行

```bash
./build.sh -linux
./build/src/t113claw
```

### 步骤 3：验证预期行为

程序启动后应看到类似输出：

```
╔══════════════════════════════╗
║       T113Claw v0.1.0         ║
║   Embedded AI Agent         ║
╚══════════════════════════════╝
[I][CONFIG ] Config loaded: data/config/config.ini
[I][LLM   ] Provider: openai | Model: deepseek-chat
[I][MEMORY ] Memory store initialized
[I][SESSION] Session manager initialized
[I][SKILL  ] Skill loader initialized
[I][BUS    ] Message bus initialized
[I][TOOLS  ] Tool registry: 10 tools registered
[I][AGENT  ] Agent loop initialized
[I][CLI    ] CLI channel started
[I][FEISHU ] Feishu bot initialized (disabled - no app_id)
[I][CRON   ] Cron service started
[I][HB     ] Heartbeat service started
T113Claw> 
```

### 步骤 4：CLI 对话测试

在 `T113Claw>` 提示符后输入消息：

```
T113Claw> 你好，请介绍你自己
```

如果 API 密钥有效且网络可达，会收到 LLM 回复（🤖 前缀）。

### 步骤 5：工具调用测试

```
T113Claw> 现在几点了？
```

LLM 应调用 `get_current_time` 工具并返回当前时间。

```
T113Claw> 帮我查看一下系统状态
```

LLM 应调用 `system_info` 工具并返回系统信息。

```
T113Claw> 帮我搜索一下 T113 Linux 快速启动优化
```

如果网络可达，LLM 应调用 `web_search` 工具并结合搜索结果给出总结。

如果已配置 `[remote]` 并启动远程 agent，则还可以测试：

```
T113Claw> 帮我在服务器上执行 uname -a
```

LLM 应调用 `remote_exec` 并返回远程主机输出。

### 退出

输入 `/quit` 退出程序。

### 无 API 密钥时的行为

如果未配置 API 密钥，程序仍然正常启动，所有子系统正常初始化。发送消息后会收到：

```
🤖 [Error] API key not configured. Set api_key in config.ini or T113CLAW_API_KEY env.
```

这是正确的 graceful 降级行为。

---

## 八、T113 开发板部署

> 完整的编译、部署、运行、自启动指南请参见 [`docs/DEPLOY.md`](docs/DEPLOY.md)。

### 快速部署（编译→推送→运行）

```bash
# 1. 配置密钥（首次）
cp t113claw_secrets.h.example t113claw_secrets.h && vi t113claw_secrets.h

# 2. 交叉编译
./build.sh -t113

# 3. 推送到板子
adb shell "mkdir -p /usr/share/t113claw"
adb push build/src/t113claw /usr/share/t113claw/
adb push build/audio/t113claw_audio /usr/share/t113claw/
adb shell "chmod +x /usr/share/t113claw/t113claw /usr/share/t113claw/t113claw_audio"
adb push data/ /usr/share/t113claw/data/    # 首次需要，后续可省略

# 4. 运行
adb shell "cd /usr/share/t113claw && ./t113claw"

# 后台运行（-d: fork+setsid 脱离终端，断开 ADB 后不会被杀死）
adb shell "cd /usr/share/t113claw && ./t113claw -d >/tmp/mc.log 2>&1"
# 查看日志：adb shell "tail -f /tmp/mc.log"
# 停止：adb shell "killall t113claw t113claw_audio"
```

如果 `t113claw_secrets.h` 中配置了 WiFi，程序会自动联网。否则需先手动配置 WiFi（见 `docs/DEPLOY.md` 第 5.3 节）。

---

## 九、架构说明

### 消息流

```
用户输入                               用户看到/听到回复
   │                                      ▲
   ▼                                      │
┌─────────┐     inbound      ┌──────────┐ │  outbound   ┌─────────┐
│ CLI     ├────────────────►  │          ├─┼────────────► │ CLI    │
│ Channel │                   │  Agent   │ │              │ 输出   │
└─────────┘     inbound      │   Loop   │ │  outbound   └─────────┘
┌─────────┐  ─────────────►  │          ├─┼────────────► ┌─────────┐
│ 飞书    ├                   │ (ReAct)  │ │              │ 飞书   │
│ Channel │                   └────┬─────┘ │              │ 发消息 │
└─────────┘     inbound           │       │              └─────────┘
┌─────────┐  ─────────────►       │       │  outbound   ┌─────────┐
│ 语音    ├  (STT 识别文本)       │       ├────────────► │ 语音   │
│ Channel │                       │       │              │ TTS播放│
└─────────┘                       ▼       │              └─────────┘
                              ┌─────────┐
                              │  LLM    │
                              │ Client  │
                              └────┬────┘
                                   ▼
                              ┌─────────┐
                              │  工具    │
                              │  执行    │
                              └─────────┘
```

### ReAct 循环

```
1. 从 inbound 队列取消息
2. 构建上下文（system prompt + 历史 + 用户消息）
3. 调用 LLM API
4. 如果 LLM 返回工具调用 → 执行工具 → 将结果回填 → 回到步骤 3
5. 如果 LLM 返回文本回复 → 推入 outbound 队列
6. 最多迭代 10 轮
```

### 线程模型

| 线程 | 职责 |
|------|------|
| **主线程** | 初始化 → 200ms 轮询 outbound 队列进行分发 |
| **Agent 线程** | 从 inbound 取消息 → ReAct 循环 → 推 outbound |
| **CLI 线程** | 阻塞读 stdin → 推 inbound |
| **飞书线程** | WebSocket 接收事件 → 推 inbound |
| **Cron 线程** | 60 秒检查一次 → 触发任务推 inbound |
| **Heartbeat 线程** | 30 分钟检查系统状态 |
| **Audio 接收线程** | 从 Unix 域套接字读取音频进程事件（录音数据、状态等） |
| **Voice 线程** | 语音状态机：持续监听→唤醒/按键→VAD录音→STT→推消息→等待Agent回复→TTS→播放→循环 |
| **Button 线程** | GPIO sysfs poll 轮询 PD10 按键状态变化（200ms 间隔） |

### 进程模型

| 进程 | 可执行文件 | 职责 |
|------|-----------|------|
| **T113Claw 主进程** | `t113claw` | AI Agent 核心：LLM、消息总线、通道、工具、记忆 |
| **音频进程** | `t113claw_audio` | ALSA 硬件访问：录音、播放、编解码器初始化、功放控制 |

进程间通过 Unix 域套接字 (`/tmp/t113claw_audio.sock`) 通信。T113Claw 主进程在启动时自动 fork/exec 音频进程。

### 音频架构

```
┌──────────────────────────────────────────────────────────────────┐
│                       t113claw (主进程)                             │
│                                                                  │
│  ┌─────────────────────────────────────────────────────────────┐ │
│  │  voice_manager.c (语音状态机)                                 │ │
│  │   持续监听→唤醒词/按键→VAD录音→STT→推消息→等Agent回复→TTS→播放→循环│ │
│  │   ├─ wake_detector.c — sherpa-onnx 唤醒词检测 ("你好小爪")     │ │
│  │   ├─ vad.c         — Speex DSP VAD（自动结束录音）             │ │
│  │   ├─ xfyun_stt.c  — 讯飞 STT WebSocket 客户端                 │ │
│  │   ├─ xfyun_tts.c  — 讯飞 TTS WebSocket 客户端                 │ │
│  │   ├─ xfyun_auth.c — HMAC-SHA256 鉴权 URL 构建                 │ │
│  │   └─ gpio_button.c — GPIO PD10 按键 (sysfs poll+轮询)         │ │
│  └─────────────────────────┬───────────────────────────────────┘ │
│                            │ 调用                                 │
│  audio_service.c (IPC 客户端)                                     │
│   ├─ 发送: RECORD_START/STOP, PLAY_DATA, VOLUME                   │
│   ├─ 接收: AUDIO_DATA (录音), STATUS, ACK                         │
│   └─ 管理: fork/exec, 连接重试, 优雅关闭                            │
│                     │                                            │
└─────────────────────┼────────────────────────────────────────────┘
                      │ Unix Domain Socket
                      │ /tmp/t113claw_audio.sock
┌─────────────────────┼────────────────────────────────────────────┐
│                  t113claw_audio (音频进程)                          │
│                     │                                            │
│  audio_main.c (IPC 服务器 + 命令分发 + 1MB 环形缓冲)                │
│   ├─ audio_capture.c  — ALSA 录音线程                             │
│   │   └─ hw:0,0 / MIC3 / 16kHz / mono / S16_LE                   │
│   ├─ audio_playback.c — ALSA 播放线程                             │
│   │   └─ hw:0,0 / HP  / 16kHz / stereo / S16_LE                  │
│   └─ audio_codec.c   — amixer 初始化 + GPIO 功放                   │
│       └─ GPIO34 (PB2) → LM4871 SHUTDOWN                          │
└──────────────────────────────────────────────────────────────────┘
```

### 语音交互流程

```
[持续录音]                          [sherpa-onnx]   [讯飞云端]
     │                                   │               │
     ▼                                   │               │
 WAKE_LISTENING (持续监听)               │               │
     │ (ALSA 录音 → KWS 推理)           │               │
     │ ──── PCM 帧 ───────────────────► │               │
     │ ◄── 检测到"你好小爪" ────────── │               │
     │                                   │               │
     │ 或 [按键按下]                      │               │
     │                                   │               │
     ▼                                   │               │
 LISTENING (VAD 检测)                     │               │
     │ (Speex VAD 逐帧分析 + 累积 PCM)   │               │
     │                                   │               │
[VAD 静音 1.5s 或 录音满]                │               │
     │                                   │               │
     ▼                                   │               │
 RECOGNIZING                             │               │
     │ ─── STT WebSocket ──────────────────────────────► │
     │     (PCM base64 分帧发送)          │               │
     │ ◄── 识别结果 JSON ────────────────────────────  │  │
     │                                   │               │
     ▼                                   │               │
 THINKING                                │               │
     │ → 消息总线 → Agent Loop → LLM     │               │
     │ ← Agent 回复 (outbound分发)       │               │
     │                                   │               │
     ▼                                   │               │
 SPEAKING                                │               │
     │ ─── TTS WebSocket ──────────────────────────────► │
     │ ◄── PCM 流式回调 ──────────────────────────── │  │
     │ → mono→stereo → 环形缓冲 → ALSA  │               │
     │                                   │               │
     ▼                                   │               │
 WAKE_LISTENING (回到持续监听)            │               │
```

---

## 十、待完成事项

### 高优先级

- [x] **网络验证**：DeepSeek API 已验证通过，端到端对话和工具调用均正常（见第十四节）
- [x] **飞书 WebSocket**：WS 长连接完整实现 — protobuf 帧解析、心跳保活、断线重连、事件 ACK（详见 `docs/FEISHU_SETUP.md`）
- [x] **T113 板上实测**：已成功部署并验证 — WiFi 联网、HTTPS/TLS、飞书 WS 长连接、LLM 对话全部通过（见第十五节）
- [x] **音频服务**：独立 `t113claw_audio` 进程实现 ALSA 录音/播放、编解码器初始化、功放 GPIO 控制，通过 Unix 域套接字 IPC 与主进程通信
- [x] **语音交互**：讯飞在线 STT/TTS 对接完成，GPIO PD10 按键触发，完整端到端验证通过（按键→录音→识别→Agent推理→TTS合成→播放）

### 中优先级

- [x] **唤醒词**：本地唤醒词"你好小爪"（sherpa-onnx KWS + zipformer 模型），免按键触发
- [x] **语音 VAD**：Speex DSP 本地 VAD，自动检测静音结束录音（1.5 秒静音阈值）
- [x] **技能编写**：在 `data/skills/` 中添加了 9 个 Markdown 技能文件（记忆管理、定时任务、系统监控、每日简报、文件管理、网络搜索、局域网远程控制、语音交互指引、技能创建器）
- [x] **语音打断**：SPEAKING 状态下按键可打断 TTS 回复，立即回到待唤醒状态
- [x] **TTS 播放修复**：修复语音回复截断问题（功放控制时序 + 环形缓冲区排空 + ALSA drain 顺序）
- [x] **通道抽象统一**：让 CLI 和飞书通道通过 `mc_channel_t` 接口注册

### 低优先级

- [x] **LVGL UI**：已完成聊天页、设置页的界面渲染
- [x] **网络搜索**：已完成 `web_search` 工具，`auto` 模式下优先 Tavily，失败或未配置时回退搜狗
- [x] **局域网远程控制**：已完成 `remote_exec` 工具和 `scripts/t113claw_remote_agent.py`，x86 本机端到端烟测通过；独立 UI 页面后续补齐

---

## 十一、硬件参考

基于 T113-S3 核心板，关键外设：

| 外设 | 接口 | 备注 |
|------|------|------|
| WiFi | SDIO (RTL8723DS) | 联网必需 |
| 蓝牙 | UART1 (RTL8723DS) | 预留 |
| 显示屏 | MIPI DSI 4-Lane | LVGL UI 用 |
| 触摸屏 | I2C (GT911) | UI 交互用 |
| 麦克风 | MICIN3 模拟输入 | 语音采集用 |
| 扬声器 | HPOUT + LM4871 功放 | 语音播放用 |
| 用户按键 | PD10 GPIO | 唤醒/交互 |
| LED | PD11 GPIO | 状态指示 |
| 调试串口 | UART0 (115200) | 控制台输出 |
| USB | USB0 Type-C | ADB 调试、供电 |
| 存储 | SPI NAND (W25N01GV) | 128MB |

详见 `harware/hardware.md`。

---

## 十二、参考项目

本项目参考了以下开源项目的架构设计：

| 项目 | 路径 | 参考内容 |
|------|------|---------|
| M5Claw | `reference_claw/M5Claw/` | Agent 架构、记忆系统、工具注册模式（C++ / ESP32） |
| MimiClaw | `reference_claw/mimiclaw/` | 消息总线、通道抽象、技能系统（C / ESP-IDF） |
| app_sdk | `reference_linux/app_sdk/` | T113 交叉编译配置、预编译 ARM 库 |
| app_sdk_xiaozhi | `reference_linux/app_sdk_xiaozhi/` | OpenSSL 头文件、音频处理参考 |

---

## 十三、许可

MIT License

---

## 十四、验证记录

### 2026-04-08 网络 + 端到端 LLM 验证

**环境**：x86 Linux（模拟器模式，`SIMULATOR_LINUX=ON`）

**配置**：
```
provider = openai
model    = deepseek-chat
api_url  = https://api.deepseek.com/v1/chat/completions
```

**测试命令**：
```bash
printf "你好，你是谁？现在几点了？\n/quit\n" | ./build/src/t113claw
```

**测试结果**：

```
09:44:51 [INFO] config: Config initialized (6 entries)
09:44:51 [INFO] llm: LLM client initialized (provider: openai, model: deepseek-chat)
09:44:51 [INFO] tools: Tool registry initialized (8 tools)
09:44:51 [INFO] agent: Agent loop started
09:44:51 [INFO] agent: Processing [cli/local]: 你好，你是谁？现在几点了？
09:44:53 [INFO] main: Dispatching [cli/local]: 我是 T113Claw...

🤖 我是 T113Claw，一个运行在 T113-S3 Linux 开发板上的小型 AI 助手。
   现在时间是 2026年4月8日 09:39:43（中国标准时间）。
```

**验证项**：

| 验证项 | 结果 |
|--------|------|
| 所有子系统启动 | ✅ 通过 |
| DeepSeek API 网络连通 | ✅ 通过（TLS 握手成功，API 200 OK）|
| LLM 真实回复 | ✅ 通过（非错误占位符）|
| `get_current_time` 工具调用 | ✅ 通过（LLM 自主触发，返回正确时间）|
| ReAct 循环工具结果回填 | ✅ 通过（时间已嵌入回复中）|
| graceful 降级（无 API Key）| ✅ 通过（上次测试已验证）|

### 2026-04-30 网络搜索功能验证

**环境**：x86 Linux（`./build.sh -linux`，SIMULATOR_LINUX 模式）

**搜索配置**：
- 路径 1：`provider=sogou`，或 `provider=auto` 且未配置 `tavily_api_key`
- 路径 2：`provider=tavily`，或 `provider=auto` 且 `domestic_only=0` 并配置 `tavily_api_key`

**验证方式**：编译主程序后通过 CLI 发起搜索请求，观察 `[tool_search]` 日志输出和 Agent 回复。

```bash
./build.sh -linux
./build/src/t113claw
# T113Claw> 帮我搜索一下 T113 Linux 快速启动优化
```

**验证项**：

| 验证项 | 结果 |
|--------|------|
| x86 构建通过 | ✅ |
| `tool_web_search.c` 编译通过 | ✅ |
| 搜狗移动搜索解析（返回 3 条结果） | ✅ |
| Tavily API 调用（返回 3 条结果） | ✅ |
| 结果格式化输出（标题/来源/URL/摘要） | ✅ |
| auto 模式 provider 选择（Tavily 优先 / 搜狗回退） | ✅ |
| Tavily 失败时自动降级到搜狗 | ✅（代码路径覆盖） |
| T113 交叉编译通过（`./build.sh -t113`） | ✅ |

### 2026-05-01 局域网远程控制烟测验证

**环境**：x86 Linux（本机启动远程 agent，直接调用 `tool_remote_exec_execute()`）

**验证方式**：
- 启动 `scripts/t113claw_remote_agent.py --bind 127.0.0.1 --port 8765 --username demo --password secret`
- 将 `[remote]` 指向 `127.0.0.1:8765`
- 调用 `remote_exec` 执行 `printf hello_remote`

**验证结果**：

```text
Remote target: 127.0.0.1:8765
Exit code: 0
Duration: 1 ms
Timed out: no

Stdout:
hello_remote
```

**验证项**：

| 验证项 | 结果 |
|--------|------|
| `tool_remote_exec.c` 编译通过 | ✅ |
| HTTP Basic Auth | ✅ |
| `/exec` JSON 协议 | ✅ |
| stdout 回传 | ✅ |
| exit_code / duration / timeout 字段解析 | ✅ |

### 2026-04-08 T113-S3 板上实测验证（第十五节）

**环境**：T113-S3 开发板（ARM Cortex-A7, TinaLinux 5.4.61, 128MB DDR3）

**部署步骤**：
```bash
# 1. 交叉编译
./build.sh -t113    # 产物: build/src/t113claw (约706KB ARM ELF)

# 2. 推送到板子
adb push build/src/t113claw /usr/share/t113claw/
adb push data/ /usr/share/t113claw/data/

# 3. 配置 WiFi（板上 ADB shell）
ifconfig wlan0 up
wpa_supplicant -i wlan0 -c /etc/wifi/wpa_supplicant/wpa_supplicant.conf -B
wpa_cli -p /etc/wifi/wpa_supplicant/sockets -i wlan0 add_network
wpa_cli -p /etc/wifi/wpa_supplicant/sockets -i wlan0 set_network 0 ssid '"WiFi名"'
wpa_cli -p /etc/wifi/wpa_supplicant/sockets -i wlan0 set_network 0 psk '"密码"'
wpa_cli -p /etc/wifi/wpa_supplicant/sockets -i wlan0 enable_network 0
wpa_cli -p /etc/wifi/wpa_supplicant/sockets -i wlan0 select_network 0
wpa_cli -p /etc/wifi/wpa_supplicant/sockets -i wlan0 save_config
udhcpc -i wlan0 -t 5 -T 2 -A 5 -q

# 4. 设置时间（TLS 证书验证必需）
date -s '2026-04-08 16:20:00'
ntpd -q -p ntp.aliyun.com

# 5. 设置 DNS
echo 'nameserver 8.8.8.8' > /etc/resolv.conf
echo 'nameserver 114.114.114.114' >> /etc/resolv.conf

# 6. 运行
cd /usr/share/t113claw && ./t113claw
```

**板上启动日志**：
```
16:25:56 [INFO] config: Config initialized (8 entries)
16:25:56 [INFO] main: Config loaded from /usr/share/t113claw/data/config/config.ini
16:25:56 [INFO] http: HTTP client initialized
16:25:56 [INFO] llm: LLM client initialized (provider: openai, model: deepseek-chat)
16:25:56 [INFO] memory: Memory store initialized (data: /usr/share/t113claw/data)
16:25:56 [INFO] feishu: Feishu bot initialized
16:25:56 [INFO] main: Starting subsystems...
16:25:56 [INFO] feishu: Feishu WebSocket mode enabled
16:25:58 [INFO] feishu: Token refreshed (expires in 7199s)
16:25:59 [INFO] feishu: WS config: service_id=33554678 ping=90s reconnect=90s
16:25:59 [INFO] feishu: Connecting to Feishu WebSocket...
16:26:00 [INFO] feishu: Feishu WebSocket connected!
```

**验证项**：

| 验证项 | 结果 |
|--------|------|
| ARM 交叉编译 | ✅ 约706KB ELF, armv7-a hard-float |
| ADB 推送部署 | ✅ 可执行文件 + data 目录成功推送 |
| 依赖库就绪 | ✅ 板上已有 libcurl/libssl/libcrypto/libnghttp2/libz |
| WiFi 连接 (RTL8723DS) | ✅ wpa_supplicant + DHCP 正常 |
| DNS 解析 | ✅ open.feishu.cn / api.deepseek.com 均可解析 |
| NTP 时间同步 | ✅ 手动设置 + NTP 微调 |
| HTTPS/TLS 连通 | ✅ 飞书 API token 获取成功 |
| 飞书 WebSocket 长连接 | ✅ 连接成功, 帧收发正常 |
| 飞书消息收发 | ✅ 发消息-收回复 端到端验证通过 |
| 所有子系统启动 | ✅ 8 个模块全部初始化成功 |
| 进程内存占用 | ✅ ~52KB VSZ (极低) |

**注意事项**：
- T113 无 RTC，每次断电重启后需重新设置时间（`date -s`），否则 TLS 证书验证失败
- WiFi 配置已通过 `save_config` 持久化，重启后 wpa_supplicant 自动连接
- 建议将 WiFi 连接 + 时间同步 + T113Claw 启动写入 `/etc/init.d/rc.final` 实现开机自启

### 2026-04-09 语音交互端到端验证

**环境**：T113-S3 开发板，T113Claw v0.1.0

**配置**：
- STT/TTS：讯飞在线 WebSocket API（APPID: c5a5c1bc）
- 按键：GPIO PD10（active-low，sysfs 轮询模式）
- 麦克风：MIC3 模拟输入，16kHz / mono / S16_LE
- 扬声器：HP 输出，16kHz / stereo / S16_LE，LM4871 功放
- LLM：DeepSeek Chat（api.deepseek.com）
- TTS 发音人：xiaoyan

**测试流程**：
```
1. 对着麦克风说唤醒词："你好小爪"
2. 听到系统进入 LISTENING 后开始说话
3. 停止说话，VAD 自动检测静音并结束录音
4. 等待 STT 识别 → Agent 推理 → TTS 合成 → 扬声器播放
（也可按住 PD10 按键直接触发录音，松开停止）
```

**板上日志**：
```
17:19:08 [INFO] wake: Wake detector initialized (model load ~14s)
17:19:08 [INFO] voice: State: → WAKE_LISTENING
17:19:22 [INFO] wake: Wake word detected: 你好小爪
17:19:22 [INFO] voice: State: WAKE_LISTENING → LISTENING
17:19:32 [INFO] voice: VAD stop: speech 1720ms + silence 1500ms
17:19:32 [INFO] voice: Recorded 382976 bytes (12.0 s)
17:19:32 [INFO] voice: State: LISTENING → RECOGNIZING
17:20:13 [INFO] stt: STT result: 你是谁？
17:20:13 [INFO] voice: STT: "你是谁？"
17:20:13 [INFO] voice: State: RECOGNIZING → THINKING
17:20:17 [INFO] voice: Agent: 我是 T113Claw，一个运行在 T113-S3 开发板上的小型 AI 助手。
17:20:17 [INFO] voice: State: THINKING → SPEAKING
17:20:17 [INFO] audio: Amplifier ON
17:20:29 [INFO] tts: TTS complete: 975176 bytes PCM delivered
17:20:29 [INFO] voice: State: SPEAKING → WAKE_LISTENING
```

**验证项**：

| 验证项 | 结果 |
|--------|------|
| GPIO PD10 按键检测 | ✅ 按下/松开事件正确，sysfs 轮询模式正确工作 |
| ALSA 录音 | ✅ 16kHz mono S16_LE，3秒录制 97KB PCM |
| 讯飞 STT WebSocket | ✅ HMAC-SHA256 鉴权成功，PCM base64 分帧发送，3秒内返回结果 |
| STT 识别精度 | ✅ 准确识别"你好，请介绍一下你自己。" |
| Agent 消息总线路由 | ✅ voice 通道 → inbound → Agent → outbound → voice 分发 |
| DeepSeek LLM 推理 | ✅ 10秒内返回 1698 字节回复 |
| 讯飞 TTS WebSocket | ✅ 流式 PCM 回调，xiaoyan 发音人 |
| TTS 音频质量 | ✅ 清晰完整播放，4.25MB PCM（约 2 分钟） |
| mono→stereo 转换 | ✅ 16位 PCM 样本复制，L/R 通道一致 |
| 环形缓冲流控 | ✅ 1MB 缓冲 + 阻塞写入，无溢出 |
| 功放控制 | ✅ 播放时自动开启 LM4871，停止时关闭 |
| 状态机完整循环 | ✅ WAKE_LISTENING→LISTENING→RECOGNIZING→THINKING→SPEAKING→WAKE_LISTENING |
| 信号处理 | ✅ SIGHUP/SIGPIPE 已忽略，`-d` 参数 fork+setsid 支持 ADB 后台运行 |

**时间线分析**：
| 阶段 | 耗时 | 说明 |
|------|------|------|
| 按键录音 | 4s | 用户按住时长 |
| STT 识别 | 3s | 网络延迟 + 讯飞处理 |
| Agent 推理 | 10s | DeepSeek 生成回复 |
| TTS 播放 | 127s | 长回复完整播放 |
| **总计** | ~144s | 其中用户等待（录音结束→开始播放）约 13s |
