# T113Claw 编译与部署指南

> 适用于 Allwinner T113-S3 开发板（TinaLinux 5.4.61）

---

## 一、环境准备

### 1.1 开发主机（Ubuntu / Debian）

```bash
# 构建工具
sudo apt-get install -y build-essential cmake libcurl4-openssl-dev libssl-dev

# ADB 工具
sudo apt-get install -y adb
```

### 1.2 交叉编译工具链

工具链路径（已内置在 `build.sh` 中）：
```
/yours/toolchain-sunxi-glibc-gcc-830/toolchain/bin/
```

如需修改，编辑 `build.sh` 中的 `TOOLCHAIN_PATH` 变量。

### 1.3 T113 开发板

- 通过 USB Type-C 连接开发主机（供电 + ADB 调试）
- 确认 ADB 可用：`adb devices`

---

## 二、配置密钥（首次设置）

T113Claw 使用两层配置体系：

| 优先级 | 来源 | 说明 |
|--------|------|------|
| 最高 | 环境变量 `T113CLAW_API_KEY` | 临时覆盖 API 密钥 |
| 高 | `data/config/config.ini` | 运行时配置文件 |
| 低 | `t113claw_secrets.h` | **编译时密钥，烧入二进制** |

### 推荐方式：创建 `t113claw_secrets.h`

将密钥编译进二进制文件，这样即使删除 `data/` 目录，程序仍能正常运行（会自动重新生成 `config.ini`）。

```bash
cp t113claw_secrets.h.example t113claw_secrets.h
```

编辑 `t113claw_secrets.h`，填入你的实际值：

```c
#define T113CLAW_SECRET_WIFI_SSID         "你的WiFi名"
#define T113CLAW_SECRET_WIFI_PASS         "你的WiFi密码"
#define T113CLAW_SECRET_MODEL_PROVIDER    "openai"
#define T113CLAW_SECRET_API_KEY           "sk-你的API密钥"
#define T113CLAW_SECRET_MODEL             "deepseek-chat"
#define T113CLAW_SECRET_API_URL           "https://api.deepseek.com/v1/chat/completions"
#define T113CLAW_SECRET_FEISHU_APP_ID     "cli_xxxxxxxx"
#define T113CLAW_SECRET_FEISHU_APP_SECRET "xxxxxxxxxxxxxxxx"

// 讯飞语音服务（语音交互功能必需）
#define T113CLAW_SECRET_XFYUN_APPID       "your_xfyun_appid"
#define T113CLAW_SECRET_XFYUN_APIKEY      "your_xfyun_apikey"
#define T113CLAW_SECRET_XFYUN_APISECRET   "your_xfyun_apisecret"
```

> **注意**：`t113claw_secrets.h` 已在 `.gitignore` 中，不会被提交到版本库。

### 配置行为说明

| 场景 | 行为 |
|------|------|
| 有 `t113claw_secrets.h` + 有 `config.ini` | INI 覆盖编译默认值 |
| 有 `t113claw_secrets.h` + 无 `config.ini` | 用编译默认值，**自动生成** `config.ini` |
| 无 `t113claw_secrets.h` + 有 `config.ini` | 纯靠 INI 文件 |
| 无 `t113claw_secrets.h` + 无 `config.ini` | 空配置运行（功能受限） |

---

## 三、编译

### 3.1 x86 本机编译（验证 / 调试用）

```bash
cd T113Claw
./build.sh -linux
```

运行：
```bash
./build/src/t113claw
```

> `-linux` 会启动 SDL UI 模拟器，便于在桌面侧直接联调 Chat / Settings 页面。

### 3.2 T113 交叉编译

```bash
cd T113Claw
./build.sh -t113
```

产物：`build/src/t113claw`（ARM ELF，约 706KB）+ `build/audio/t113claw_audio`（ARM ELF，约 97KB）

### 3.3 LVGL UI 构建说明

