# T113Claw 音频服务技术文档

> 本文档详细描述 T113Claw 音频子系统的架构设计、IPC 协议、API 接口和部署配置。
> 适用于二次开发人员快速理解和修改音频功能。

---

## 一、架构概述

T113Claw 音频子系统采用 **独立进程 + IPC 通信** 架构：

| 组件 | 可执行文件 | 职责 |
|------|-----------|------|
| **音频进程** | `t113claw_audio` | ALSA 硬件访问：录音、播放、编解码器初始化、功放控制 |
| **音频服务** | `t113claw` 内 `audio_service.c` | IPC 客户端：fork/exec 音频进程、发送命令、接收录音数据 |

### 为什么分离进程？

1. **硬件隔离**：ALSA 操作可能阻塞或崩溃，不影响主进程 Agent 逻辑
2. **重启能力**：音频进程崩溃后主进程可重新 fork
3. **权限隔离**：音频进程可独立设置 GPIO/ALSA 权限
4. **开发便捷**：可单独调试音频进程

### 进程间通信

```
┌────────────────────────┐         ┌────────────────────────┐
│    t113claw (主进程)      │         │  t113claw_audio (音频)   │
│                         │         │                        │
│  audio_service.c ◄──────┼── IPC ──┼───► audio_main.c      │
│  (IPC 客户端)           │  Socket │   (IPC 服务器)         │
│                         │         │                        │
│  发送: CMD_RECORD_START │         │  接收: CMD → 分发执行   │
│  发送: CMD_PLAY_DATA    │         │  发送: EVT_AUDIO_DATA  │
│  接收: EVT_AUDIO_DATA   │         │  发送: EVT_ACK         │
└────────────────────────┘         └────────────────────────┘
           Unix Domain Socket: /tmp/t113claw_audio.sock
```

---

## 二、IPC 协议

### 2.1 帧格式

所有 IPC 消息使用统一的二进制帧格式：

```
 0                   1                   2                   3
 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1 2 3 4 5 6 7 8 9 0 1
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Magic (0x4D434157)                       |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|           Type (uint16)       |          Flags (uint16)        |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Length (uint32)                           |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
|                       Payload (变长)                           |
|                          ...                                   |
+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+-+
```

| 字段 | 类型 | 字节数 | 说明 |
|------|------|--------|------|
| magic | uint32 | 4 | 固定值 `0x4D434157` ("MCAW")，用于帧同步 |
| type | uint16 | 2 | 消息类型（见下表） |
| flags | uint16 | 2 | 保留，当前为 0 |
| length | uint32 | 4 | payload 字节数 |
| payload | bytes | length | 消息数据 |

**头部总长**：12 字节
**最大 payload**：32,768 字节（32KB）

### 2.2 消息类型

#### 命令消息（t113claw → t113claw_audio）

| 类型 | 值 | Payload | 说明 |
|------|------|---------|------|
| `AUDIO_CMD_RECORD_START` | 0x0001 | 无 | 开始录音 |
| `AUDIO_CMD_RECORD_STOP` | 0x0002 | 无 | 停止录音 |
| `AUDIO_CMD_PLAY_START` | 0x0003 | 无 | 准备播放（清空环形缓冲） |
| `AUDIO_CMD_PLAY_DATA` | 0x0004 | PCM 数据 | 送入播放数据（可重复发送，**无 ACK**） |
| `AUDIO_CMD_PLAY_STOP` | 0x0005 | 无 | 停止播放，关闭功放 |
| `AUDIO_CMD_VOLUME_SET` | 0x0006 | uint8_t [0-100] | 设置音量（百分比） |
| `AUDIO_CMD_STATUS_GET` | 0x0007 | 无 | 请求状态 |
| `AUDIO_CMD_SHUTDOWN` | 0x00FF | 无 | 优雅关闭音频进程 |

#### 事件消息（t113claw_audio → t113claw）

