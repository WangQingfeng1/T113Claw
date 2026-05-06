# T113Claw

A micro AI agent running on the T113-S3 Linux development board. Chat via **CLI**, **Feishu (飞书)**, or **Voice (语音)**, with tool calling, persistent memory, scheduled tasks, and automatic WiFi connection.

- **Pure C** — ~706KB main binary + ~97KB audio binary
- **Voice Interaction** — 唤醒词"你好小爪"(sherpa-onnx) + VAD(Speex) + iFlytek STT/TTS
- **Dual LLM** — OpenAI-compatible (DeepSeek, Qwen, GPT) + Anthropic Claude
- **ReAct Agent** — Tool calling with up to 10 iterations
- **Web Search** — `auto` 模式下有 Tavily key 时优先 Tavily，失败或未配置时回退搜狗移动搜索
- **LAN Remote Control** — 通过 `remote_exec` + 本地 HTTP agent 控制一台局域网 Linux 服务器
- **LVGL UI Shell** — Chat / Settings 两页界面已接入，Settings 内含 WIFI / SERVER LINK / SYSTEM 三个子视图，T113 板端与 x86 SDL 模拟器共用同一套 UI 流程

## Architecture

```
┌───────────────────────────────────────────────────┐
│                     main.c                        │
│    init → start → outbound dispatch loop          │
├──────────┬──────────┬─────────┬───────────────────┤
│ CLI Chan │ Feishu   │ Voice   │ Cron / Heartbeat  │
│ (stdin)  │ (WS+REST)│ (STT/   │                   │
│          │          │  TTS)   │                   │
├──────────┴──────────┴─────────┤                   │
│         Message Bus           │                   │
│      [inbound/outbound]       │                   │
├───────────────────────────────┤                   │
│         Agent Loop            │                   │
│    context → LLM → tools →    │                   │
│          respond              │                   │
├───────────┬───────────────────┤                   │
│ LLM       │ Tool Registry     │                   │
│ Client    │ (11 built-in)     │                   │
├───────────┴───────────────────┤                   │
│ Memory   │ Session  │ Skills  │                   │
│ Store    │ Manager  │ Loader  │                   │
└──────────┴──────────┴─────────┴───────────────────┘
           │ IPC (Unix Socket)
┌──────────┴────────────────────────────────────────┐
│              t113claw_audio （音频进程）            │
│  ALSA capture/playback + codec init + amplifier   │
└───────────────────────────────────────────────────┘
```

## Quick Start

### Prerequisites

```bash
# Ubuntu/Debian
sudo apt-get install libcurl4-openssl-dev libssl-dev cmake build-essential
```

### Configure secrets

```bash
cp t113claw_secrets.h.example t113claw_secrets.h
# Edit t113claw_secrets.h — fill in API key, iFlytek credentials, and optionally Feishu/WiFi
```

### Build for x86 Linux (development)

```bash
./build.sh -linux
```

> Note: x86 build launches the SDL UI simulator.
> Audio and voice backends stay disabled/no-op, but the Chat / Settings UI and runtime-state bridge use the same code path as T113.

### Build for T113-S3

```bash
./build.sh -t113
# Output: build/src/t113claw (~706KB) + build/audio/t113claw_audio (~97KB)
```

### Run

```bash
./build/src/t113claw
```

Type messages at the `>` prompt. Use `/quit` to exit.

## Project Structure

