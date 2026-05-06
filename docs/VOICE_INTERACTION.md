# T113Claw 语音交互技术文档

> 本文档详细描述 T113Claw 语音交互子系统的架构设计、讯飞 WebSocket 协议、状态机、GPIO 按键和配置方法。
> 适用于二次开发人员快速理解和修改语音交互功能。

---

## 一、架构概述

T113Claw 语音交互实现了完整的 **唤醒词→反馈→录音→识别→推理→合成→播放** 闭环：

```
[持续监听] → [唤醒词/按键] → ["您好，我在听您说"反馈] → [录音+VAD+流式STT] → [Agent推理] → [讯飞 TTS] → [扬声器播放]
```

### v2 优化亮点

| 优化项 | v1 行为 | v2 行为 |
|--------|---------|--------|
| STT 识别 | 录完→连接→逐帧发送→等结果 (延迟 ~11s) | 边录边发，录完即出结果 (延迟 ~1s) |
| 唤醒反馈 | 无反馈，用户不知何时开口 | 播放缓存的"您好，我在听您说"提示音 |
| 按键模式 | 按键仅触发，VAD 决定停止 | **Push-to-Talk**：松手即停 |
| 录音隔离 | 录音不停，唤醒词尾污染 STT | 唤醒后停录→播提示→重新录音 |
| VAD 稳定性 | 无保护期，易误触发 | 500ms grace period，1.0s 静音阈值 |

### 技术选型

| 组件 | 方案 | 说明 |
|------|------|------|
| 唤醒词检测 (KWS) | sherpa-onnx 本地推理 | zipformer-wenetspeech-3.3M int8 模型，唤醒词"你好小爪" |
| 语音活动检测 (VAD) | Speex DSP 本地 | `speex_preprocess_run()` + 静音/语音时长跟踪 |
| 语音识别 (STT) | 讯飞在线 WebSocket API | `wss://iat-api.xfyun.cn/v2/iat` |
| 语音合成 (TTS) | 讯飞在线 WebSocket API | `wss://tts-api.xfyun.cn/v2/tts` |
| 鉴权方式 | HMAC-SHA256 | RFC1123 日期签名 |
| 触发方式 | 唤醒词 + GPIO PD10 | 语音或按键触发，双模式 |
| 音频格式 | PCM S16_LE 16kHz | 录音 mono，播放 stereo |
| 音频服务 | t113claw_audio 进程 | Unix 域套接字 IPC |

### 进程内架构

语音交互模块运行在 **t113claw 主进程内**（不是独立进程），原因：
1. 语音管理器是编排层（非硬件驱动），不需要进程隔离
2. t113claw_audio 仅支持单客户端连接，额外进程无法直接通信
3. 消息总线集成更紧密，无需额外 IPC 开销
4. 清晰的头文件接口已提供足够的模块解耦

---

## 二、源文件结构

```
src/voice/
├── voice_manager.c/h   # 语音状态机（核心编排）
├── wake_detector.c/h   # sherpa-onnx 唤醒词检测（KWS）
├── vad.c/h             # Speex DSP 语音活动检测
├── xfyun_auth.c/h      # 讯飞 HMAC-SHA256 鉴权
├── xfyun_stt.c/h       # 讯飞 STT WebSocket 客户端
├── xfyun_tts.c/h       # 讯飞 TTS WebSocket 客户端
└── gpio_button.c/h     # GPIO 按键检测
```

---

## 三、状态机

### 3.1 状态定义

```
VOICE_WAKE_LISTENING  持续录音→唤醒词检测（或等待按键）
     │
     ▼ [唤醒词"你好小爪" 或 按键按下]
     │  ── 停止录音 ──
     │  ── 播放缓存唤醒提示 "您好，我在听您说" ──
     │  ── 重新开始录音（干净缓冲区）──
     │  ── 同时打开 STT 流式连接 ──
VOICE_LISTENING       录音 + VAD(带500ms grace) + 流式STT发送
     │                (按键模式: 松手即停; 唤醒词模式: VAD 1.0s静音停)
     ▼ [停止条件触发]
VOICE_RECOGNIZING     等待 STT 流式结果（数据已在录音期间发送）
     │
     ▼ [识别完成]
VOICE_THINKING        等待 Agent 回复（最多 30 秒）
     │
     ▼ [收到回复]
VOICE_SPEAKING        TTS 合成 + 播放中
     │
     ▼ [播放完成]
VOICE_WAKE_LISTENING  回到持续监听状态
```