- T113 与 x86 构建都会直接使用项目内的 `lvgl/` 源码目录
- 显示逻辑使用旋转布局模型，但相关端口代码已经内置在项目的 `platform/` 目录中
- 当前端口里，`platform/t113/src/porting/lv_port_disp.c` 会在旋转后将逻辑宽度固定为 `280`
- 中文字体资源默认来自 `data/ui/font/SOURCEHANSANSCN_REGULAR.OTF`
- x86 `-linux` 构建会使用 `platform/x86linux/src/porting/` 下的 SDL 端口层，复用同一套 `ui_manager` / `page_chat` / `page_settings` 代码路径
- 音频与语音后端在 x86 侧仍为 no-op，因此桌面联调主要用于 UI 与主流程联调

### 3.4 清理

```bash
./build.sh -clean
```

> `clean` 只删除 `build/` 目录，不影响 `data/` 运行时数据。

---

## 四、部署到 T113 板

### 4.1 首次部署

```bash
# 创建目标目录
adb shell "mkdir -p /usr/share/t113claw"

# 推送可执行文件
adb push build/src/t113claw /usr/share/t113claw/
adb push build/audio/t113claw_audio /usr/share/t113claw/
adb shell "chmod +x /usr/share/t113claw/t113claw /usr/share/t113claw/t113claw_audio"

# 推送运行时数据（首次需要，包含 config.ini、SOUL.md 等）
adb push data/ /usr/share/t113claw/data/
```

> `data/` 目录现在还包含 UI 图片资源 `data/ui/image/` 和字体资源 `data/ui/font/SOURCEHANSANSCN_REGULAR.OTF`，首次部署不要省略。

### 4.2 后续更新（仅需推可执行文件）

```bash
./build.sh -t113
adb push build/src/t113claw /usr/share/t113claw/
adb push build/audio/t113claw_audio /usr/share/t113claw/
```

> 如果你配置了 `t113claw_secrets.h`，可以不推 `data/`。程序启动时会自动创建目录结构并生成 `config.ini`。

### 4.3 动态库（大多数固件已内置，首次检查即可）

```bash
# 检查板上是否有必要的库
adb shell "ls /usr/lib/libcurl.so* /usr/lib/libssl.so* /usr/lib/libcrypto.so* /usr/lib/libasound.so* /usr/lib/libfreetype.so* /usr/lib/libbz2.so* /usr/lib/libuapi.so*"

# 如果缺少，从预编译库推送
adb push platform/t113/lib/libcurl.so.4.7.0 /usr/lib/
adb push platform/t113/lib/libssl.so.1.1 /usr/lib/
adb push platform/t113/lib/libcrypto.so.1.1 /usr/lib/
adb push platform/t113/lib/libnghttp2.so.14.20.1 /usr/lib/
adb push platform/t113/lib/libz.so.1.2.11 /usr/lib/
# 音频库（t113claw_audio 需要）
adb push platform/t113/lib/libasound.so.2 /usr/lib/
# UI 相关库（LVGL + FreeType + sunxi G2D porting 需要）
adb push platform/t113/lib/libfreetype.so.6.17.0 /usr/lib/
adb push platform/t113/lib/libbz2.so.1.0 /usr/lib/
adb push platform/t113/lib/libuapi.so /usr/lib/
```

> 如果固件里已经带有这些库，只需要校验一次即可，不必每次重复推送。

### 4.4 sherpa-onnx 库 + KWS 模型（唤醒词功能必需）

sherpa-onnx 库和 KWS 模型已包含在项目中：

```bash
# 推送 sherpa-onnx 共享库
adb push platform/t113/sherpa-onnx/lib/libsherpa-onnx-c-api.so /usr/lib/

# 推送 KWS 模型（唤醒词 "你好小爪"）
adb shell "mkdir -p /usr/share/t113claw/kws-model"
adb push data/kws-model/encoder-epoch-12-avg-2-chunk-16-left-64.int8.onnx /usr/share/t113claw/kws-model/
adb push data/kws-model/decoder-epoch-12-avg-2-chunk-16-left-64.onnx /usr/share/t113claw/kws-model/
adb push data/kws-model/joiner-epoch-12-avg-2-chunk-16-left-64.int8.onnx /usr/share/t113claw/kws-model/
adb push data/kws-model/tokens.txt /usr/share/t113claw/kws-model/
adb push data/kws-model/keywords_t113claw.txt /usr/share/t113claw/kws-model/
```