```
T113Claw/
├── build.sh                  # Build script (-linux / -t113 / -clean)
├── CMakeLists.txt            # Top-level CMake
├── t113claw_config.h           # Compile-time constants
├── t113claw_secrets.h.example  # Secret template (copy to t113claw_secrets.h)
│
├── audio/                    # Audio process (separate executable)
│   ├── audio_main.c          # IPC server + ring buffer + command dispatch
│   ├── audio_capture.c/h     # ALSA recording (MIC3, 16kHz, mono)
│   ├── audio_playback.c/h    # ALSA playback (HP, 16kHz, stereo)
│   ├── audio_codec.c/h       # T113 codec init (amixer) + amplifier GPIO
│   └── audio_ipc.h           # Shared IPC protocol definitions
│
├── component/
│   ├── cjson/                # cJSON library
│   └── net/                  # HTTP + WebSocket clients
│
├── data/
│   ├── config/config.ini     # Runtime configuration
│   ├── memory/               # SOUL.md, USER.md, MEMORY.md
│   ├── sessions/             # Per-chat JSONL history
│   ├── skills/               # Skill markdown files
│   └── ui/
│       ├── image/            # UI PNG assets
│       └── font/             # UI font assets (SOURCEHANSANSCN_REGULAR.OTF)
│
├── platform/
│   ├── x86linux/             # x86 simulator toolchain
│   └── t113/                 # ARM cross-compile toolchain + prebuilt libs + LVGL porting
│
└── src/
    ├── main.c                # Entry point
    ├── agent/                # Context builder, ReAct agent loop
    ├── bus/                  # Thread-safe message bus
    ├── channels/
    │   ├── cli/              # Terminal stdin/stdout
    │   └── feishu/           # Feishu bot (WS + REST)
    ├── config/               # INI file parser
    ├── llm/                  # Multi-provider LLM client
    ├── memory/               # Persistent memory & sessions
    ├── services/             # WiFi, cron, heartbeat, audio service (IPC client)
    ├── skills/               # Skill file loader
    ├── tools/                # Tool registry & built-in tools
    ├── ui/                   # LVGL UI manager + Chat / Settings pages
    ├── voice/                # Voice interaction
    │   ├── voice_manager.c/h # State machine (WAKE→LISTEN+VAD→STT→THINK→TTS)
    │   ├── wake_detector.c/h # sherpa-onnx 唤醒词检测 (KWS)
    │   ├── vad.c/h           # Speex DSP 语音活动检测
    │   ├── xfyun_stt.c/h    # iFlytek STT WebSocket client
    │   ├── xfyun_tts.c/h    # iFlytek TTS WebSocket client
    │   ├── xfyun_auth.c/h   # HMAC-SHA256 authentication
    │   └── gpio_button.c/h  # GPIO button (sysfs polling)
    └── utils/                # Logging, helpers
```

## On-device UI

- Current pages: `Chat`, `Settings`
- Settings views: `WIFI`, `SERVER LINK`, `SYSTEM`
- Entry points: top-right settings button, Chat 顶栏下滑进入 Settings；顶栏服务器图标与 WiFi 图标一样只显示状态，不作为页面入口
- Startup order is UI-first: `main.c` 先做轻量初始化并尽早调用 `ui_manager_init()`，慢初始化放到后台 bootstrap 线程
- Runtime state is live: Chat 页接入消息流、Voice/Agent 状态和底部运行时错误字；顶栏 WiFi / 服务器图标都按真实链路状态轮询刷新
- In `platform/t113/src/porting/lv_port_disp.c`, the logical UI width is fixed to `280` after rotation, matching the proven reference layout model on the board
- Chinese text rendering uses `data/ui/font/SOURCEHANSANSCN_REGULAR.OTF` via LVGL FreeType

See [`docs/UI_DEVELOPMENT.md`](docs/UI_DEVELOPMENT.md) for secondary development guidance and [`docs/DEPLOY.md`](docs/DEPLOY.md) for deployment/runtime dependencies.

## Built-in Tools

| Tool | Description |
|------|-------------|
| `get_current_time` | Get current date and time |
| `read_file` | Read a file from the data directory |
| `write_file` | Write/overwrite a file |
| `list_dir` | List files in data directory |
| `system_info` | CPU temp, memory, storage, uptime |
| `cron_add` | Schedule a recurring or one-shot task |
| `cron_list` | List all scheduled jobs |
| `cron_remove` | Remove a scheduled job |
| `run_command` | Execute shell commands on the device |
| `web_search` | Search external web sources with Tavily-first auto mode and Sogou fallback |
| `remote_exec` | Run shell commands on the configured LAN Linux server |

## Web Search

T113Claw now includes a built-in `web_search` tool for external knowledge retrieval.