### 3.2 线程模型

| 线程 | 函数 | 职责 |
|------|------|------|
| **Voice 线程** | `voice_thread()` | 主状态机循环：持续监听→唤醒/按键→VAD录音→STT→等待→TTS→播放 |
| **Button 线程** | `poll_thread()` | GPIO 轮询：200ms 间隔读取按键状态 |
| **Audio 接收线程** | `audio_data_cb()` | 录音回调：WAKE_LISTENING 时喂给唤醒词检测器，LISTENING 时喂给 VAD(带 grace period) + 累积 PCM |
| **Main 线程** | `dispatch_outbound()` | 调用 `voice_manager_on_response()` 传递 Agent 回复 |

### 3.3 同步机制

| 同步原语 | 用途 |
|---------|------|
| `s_evt_mutex` + `s_evt_cond` | Voice 线程等待唤醒词触发/按键按下(s_btn_wake_event持久标志)/VAD 静音结束 |
| `s_resp_mutex` + `s_resp_cond` | Voice 线程等待 Agent 回复（30 秒超时） |
| `s_pcm_mutex` | 保护录音缓冲区（回调写 + 主线程读） |

### 3.4 异常处理

| 场景 | 处理方式 |
|------|---------|
| 录音 < 100ms (3200 字节) | 警告日志，忽略，回到 WAKE_LISTENING |
| STT 返回空文本 | 警告日志，忽略，回到 WAKE_LISTENING |
| Agent 超时 (30s) | `pthread_cond_timedwait` 超时，日志警告，回到 WAKE_LISTENING |
| STT 连接失败 | 错误日志，回到 WAKE_LISTENING |
| TTS 连接失败 | 错误日志，回到 WAKE_LISTENING |
| 录音缓冲区满 | 截断（最多 30 秒） |
| 唤醒词检测器初始化失败 | voice_manager_init 返回 -1，语音功能不启动 |

---

## 四、唤醒词检测 (wake_detector)

### 4.1 技术方案

使用 **sherpa-onnx** 流式关键词检测 (KWS)，基于 zipformer-wenetspeech-3.3M-2024-01-01 模型。

| 参数 | 值 | 说明 |
|------|------|------|
| 唤醒词 | "你好小爪" | 拼音音素序列: `n ǐ h ǎo x iǎo zh uǎ` |
| 模型 | int8 量化 | encoder ~4.6MB + decoder ~180KB + joiner ~64KB |
| 推理线程 | 2 | `num_threads=2`（T113 双核 Cortex-A7）|
| 关键词分数 | 3.0 | `keywords_score=3.0f` |
| 检测阈值 | 0.25 | `keywords_threshold=0.25f` |
| 采样率 | 16000 Hz | S16_LE mono → float32 转换 |

### 4.2 工作流程

1. WAKE_LISTENING 状态下持续录音，PCM 数据通过 `audio_data_cb()` 回调到达
2. 回调中调用 `wake_detector_feed_pcm()` — 将 S16 转换为 float32，送入 sherpa-onnx 流
3. 调用 `SherpaOnnxDecodeKeywordStream()` 进行解码
4. 若 `SherpaOnnxGetKeywordResult()` 返回非空关键词，设置 `s_wake_detected=1` 并唤醒 Voice 线程
5. Voice 线程切换到 LISTENING 状态，开始 VAD 检测录音

### 4.3 模型文件部署

模型需部署到板上 `/usr/share/t113claw/kws-model/` 目录：

```
kws-model/
├── encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx  # 编码器 (~4.6MB)
├── decoder-epoch-12-avg-2-chunk-16-left-64.onnx       # 解码器 (~180KB)
├── joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx   # 连接器 (~64KB)
├── tokens.txt                                          # 音素词表
└── keywords_t113claw.txt                                 # 自定义关键词
```