> `libsherpa-onnx-c-api.so` 约 11MB（内含静态 onnxruntime + nsync + ABI 兼容层）。KWS 模型文件约 5MB（int8 量化）。

---

## 五、运行

### 5.1 手动运行

```bash
adb shell
cd /usr/share/t113claw && ./t113claw
```

程序启动后会初始化 LVGL，并显示 `Chat` 首页。进入 `Settings` 的入口有两个：右上角设置按钮，以及 Chat 顶栏下滑手势；`Settings` 顶部 `BACK` 返回 Chat。Chat 页会显示消息流、Voice/Agent 状态和底部运行时错误字，顶栏 WiFi 图标按真实链路状态刷新。

### 5.15 后台运行

通过 `-d` 参数启动守护进程模式（fork + setsid），断开 ADB 后进程不会被杀死：

```bash
adb shell "cd /usr/share/t113claw && ./t113claw -d >/tmp/mc.log 2>&1"

# 查看日志
adb shell "tail -f /tmp/mc.log"

# 停止
adb shell "killall t113claw t113claw_audio"
```

> **为什么需要 `-d`？** BusyBox ash 退出时会向子进程发送 SIGTERM，即使忽略 SIGHUP 也无法存活。`-d` 通过 fork+setsid 创建新会话，彻底脱离终端。
>
> **调试时用**：如果通过 `adb shell` 直接进入板卡终端再运行 `./t113claw`，无需 `-d`。
>
> **开机自启时**：写入 `rc.final` 的命令无需 `-d`，因为 init 进程不会退出。

### 5.2 WiFi 自动连接

如果在 `t113claw_secrets.h` 中配置了 WiFi SSID 和密码，程序启动时会 **自动完成**：

1. 启动 wpa_supplicant
2. 配置并连接 WiFi
3. DHCP 获取 IP
4. 设置 DNS 解析
5. NTP 时间同步

无需手动执行 `wpa_cli` 或 `udhcpc`。

> **如果没配 WiFi**：需先手动连接 WiFi 再启动程序（见下方手动 WiFi 配置）。

### 5.3 手动 WiFi 配置（备选方案）

```bash
adb shell

# 启用 WiFi
ifconfig wlan0 up
wpa_supplicant -i wlan0 -c /etc/wifi/wpa_supplicant/wpa_supplicant.conf -B

# 配置网络（注意 socket 路径）
WPA="wpa_cli -p /etc/wifi/wpa_supplicant/sockets -i wlan0"
$WPA add_network
$WPA set_network 0 ssid '"你的WiFi名"'
$WPA set_network 0 psk '"你的密码"'
$WPA enable_network 0
$WPA select_network 0
$WPA save_config

# DHCP
udhcpc -i wlan0 -t 5 -T 2 -A 5 -q

# DNS
echo 'nameserver 114.114.114.114' > /etc/resolv.conf
echo 'nameserver 8.8.8.8' >> /etc/resolv.conf

# 时间同步（TLS 必须！）
date -s '2026-04-08 16:00:00'
ntpd -q -p ntp.aliyun.com
```

### 5.4 预期启动日志

```
┌─────────────────────────────────────┐
│  T113Claw v0.1.0 — Micro Agent         │
│  T113-S3 Linux AI Assistant         │
└─────────────────────────────────────┘

17:00:01 [INFO] config: Config initialized (8 entries)
17:00:01 [INFO] wifi: Starting wpa_supplicant...
17:00:02 [INFO] wifi: Configuring WiFi: MySSID
17:00:05 [INFO] wifi: WiFi connected to MySSID
17:00:06 [INFO] wifi: Requesting DHCP lease...
17:00:07 [INFO] wifi: NTP time sync initiated (ntp.aliyun.com)
17:00:07 [INFO] wifi: WiFi service started — network ready
17:00:07 [INFO] http: HTTP client initialized
17:00:07 [INFO] llm: LLM client initialized (provider: openai, model: deepseek-chat)
17:00:07 [INFO] memory: Memory store initialized
17:00:07 [INFO] feishu: Feishu bot initialized
17:00:08 [INFO] feishu: Feishu WebSocket connected!
17:00:08 [INFO] stt: STT module initialized
17:00:08 [INFO] tts: TTS module initialized
17:00:08 [INFO] voice: Voice manager initialized
17:00:08 [INFO] audio: Audio service started (t113claw_audio PID: 1234)
17:00:08 [INFO] voice: Button listener started (GPIO 106)
17:00:08 [INFO] main: T113Claw is running. Press Ctrl+C or type /quit to exit.
```