- Default provider strategy: `auto`
- Auto behavior: prefer Tavily when configured and `domestic_only=0`, otherwise use Sogou
- Built-in domestic path: 搜狗移动搜索结果页解析
- Optional enhanced path: Tavily API JSON search

Configure search via `t113claw_secrets.h` or `config.ini`:

```c
#define T113CLAW_SECRET_SEARCH_PROVIDER      "auto"
#define T113CLAW_SECRET_SEARCH_DOMESTIC_ONLY "0"
#define T113CLAW_SECRET_TAVILY_API_KEY       ""
```

See [`docs/WEB_SEARCH.md`](docs/WEB_SEARCH.md) for implementation details and deployment guidance.

## LAN Remote Control

T113Claw can control one configured LAN Linux server through the built-in `remote_exec` tool.

- Device side: `remote_exec` sends HTTP requests with Basic Auth using the existing libcurl wrapper
- Server side: run `scripts/t113claw_remote_agent.py` on the target Linux host
- Current scope: one server entry; remote commands are initiated by Agent tools, while Settings only manages server config and status

See [`docs/REMOTE_CONTROL.md`](docs/REMOTE_CONTROL.md) for setup and validation details.

## Voice Interaction

Say **"你好小爪"** (wake word) or press GPIO PD10 button to start. VAD auto-detects speech end. Full pipeline:

1. **Wake** — sherpa-onnx KWS 本地唤醒词检测 (or GPIO button)
2. **Record + VAD** — Speex DSP 语音活动检测，1.5s 静音自动停止
3. **STT** — iFlytek online WebSocket API (speech-to-text)
4. **Think** — Agent processes text through LLM
5. **TTS** — iFlytek online WebSocket API (text-to-speech)
6. **Play** — Streaming playback via t113claw_audio (stereo, LM4871 amplifier)

Configure iFlytek credentials in `t113claw_secrets.h`:
```c
#define T113CLAW_SECRET_XFYUN_APPID     "your_app_id"
#define T113CLAW_SECRET_XFYUN_APIKEY    "your_api_key"
#define T113CLAW_SECRET_XFYUN_APISECRET "your_api_secret"
```

## LLM Providers

Configured via `t113claw_config.h` or `config.ini`:

- **openai** — OpenAI-compatible APIs (GPT, DeepSeek, Qwen, etc.)
- **claude** — Anthropic Claude API

## Feishu Integration

T113Claw connects to Feishu via WebSocket long connection (device-initiated, no exposed ports). Configure `app_id` and `app_secret` in `t113claw_secrets.h`.

WebSocket receive + protobuf decode + REST send are fully implemented.

## WiFi Auto-Connection (T113)

If WiFi SSID/password are configured in `t113claw_secrets.h`, T113Claw auto-connects on startup:
1. Start wpa_supplicant
2. Configure & connect WiFi via wpa_cli
3. DHCP + DNS
4. NTP time sync (needed for TLS)

See [`docs/DEPLOY.md`](docs/DEPLOY.md) for complete deployment guide.

## Documentation

| Document | Description |
|----------|-------------|
| [`T113Claw.md`](T113Claw.md) | Full project documentation (architecture, modules, verification records) |
| [`docs/DEPLOY.md`](docs/DEPLOY.md) | Build & deploy guide for T113 |
| [`docs/UI_DEVELOPMENT.md`](docs/UI_DEVELOPMENT.md) | UI secondary development guide: structure, event flow, extension workflow |
| [`docs/AUDIO_SERVICE.md`](docs/AUDIO_SERVICE.md) | Audio subsystem: IPC protocol, ALSA config, ring buffer, codec |
| [`docs/VOICE_INTERACTION.md`](docs/VOICE_INTERACTION.md) | Voice: 唤醒词(sherpa-onnx), VAD(Speex), iFlytek STT/TTS, 状态机 |
| [`docs/FEISHU_SETUP.md`](docs/FEISHU_SETUP.md) | Feishu bot setup guide |
| [`docs/WEB_SEARCH.md`](docs/WEB_SEARCH.md) | Web search design, providers, config, and validation |

## License

MIT