### 4.4 自定义唤醒词

编辑 `keywords_t113claw.txt`，格式为音素序列 + `@关键词名`：

```
n ǐ h ǎo x iǎo zh uǎ @你好小爪
```

音素可在 `tokens.txt` 中查找。多个唤醒词每行一个。

---

## 五、语音活动检测 (VAD)

### 5.1 技术方案

使用 **Speex DSP** 的 `speex_preprocess_run()` 进行语音活动检测。

| 参数 | 值 | 说明 |
|------|------|------|
| 帧大小 | 320 样本 (20ms) | `vad_process()` 每次处理一帧 |
| 采样率 | 16000 Hz | 匹配录音格式 |
| VAD 概率启动 | 80% | `SPEEX_PREPROCESS_SET_PROB_START` |
| VAD 概率持续 | 65% | `SPEEX_PREPROCESS_SET_PROB_CONTINUE` |
| 降噪 | -15 dB | 辅助 VAD 判断 |
| 静音停止阈值 | 1000 ms | 连续静音 1.0 秒停止录音 (v2 由 1.5s 降至 1.0s) |
| 最小语音时长 | 300 ms | 至少 300ms 语音才生效 |
| VAD grace period | 500 ms | 录音开始后前 500ms 忽略 VAD 判断，避免唤醒残留误触发 |

### 5.2 工作流程

1. LISTENING 状态下，`audio_data_cb()` 先检查 grace period（前 500ms 跳过 VAD 检测）
2. grace period 之后，将 PCM 按 320 样本逐帧喂给 `vad_process()`
3. `vad_process()` 返回 1(语音) / 0(静音)，内部累计连续语音/静音帧数
4. 唤醒词模式：当 `vad_speech_duration_ms() >= 300 && vad_silence_duration_ms() >= 1000` 时，信号唤醒 Voice 线程
5. 按键模式 (PTT)：用户松开按键即停止，不依赖 VAD
6. Voice 线程停止录音，发送 STT 结束帧，等待流式识别结果

---

## 六、讯飞 WebSocket 协议

### 6.1 鉴权机制 (xfyun_auth)

讯飞 WebSocket API 使用 URL 参数鉴权，基于 HMAC-SHA256 签名。

**构建步骤**：

```
1. 解析 base_url → host + path
   例: "wss://iat-api.xfyun.cn/v2/iat" → host="iat-api.xfyun.cn", path="/v2/iat"

2. 生成 RFC1123 UTC 日期
   格式: "Wed, 09 Apr 2026 12:34:56 GMT"

3. 构造签名原文:
   "host: iat-api.xfyun.cn\ndate: Wed, 09 Apr 2026 12:34:56 GMT\nGET /v2/iat HTTP/1.1"

4. HMAC-SHA256(签名原文, api_secret) → 32 字节摘要

5. Base64(摘要) → signature

6. 构造 authorization_origin:
   'api_key="<key>", algorithm="hmac-sha256", headers="host date request-line", signature="<sig>"'

7. Base64(authorization_origin) → authorization

8. URL 编码 date 和 authorization

9. 最终 URL:
   "{base_url}?authorization={auth_enc}&date={date_enc}&host={host}"
```

**时间要求**：服务端允许最多 **300 秒时钟偏差**，板子必须正确同步时间（NTP）。

**依赖库**：OpenSSL（HMAC、Base64）

### 6.2 STT 协议 (xfyun_stt)

**Endpoint**: `wss://iat-api.xfyun.cn/v2/iat`

#### 请求（客户端 → 服务端）

PCM 数据分帧发送，每帧 1280 字节（40ms @ 16kHz mono S16_LE），帧间隔 40ms。

**第一帧** (status=0)：
```json
{
  "common": { "app_id": "<APPID>" },
  "business": {
    "language": "zh_cn",
    "domain": "iat",
    "accent": "mandarin",
    "vad_eos": 3000,
    "ptt": 1
  },
  "data": {
    "status": 0,
    "format": "audio/L16;rate=16000",
    "encoding": "raw",
    "audio": "<base64-PCM>"
  }
}
```