| 类型 | 值 | Payload | 说明 |
|------|------|---------|------|
| `AUDIO_EVT_AUDIO_DATA` | 0x0101 | PCM 数据 | 录音数据（30ms 一帧，960 字节） |
| `AUDIO_EVT_STATUS` | 0x0102 | `audio_ipc_status_t` | 状态信息（16 字节） |
| `AUDIO_EVT_ERROR` | 0x0103 | 错误数据 | 错误通知 |
| `AUDIO_EVT_ACK` | 0x0104 | 无 | 命令确认 |

#### 状态结构体 (`audio_ipc_status_t`, 16 字节)

```c
typedef struct __attribute__((packed)) {
    uint8_t  recording;    /* 1 = 正在录音 */
    uint8_t  playing;      /* 1 = 正在播放 */
    uint8_t  volume;       /* 0-100 音量 */
    uint8_t  amp_on;       /* 1 = 功放开启 */
    uint32_t sample_rate;  /* 采样率 (16000) */
    uint16_t channels;     /* 录音声道数 (1) */
    uint16_t format;       /* ALSA 格式 (2 = S16_LE) */
    uint8_t  reserved[4];
} audio_ipc_status_t;
```

---

## 三、音频参数

| 参数 | 值 | 说明 |
|------|------|------|
| 采样率 | 16,000 Hz | 语音标准采样率 |
| 录音声道 | 1（mono） | MIC3 差分输入 |
| 播放声道 | 2（stereo） | T113 DAC 立体声输出 |
| 位深度 | 16 bit | S16_LE（有符号 16 位小端序） |
| ALSA 设备 | `hw:0,0` | T113-S3 内置音频编解码器 |
| 录音 period | 512 帧（32ms） | ALSA 中断间隔 |
| 播放 period | 256 帧（16ms） | ALSA 中断间隔 |
| 录音帧大小 | 960 字节/30ms | IPC 传输边界 |
| 播放帧大小 | 1,920 字节/30ms | IPC 传输边界 |
| 环形缓冲区 | 1 MB | 约 16 秒立体声音频，阻塞写入 |

---

## 四、源文件说明

### 4.1 `audio/audio_ipc.h` — 共享协议定义

主进程和音频进程共同引用。定义了：
- Socket 路径、魔数、最大 payload
- 所有音频参数常量
- IPC 消息类型枚举
- 帧头 `audio_ipc_hdr_t` 和状态 `audio_ipc_status_t` 结构体
- GPIO 功放引脚号

### 4.2 `audio/audio_main.c` — 音频进程入口

**职责**：
- IPC 服务器（监听 Unix 域套接字）
- 命令分发（解析帧→调用对应模块）
- 播放环形缓冲区管理（1MB，阻塞写入）
- 信号处理（SIGINT/SIGTERM → 优雅退出，SIGPIPE → 忽略）

**关键实现**：

```c
/* 环形缓冲区 — 阻塞写入，确保不丢失数据 */
static void play_ring_write(const uint8_t *data, size_t len)
{
    while (offset < len && !s_play_flushing) {
        if (play_ring_free() == 0) {
            usleep(5000); /* 5ms 等待空间 */
            continue;
        }
        /* 写入可用空间 */
    }
}
```

**连接模型**：单客户端。accept 后进入 `serve_client()` 循环，客户端断开后重新等待连接。

### 4.3 `audio/audio_capture.c` — ALSA 录音

**线程模型**：独立 pthread (`mc_capture`)，由 `audio_capture_start()` 创建。

```c
/* 回调函数：每次录音 period 完成时调用 */
typedef void (*audio_capture_cb_t)(const uint8_t *buffer, size_t size, void *user);
```

**ALSA 错误处理**：
- `EPIPE`（overrun）：日志警告 + `snd_pcm_prepare()` 恢复
- 其他错误：`snd_pcm_recover()` 尝试恢复

### 4.4 `audio/audio_playback.c` — ALSA 播放

**线程模型**：独立 pthread (`mc_playback`)，由 `audio_playback_start()` 创建。

**数据模型**：Pull 模式。播放线程通过回调主动拉取数据：

```c
typedef int (*audio_play_cb_t)(uint8_t *buffer, size_t size, void *user);
/* 返回实际填充的字节数，0 = 无数据 */
```