---

## 六、开机自启

编辑板上的启动脚本：

```bash
adb shell
vi /etc/init.d/rc.final
```

在文件末尾添加：

```bash
# T113Claw auto-start
cd /usr/share/t113claw && ./t113claw &
```

> `t113claw_audio` 由主进程自动 fork/exec，无需单独启动。

**保存后必须重启**才能写入 NAND Flash：

```bash
reboot
```

---

## 七、常见问题

### Q: 删了 data/ 后程序报错，怎么办？

如果编译时创建了 `t113claw_secrets.h`，程序会自动重新生成 `data/config/config.ini`。

如果没有 `t113claw_secrets.h`，程序仍能启动但没有 API 密钥和飞书凭证。解决方案：
1. 重新推送 `data/` 目录：`adb push data/ /usr/share/t113claw/data/`
2. 或者手动创建 config.ini

### Q: TLS 证书验证失败？

T113 没有 RTC 电池，断电后时间重置为 1970。程序启动时会尝试 NTP 同步，但如果时间偏差太大（1970→2026），BusyBox ntpd 可能无法调整。

解决方案：程序已在 WiFi service 中处理——当检测到年份 < 2024 时自动设置粗略时间再 NTP 微调。

### Q: WiFi 连不上？

1. 确认 SSID 和密码正确
2. 检查板上 RTL8723DS 驱动已加载：`lsmod | grep 8723`
3. 检查 wpa_supplicant 是否运行：`ps | grep wpa`
4. 手动测试：按 5.3 节步骤排查

### Q: 只想清除记忆不丢配置？

```bash
# 只删除记忆文件，保留配置
adb shell "rm -f /usr/share/t113claw/data/memory/USER.md"
adb shell "rm -f /usr/share/t113claw/data/memory/MEMORY.md"
adb shell "rm -rf /usr/share/t113claw/data/sessions/"
```

### Q: 板上可用空间不足？

```bash
adb shell df -h
```

T113Claw 二进制约 706KB + 音频进程约 97KB + sherpa-onnx 库 11MB + KWS 模型 5MB，运行时数据通常 < 1MB。板上 UDISK 分区有约 65MB 可用。

---

## 八、快速参考

```bash
# 一次性完整部署（从零开始）
cp t113claw_secrets.h.example t113claw_secrets.h  # 填入密钥
vi t113claw_secrets.h                           # 编辑（API Key + 讯飞凭证 + WiFi）
./build.sh -t113                              # 编译
adb shell "mkdir -p /usr/share/t113claw"        # 建目录
adb push build/src/t113claw /usr/share/t113claw/  # 推主程序
adb push build/audio/t113claw_audio /usr/share/t113claw/  # 推音频进程
adb shell "chmod +x /usr/share/t113claw/t113claw /usr/share/t113claw/t113claw_audio"
adb push data/ /usr/share/t113claw/data/        # 推数据（首次）
# 推送 sherpa-onnx 库 + KWS 模型（首次，见 4.4 节）
adb push platform/t113/sherpa-onnx/lib/libsherpa-onnx-c-api.so /usr/lib/
adb shell "mkdir -p /usr/share/t113claw/kws-model"
# ... 推送模型文件（见 4.4 节完整命令）
adb shell "cd /usr/share/t113claw && ./t113claw"  # 运行

# 日常迭代（改代码后，无需重复推 sherpa-onnx 和模型）
./build.sh -t113
adb push build/src/t113claw /usr/share/t113claw/
adb push build/audio/t113claw_audio /usr/share/t113claw/
adb shell "cd /usr/share/t113claw && ./t113claw"
```