**中间帧** (status=1)：
```json
{
  "data": { "status": 1, "audio": "<base64-PCM>" }
}
```

**最后帧** (status=2)：
```json
{
  "data": { "status": 2 }
}
```

#### 业务参数说明

| 字段 | 值 | 说明 |
|------|------|------|
| `language` | `zh_cn` | 中文（简体） |
| `domain` | `iat` | 语音听写 |
| `accent` | `mandarin` | 普通话 |
| `vad_eos` | `3000` | 端点检测：静音 3 秒自动结束 |
| `ptt` | `1` | 返回标点符号 |

#### 响应（服务端 → 客户端）

```json
{
  "code": 0,
  "message": "OK",
  "data": {
    "status": 2,
    "result": {
      "ws": [
        { "cw": [ { "w": "你好" }, { "w": "世界" } ] }
      ]
    }
  }
}
```

- `data.status == 2` 表示识别完成
- 文本从 `data.result.ws[].cw[].w` 层级拼接
- `code != 0` 为错误，参见讯飞错误码文档

### 6.3 TTS 协议 (xfyun_tts)

**Endpoint**: `wss://tts-api.xfyun.cn/v2/tts`

#### 请求（单帧，一次性发送全部文本）

```json
{
  "common": { "app_id": "<APPID>" },
  "business": {
    "aue": "raw",
    "auf": "audio/L16;rate=16000",
    "vcn": "xiaoyan",
    "speed": 50,
    "volume": 80,
    "pitch": 50,
    "tte": "UTF8"
  },
  "data": {
    "status": 2,
    "text": "<base64-编码的文本>"
  }
}
```

#### 业务参数说明

| 字段 | 值 | 说明 |
|------|------|------|
| `aue` | `raw` | 返回原始 PCM（无编码） |
| `auf` | `audio/L16;rate=16000` | S16_LE PCM, 16kHz |
| `vcn` | `xiaoyan` | 发音人（可选：xiaoyan, xiaofeng, xiaoqi 等） |
| `speed` | `50` | 语速 0-100（50=正常） |
| `volume` | `80` | 音量 0-100 |
| `pitch` | `50` | 音调 0-100（50=正常） |
| `tte` | `UTF8` | 文本编码 |

#### 响应（多帧流式返回）

```json
{
  "code": 0,
  "data": {
    "audio": "<base64-PCM-chunk>",
    "status": 2
  }
}
```

- `data.audio`：Base64 编码的 PCM 音频块
- `data.status == 2`：合成完成
- 每帧解码后通过回调即时传递，实现流式播放

### 6.4 数据格式转换

讯飞 TTS 返回 **mono** 16kHz PCM，但 T113 播放设备需要 **stereo**。

**转换逻辑**：
```c
/* 每个 mono 样本复制到左右声道 */
for (size_t i = 0; i < n_samples; i++) {
    dst[i * 2]     = src[i]; /* L */
    dst[i * 2 + 1] = src[i]; /* R */
}
/* 输入: N 字节 mono → 输出: 2N 字节 stereo */
```

使用 128KB 静态缓冲区避免频繁堆分配。

---

## 七、GPIO 按键

### 7.1 硬件连接

| 参数 | 值 | 说明 |
|------|------|------|
| GPIO 引脚 | PD10 | T113-S3 用户按键 |
| Linux GPIO 编号 | 106 | PD10 = 3×32 + 10 = 106 |
| 电平逻辑 | active-low | 按下=0，松开=1 |
| 消抖时间 | 50ms | 软件消抖 |

### 7.2 sysfs 操作

```bash
# 导出 GPIO
echo 106 > /sys/class/gpio/export

# 设置方向为输入
echo in > /sys/class/gpio/gpio106/direction

# 设置边沿检测（中断触发）
echo both > /sys/class/gpio/gpio106/edge

# 读取值（0=按下，1=松开）
cat /sys/class/gpio/gpio106/value
```

### 7.3 检测机制

采用 **poll + 轮询双模式**：