**功放控制**：
- 首次收到有效音频数据 → 自动开启功放
- 回调返回 0（无数据）超过 10ms → 自动关闭功放

### 4.5 `audio/audio_codec.c` — T113 编解码器

通过 `amixer` 系统调用配置 T113-S3 内置音频编解码器。

**初始化设置**：

| 路径 | amixer numid | 设置 | 说明 |
|------|-------------|------|------|
| MIC3 输入 | 29 | ON | ADC3 MIC3 Boost 开启 |
| MIC3 模式 | 22 | MIC_DIFFER | 差分输入模式 |
| MIC3 增益 | 11 | 31 (max) | 麦克风增益最大 |
| ADC3 音量 | 8 | 160 | ADC 音量 |
| HP 开关 | 30 | ON | 耳机输出开启 |
| HP 音量 | 17 | 6/7 | 近最大值 |
| DAC 音量 | 5 | 160,160 | 左右声道 DAC 音量 |
| 数字音量 | 4 | 63 (max) | 数字增益最大 |
| 无用输入 | 23,24,25,26 | OFF | 关闭 MIC1/MIC2/FMINL/LINEINL 减少噪声 |

**音量控制映射**：
```
volume_pct (0-100) → DAC: 0-255, HP: 0-7
```

**功放 GPIO**：
- 引脚：GPIO34（PB2）
- 芯片：LM4871 功放
- 逻辑：**反相**（写 0 = 开启，写 1 = 关闭）
- sysfs 路径：`/sys/class/gpio/gpio34/value`

### 4.6 `src/services/audio_service.c` — IPC 客户端

**平台适配**：
- `SIMULATOR_LINUX`：所有函数均为 no-op（空实现）
- T113：完整 IPC 实现

**进程管理**：
1. `audio_service_init()` — 查找 `t113claw_audio` 二进制（同目录/当前目录）
2. `audio_service_start()` — fork + exec 音频进程，等待 500ms，连接 IPC
3. `audio_service_stop()` — 发 SHUTDOWN，等 200ms，SIGTERM，最后 SIGKILL

**连接重试**：30 次 × 200ms = 最多 6 秒

**接收线程**：独立 pthread，循环读取音频进程的事件消息。录音数据通过注册的回调函数转发给调用者。

---

## 五、API 参考

### 5.1 音频服务 API（t113claw 主进程内使用）

```c
#include "services/audio_service.h"

/* 初始化（查找音频进程二进制） */
int audio_service_init(void);

/* 启动（fork 音频进程 + IPC 连接） */
int audio_service_start(void);

/* 停止（关闭音频进程） */
void audio_service_stop(void);

/* 开始录音（PCM 通过回调返回） */
int audio_service_record_start(audio_data_cb_t cb, void *user);

/* 停止录音 */
int audio_service_record_stop(void);

/* 开始播放（清空缓冲区，准备接收数据） */
int audio_service_play_start(void);

/* 发送播放数据（自动分片为 ≤32KB 的 IPC 消息） */
int audio_service_play_pcm(const uint8_t *pcm, size_t len);

/* 停止播放 */
int audio_service_play_stop(void);

/* 设置音量（0-100%） */
int audio_service_set_volume(int volume_pct);

/* 检查连接状态 */
int audio_service_is_running(void);
```

### 5.2 典型使用示例

#### 录音

```c
/* 录音回调 */
void my_audio_cb(const uint8_t *pcm, size_t len, void *user) {
    /* pcm: S16_LE, 16kHz, mono */
    /* len: 通常 960 字节（30ms） */
    save_to_buffer(pcm, len);
}

/* 开始录音 */
audio_service_record_start(my_audio_cb, NULL);

/* ... 录音中 ... */

/* 停止录音 */
audio_service_record_stop();
```

#### 播放

```c
/* 准备播放 */
audio_service_play_start();

/* 发送 PCM 数据（S16_LE, 16kHz, stereo）*/
/* 可多次调用，数据自动缓冲 */
audio_service_play_pcm(stereo_pcm, pcm_len);
audio_service_play_pcm(more_pcm, more_len);

/* 停止播放 */
audio_service_play_stop();
```

---