```c
while (running) {
    poll(&pfd, 1, 200);   /* 200ms 超时 */
    
    /* 无论 poll 是否检测到边沿事件，都读取 GPIO 值 */
    /* 这确保即使 GPIO 中断不支持也能正常工作 */
    int val = gpio_read_value(fd);
    
    if (val != last_val) {
        /* 50ms 消抖 */
        debounce_and_callback(val);
    }
}
```

**为什么不纯用 poll？**
T113-S3 的 PD 端口可能不支持 GPIO 中断（sunxi pinctrl 限制），`poll()` 只会超时返回。轮询读取作为兜底确保在所有平台上工作。

---

## 八、配置

### 8.1 讯飞凭证

在 `t113claw_secrets.h` 中配置（编译时烧入）：

```c
#define T113CLAW_SECRET_XFYUN_APPID     "your_app_id"
#define T113CLAW_SECRET_XFYUN_APIKEY    "your_api_key"
#define T113CLAW_SECRET_XFYUN_APISECRET "your_api_secret"
```

**获取方式**：
1. 注册 [讯飞开放平台](https://www.xfyun.cn/)
2. 创建应用
3. 开通「语音听写」和「在线语音合成」服务
4. 从控制台获取 APPID、APIKey、APISecret

### 8.2 其他配置

在 `t113claw_config.h` 中：

```c
#define T113CLAW_BUTTON_GPIO    106       /* GPIO 引脚编号 */
#define T113CLAW_TTS_VOICE      "xiaoyan" /* TTS 发音人 */
```

### 8.3 可选 TTS 发音人

| 发音人 | 语言 | 性别 | 说明 |
|--------|------|------|------|
| xiaoyan | 中文 | 女 | 默认，标准普通话 |
| xiaofeng | 中文 | 男 | 标准普通话 |
| xiaoqi | 中文 | 女 | 温柔女声 |
| xiaohai | 中文 | 男 | 标准男声 |
| xiaomei | 粤语 | 女 | 粤语女声 |

更多发音人详见[讯飞 TTS 文档](https://www.xfyun.cn/doc/tts/online_tts/API.html)。

---

## 九、API 参考

### 9.1 Voice Manager

```c
#include "voice/voice_manager.h"

/* 初始化（加载凭证，分配缓冲区） */
int voice_manager_init(void);

/* 启动（按键监听 + 状态机线程） */
int voice_manager_start(void);

/* 停止（优雅关闭所有线程） */
void voice_manager_stop(void);

/* 传递 Agent 回复（从主线程调用） */
void voice_manager_on_response(const char *text);
```

### 9.2 STT

```c
#include "voice/xfyun_stt.h"

/* 初始化（存储凭证） */
int xfyun_stt_init(const char *app_id, const char *api_key, const char *api_secret);

/* 批量识别（阻塞调用，PCM 输入 → UTF-8 文本输出） */
int xfyun_stt_recognize(const uint8_t *pcm, size_t pcm_len,
                         char *out_text, size_t text_size);

/* ── 流式 STT API (v2 新增) ── */

/* 打开流式会话（连接 WebSocket） */
xfyun_stt_stream_t *xfyun_stt_stream_open(void);

/* 发送一段 PCM (is_last=1 表示最后一段) */
int xfyun_stt_stream_send(xfyun_stt_stream_t *s,
                            const uint8_t *pcm, size_t len, int is_last);

/* 等待并获取最终识别结果（阻塞） */
int xfyun_stt_stream_finish(xfyun_stt_stream_t *s,
                              char *out_text, size_t text_size);

/* 关闭并释放流式会话 */
void xfyun_stt_stream_close(xfyun_stt_stream_t *s);

/* 清理 */
void xfyun_stt_cleanup(void);
```

### 9.3 TTS

```c
#include "voice/xfyun_tts.h"

/* 音频回调：每次收到一块 PCM 数据时调用 */
typedef void (*tts_audio_cb_t)(const uint8_t *pcm, size_t len, void *user);

/* 初始化（存储凭证） */
int xfyun_tts_init(const char *app_id, const char *api_key, const char *api_secret);

/* 合成（阻塞调用，文本 → PCM 流式回调） */
int xfyun_tts_synthesize(const char *text, const char *vcn,
                          tts_audio_cb_t cb, void *user);

/* 清理 */
void xfyun_tts_cleanup(void);
```

### 9.4 鉴权

```c
#include "voice/xfyun_auth.h"

/* 构建讯飞鉴权 URL */
int xfyun_build_auth_url(const char *base_url,
                          const char *api_key, const char *api_secret,
                          char *out_buf, size_t buf_size);
```

### 9.5 GPIO 按键

```c
#include "voice/gpio_button.h"

/* 按键回调：按下 pressed=1, 松开 pressed=0 */
typedef void (*gpio_button_cb_t)(int pressed, void *user);

/* 启动按键监听 */
int gpio_button_start(int gpio_num, gpio_button_cb_t cb, void *user);

/* 停止 */
void gpio_button_stop(void);
```

---

## 十、消息总线集成

语音交互通过消息总线与 Agent 通信：

```
                    ┌──────────────────┐
[用户说话]          │                  │
     ↓              │    Message Bus   │
voice_thread        │                  │
  STT 识别 → text   │  ┌────────────┐  │
     ↓              │  │ inbound Q  │  │
push_inbound() ────►│  │ channel:   │  │
  channel="voice"   │  │  "voice"   │  │
  chat_id=          │  └─────┬──────┘  │
  "voice/local"     │        ↓         │
                    │  Agent Loop      │
                    │    (ReAct)       │
                    │        ↓         │
                    │  ┌────────────┐  │
                    │  │ outbound Q │  │
                    │  │ channel:   │  │
                    │  │  "voice"   │  │
                    │  └─────┬──────┘  │
                    │        ↓         │
dispatch_outbound()─┤                  │
  voice_manager_    │                  │
  on_response(text) │                  │
     ↓              └──────────────────┘
voice_thread 被唤醒
  TTS 合成 → 播放
```

**通道标识**：`T113CLAW_CHAN_VOICE = "voice"`（定义在 `bus/message_bus.h`）

---

## 十一、完整数据流

### 唤醒阶段

```
audio_service_record_start() → 持续录音（16kHz mono S16_LE）
     ↓
t113claw_audio: ALSA snd_pcm_readi() → IPC EVT_AUDIO_DATA (960B/30ms)
     ↓
audio_data_cb() [state=WAKE_LISTENING]:
     wake_detector_feed_pcm(pcm, n_samples)
       → S16→float32 → SherpaOnnxOnlineStreamAcceptWaveform()
       → SherpaOnnxDecodeKeywordStream()
       → SherpaOnnxGetKeywordResult() 检测到 "你好小爪"
     ↓
     signal(s_evt_cond), s_wake_detected = 1

或：用户按下 GPIO 按键 → button_cb(pressed=1) → s_btn_wake_event=1 → signal(s_evt_cond)
     ↓
voice_thread: wait_wake_event() 返回 (woke_by_btn=0|1)
     ↓
audio_service_record_stop()  ← 【v2关键】停止录音，防止唤醒词尾音污染STT
     ↓
play_wake_prompt()  ← 播放缓存的"您好，我在听您说"提示音（~1.5s）
```

### 录音 + 流式STT 阶段

```
vad_reset(); s_pcm_len = 0;
xfyun_stt_stream_open()  ← 建立 STT WebSocket 连接
audio_service_record_start()  ← 重新开始录音（干净缓冲区）
     ↓
audio_data_cb() [state=LISTENING]:
     (前500ms) 仅累积PCM，跳过VAD检测 ← 【v2 grace period】
     (500ms后) vad_process(frame, 320) → 逐帧 VAD 检测
     同时：追加 PCM 到 s_pcm_buf (最大 960KB / 30秒)
     ↓
voice_thread 并行循环:
     ├─ 每积累 1280B PCM → xfyun_stt_stream_send() ← 【v2流式发送】
     ├─ 按键模式(PTT): 检查 s_btn_pressed==0 → 立即停止
     └─ 唤醒词模式: 检查 vad_speech >= 300ms && vad_silence >= 1000ms → 停止
     ↓
audio_service_record_stop()
xfyun_stt_stream_send(remaining, is_last=1)  ← 发送剩余数据+结束帧
```

### STT 阶段

```
voice_thread: set_state(RECOGNIZING)
     ↓
xfyun_stt_stream_finish(stream, stt_text, 2048)
     ↓
接收响应 JSON: 解析 data.result.ws[].cw[].w 拼接文本
     ↓
data.status==2 → 识别完成（因数据已流式发送，服务端几乎实时处理，延迟极低）
     ↓
xfyun_stt_stream_close(stream)
```

### 推理阶段

```
voice_thread: set_state(THINKING)
     ↓
message_bus_push_inbound(channel="voice", content=stt_text)
     ↓
agent_loop: 取消息 → context_builder → LLM API 调用
     ↓
（等待 LLM 返回，可能包含多轮工具调用）
     ↓
agent_loop: 推 outbound (channel="voice")
     ↓
main loop: dispatch_outbound()
     ↓
voice_manager_on_response(text) → 设置 s_response_text → signal condvar
     ↓
voice_thread: pthread_cond_timedwait() 被唤醒（或 30 秒超时）
```

### TTS + 播放阶段

```
voice_thread: set_state(SPEAKING)
     ↓
audio_service_play_start() → IPC PLAY_START → t113claw_audio
     ↓
xfyun_tts_synthesize(text, "xiaoyan", tts_audio_cb, NULL)
     ↓
xfyun_build_auth_url() → 构造 wss:// 鉴权 URL
     ↓
ws_connect(auth_url) → TLS WebSocket 握手
     ↓
发送单帧 JSON: common + business + data(text=base64)
     ↓
循环接收响应:
  ├─ 解析 JSON → base64_decode(data.audio) → PCM
  ├─ tts_audio_cb(pcm, len, NULL)
  │   ├─ mono → stereo 转换（复制每个样本到 L/R）
  │   └─ audio_service_play_pcm(stereo_buf, stereo_len)
  │       └─ IPC PLAY_DATA → t113claw_audio 环形缓冲
  │           └─ ALSA snd_pcm_writei() → 功放 → 扬声器
  └─ data.status==2 → 合成完成
     ↓
audio_service_play_stop() → IPC PLAY_STOP → t113claw_audio 停止播放
     ↓
set_state(WAKE_LISTENING) → 回到持续监听，等待下次唤醒
```

---

## 十二、性能参数

| 指标 | v1 实测 | v2 优化后 | 说明 |
|------|---------|----------|------|
| 按键响应延迟 | < 200ms | < 200ms | sysfs 轮询间隔 |
| 唤醒反馈延迟 | 无反馈 | ~1.5s 提示音 | 预缓存"您好，我在听您说"，即时播放 |
| STT 送数延迟 | ~5-6s (录完再送) | ~0s (实时流式) | 边录边发，省去串行发送时间 |
| STT 识别延迟 (含送数) | ~8-10s | ~1-2s | 流式模式下结束帧后快速出结果 |
| Agent 推理延迟 | ~10s | ~10s | 依赖 LLM API |
| TTS 首音延迟 | ~1s | ~1s | 从 TTS 请求到首个 PCM 块 |
| VAD 静音停止 | 1.5s | 1.0s | 更快响应 |
| 播放吞吐量 | 64 KB/s | 64 KB/s | 16kHz stereo S16_LE |
| 录音缓冲上限 | 30 秒 / 960 KB | 30 秒 / 960 KB | 超过截断 |
| 环形缓冲大小 | 1 MB | 1 MB | 约 16 秒立体声音频 |
| 内存占用 | ~2.2 MB | ~2.3 MB | +96KB 唤醒提示音缓存 |

---

## 十三、故障排查

| 症状 | 可能原因 | 解决方案 |
|------|---------|---------|
| 按键无反应 | GPIO 编号错误 | 检查 `T113CLAW_BUTTON_GPIO` 和实际硬件 |
| 按键无反应 | GPIO 未导出 | `ls /sys/class/gpio/gpio106/` 确认 |
| STT 连接失败 | 时间不准 | `date -s` 或 NTP 同步，允许 300 秒偏差 |
| STT 错误 10407 | 接口未开通 | 讯飞控制台开通「语音听写」服务 |
| STT 错误 10105 | APPID 错误 | 检查 `t113claw_secrets.h` 中的凭证 |
| STT 返回空 | 说话音量太小 | 调大 MIC3 增益，靠近麦克风 |
| STT 返回空 | 唤醒词尾音污染录音(v1) | v2 已修复：唤醒后停录→播提示→重新录 |
| 唤醒后误停 | VAD 误触发(v1) | v2 已修复：500ms grace period + 1.0s 静音阈值 |
| TTS 无声音 | 功放未开启 | 检查 GPIO34 (PB2) 和 LM4871 连接 |
| TTS 杂音 | 缓冲区溢出 | 确认 PLAY_RING_SIZE ≥ 1MB |
| TTS 杂音 | mono/stereo 不匹配 | TTS 输出必须做 mono→stereo 转换再播放 |
| Agent 超时 | LLM API 慢 | 检查网络连接和 API 配额 |
| "Voice manager start failed" | GPIO 权限 | 用 root 运行 或设置 gpio 组权限 |

---

## 十四、二次开发指南

### 14.1 替换 STT/TTS 服务商

如需替换讯飞为其他服务商（百度、阿里等）：

1. 新建 `xfyun_stt.c` 的替代文件（如 `baidu_stt.c`）
2. 实现相同的接口：
   ```c
   int xxx_stt_init(const char *app_id, ...);
   int xxx_stt_recognize(const uint8_t *pcm, size_t len, char *out, size_t size);
   ```
3. 在 `voice_manager.c` 中替换 `#include` 和初始化调用
4. TTS 同理，保持回调接口不变

### 14.2 唤醒词检测实现说明

已使用 **sherpa-onnx** 实现本地唤醒词检测（见第四节）：

- 模型：zipformer-wenetspeech-3.3M int8 量化
- 唤醒词："你好小爪"（`keywords_t113claw.txt`）
- 修改唤醒词：编辑 `data/kws-model/keywords_t113claw.txt`，音素可查 `tokens.txt`
- 添加新唤醒词：在文件中追加一行 `音素序列 @新关键词`
- 调整灵敏度：修改 `wake_detector.c` 中 `keywords_score`（越大越严格）和 `keywords_threshold`（越小越敏感）

### 14.3 VAD 参数调优

已使用 **Speex DSP** 实现 VAD（见第五节）。可调参数在 `voice_manager.c`：

| 参数 | 默认值 | 说明 |
|------|--------|------|
| `VAD_SILENCE_STOP_MS` | 1000 | 连续静音多久后停止录音（毫秒，v2 由 1500 降至 1000）|
| `VAD_MIN_SPEECH_MS` | 300 | 最少累计语音时长才生效（毫秒）|
| `VAD_GRACE_MS` | 500 | 录音开始后忽略 VAD 的时间（毫秒，v2 新增，防误触发）|
| `LISTEN_TIMEOUT_MS` | 15000 | 最长录音等待时间（毫秒，v2 由 10000 增至 15000）|

VAD 概率阈值在 `vad.c` 中：`PROB_START=80%`, `PROB_CONTINUE=65%`

### 14.4 修改 GPIO 按键引脚

1. 修改 `t113claw_config.h` 中 `T113CLAW_BUTTON_GPIO` 的值
2. 如果新引脚支持中断，`poll()` 的 `POLLPRI` 会自动工作
3. 如果不支持中断，轮询模式（200ms 间隔）自动兜底
4. 需确认新引脚的电平逻辑（active-low vs active-high），修改 `gpio_button.c` 中 `pressed = (val == 0)` 逻辑

### 14.5 修改 TTS 参数

在 `xfyun_tts.c` 的 JSON 请求中修改：

```c
"\"speed\":%d,"    /* 语速 0-100 */
"\"volume\":%d,"   /* 音量 0-100 */
"\"pitch\":%d,"    /* 音调 0-100 */
```

或提取为配置参数传入 `xfyun_tts_synthesize()`。