## 六、数据流

### 录音流

```
  MIC3 (模拟输入)
       │
       ▼
  T113 ADC3 → ALSA hw:0,0 (16kHz, mono, S16_LE)
       │
       ▼
  audio_capture_thread: snd_pcm_readi() (每 32ms/512帧)
       │
       ▼
  on_capture_data() → ipc_send_msg(EVT_AUDIO_DATA, pcm, 960B)
       │  Unix Socket
       ▼
  audio_service recv_thread → s_data_cb(pcm, 960, user)
       │
       ▼
  调用者的回调函数处理 PCM 数据
```

### 播放流

```
  调用者: audio_service_play_pcm(stereo_pcm, len)
       │
       ▼
  ipc_send(PLAY_DATA, pcm, ≤32KB) × N 次
       │  Unix Socket
       ▼
  handle_command(PLAY_DATA) → play_ring_write(阻塞写入)
       │
       ▼
  环形缓冲区 (1MB)
       │
       ▼
  playback_thread: on_play_request() → play_ring_read()
       │
       ▼
  snd_pcm_writei() → 功放开启 → T113 DAC → HP 输出
```

---

## 七、构建配置

### CMakeLists.txt 关键配置

```cmake
add_executable(t113claw_audio
    audio_main.c
    audio_codec.c
    audio_capture.c
    audio_playback.c
)
target_link_libraries(t113claw_audio PRIVATE asound pthread)
```

### 编译

音频进程在 T113 交叉编译时自动构建：
```bash
./build.sh -t113
# 产物: build/audio/t113claw_audio (约 97KB ARM ELF)
```

x86 模拟器模式下不编译 `t113claw_audio`，`audio_service` 为空实现。

---

## 八、部署

### 文件布局

```
/usr/share/t113claw/
├── t113claw           # 主进程
├── t113claw_audio     # 音频进程（自动被主进程 fork/exec）
└── data/            # 配置和数据
```

`t113claw_audio` 必须与 `t113claw` 在同一目录下，主进程通过 `/proc/self/exe` 定位自身路径并查找 `t113claw_audio`。

### 权限要求

- ALSA 设备 (`hw:0,0`)：需要 root 或 audio 组权限
- GPIO sysfs (`/sys/class/gpio/gpio34/`)：需要 root 或 gpio 组权限

### 故障排查

| 症状 | 可能原因 | 解决方案 |
|------|---------|---------|
| "Audio binary not found" | `t113claw_audio` 不在同目录 | 确认部署位置正确 |
| "IPC connect failed" | 音频进程启动失败 | 检查 ALSA 设备和权限 |
| 录音无数据 | MIC3 未接或 amixer 配置错误 | 用 `arecord` 测试 |
| 播放无声音 | 功放 GPIO 未配置 | 检查 gpio34 export 和方向 |
| 播放杂音 | 采样率/声道不匹配 | 确保送入 stereo S16_LE 16kHz |

---

## 九、二次开发指南

### 修改采样率

1. 修改 `audio_ipc.h` 中 `AUDIO_SAMPLE_RATE`
2. 重新编译 `t113claw` 和 `t113claw_audio`
3. 注意：T113 编解码器支持的采样率有限（8k, 16k, 24k, 48k）

### 添加新的 IPC 命令

1. 在 `audio_ipc.h` 的 `audio_ipc_type_t` 枚举添加新类型
2. 在 `audio_main.c` 的 `handle_command()` 添加 case 分支
3. 在 `audio_service.c` 添加对应的客户端 API 函数

### 支持多客户端

当前设计为单客户端。如需支持多客户端（如语音进程独立通信）：
1. `audio_main.c` 中 `serve_client()` 改为非阻塞 accept + select/epoll
2. 每个客户端分配独立的发送锁
3. 录音数据广播给所有已注册客户端
4. 播放数据需要混音或独占

### 替换 ALSA 后端

如需对接其他音频框架（PulseAudio、PipeWire 等）：
1. 修改 `audio_capture.c` 和 `audio_playback.c` 中的 ALSA 调用
2. 保持回调接口不变即可
3. IPC 协议层无需修改
