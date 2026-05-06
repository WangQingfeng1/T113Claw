# 局域网远程控制

## 概述

T113Claw 新增了 `remote_exec` 工具，用于控制局域网内一台已配置的 Linux 服务器。T113 设备侧不依赖 `ssh` 命令，而是通过现有的 `libcurl` HTTP 客户端向远程主机上的轻量守护程序发请求。

当前版本支持单服务器配置。命令执行只保留 Agent 调用 `remote_exec` 这一种交互方式；板端 UI 不再直接输入命令，只在 Settings 的 `SERVER LINK` 视图里维护服务器连接配置，并显示在线状态。

## 组成

| 文件 | 职责 |
|------|------|
| `src/tools/tool_remote_exec.c` | 解析工具输入、读取 `[remote]` 配置、发送 HTTP 请求、格式化输出 |
| `src/tools/tool_remote_exec.h` | 声明 `tool_remote_exec_execute` |
| `src/services/remote_client.c` | 共享远程 HTTP 客户端，供工具层和 UI 状态探测复用 |
| `src/ui/ui_manager.c` | 顶栏服务器状态图标轮询与异步探测 |
| `src/ui/page_settings.c` | Settings 中的 `SERVER LINK` 视图，负责保存远程配置 |
| `scripts/t113claw_remote_agent.py` | PC 端 HTTP 守护程序，执行命令并返回 JSON |
| `data/skills/remote-control.md` | 指导 LLM 在何时、如何使用 `remote_exec` |

## 通信模型

```
T113Claw (T113 / x86)
  remote_exec tool
      |
      | HTTP POST /exec
      | Authorization: Basic <base64(username:password)>
      v
LAN Linux Server
  t113claw_remote_agent.py
      |
      +--> subprocess.run(shell=True)
      +--> JSON {stdout, stderr, exit_code, timed_out, duration_ms}
```

## 配置

在 `data/config/config.ini` 中增加单个 `[remote]` 节：

```ini
[remote]
host = 192.168.1.100
port = 8765
username = demo
password = secret
```

说明：

- `host`：主机名、IP，或完整 base URL（如 `http://192.168.1.100:8765`）
- `port`：默认 `8765`
- `username/password`：HTTP Basic Auth 凭证

也可在 `t113claw_secrets.h` 中编译进默认值：

```c
#define T113CLAW_SECRET_REMOTE_HOST       "192.168.1.100"
#define T113CLAW_SECRET_REMOTE_PORT       "8765"
#define T113CLAW_SECRET_REMOTE_USERNAME   "demo"
#define T113CLAW_SECRET_REMOTE_PASSWORD   "secret"
```

## PC 端守护程序

### 启动方式

```bash
cd T113Claw
python3 scripts/t113claw_remote_agent.py \
  --bind 0.0.0.0 \
  --port 8765 \
  --username demo \
  --password secret
```

常用参数：

- `--max-timeout 120`：限制单次命令最长执行时间
- `--max-output 4096`：限制 `stdout`/`stderr` 返回长度
- `--allow-prefix PREFIX`：可重复添加，仅允许以指定前缀开头的命令

### API

`GET /status`

返回主机名、平台、当前工作目录，供顶栏服务器状态探测和 Settings 中的 `SERVER LINK` 状态显示复用。

`POST /exec`

请求体：

```json
{
  "command": "df -h",
  "timeout": 10,
  "working_directory": "/srv/app"
}
```

响应体：

```json
{
  "exit_code": 0,
  "timed_out": false,
  "duration_ms": 8,
  "stdout": "Filesystem ...",
  "stderr": ""
}
```

## LLM 工具接口

`remote_exec` 的输入 schema：

| 参数 | 类型 | 必填 | 说明 |
|------|------|------|------|
| `command` | string | ✅ | 在远程主机上执行的 shell 命令 |
| `timeout` | integer | ❌ | 超时秒数，默认 30，最大 120 |
| `working_directory` | string | ❌ | 远程执行目录 |

输出为纯文本，包含：

- 目标主机
- exit code
- duration
- 是否超时
- stdout
- stderr

## UI 页面

板端 UI 现在只保留配置能力，不再直接发起远程命令：

- Chat 顶栏显示服务器状态图标，和 WiFi 图标一样实时刷新
- Settings 中新增 `SERVER LINK` 视图，位置在 `WIFI` 与 `SYSTEM` 之间
- `SERVER LINK` 负责编辑 host / port / username / password 并写回 `config.ini`
- 保存后立即触发一次 `/status` 探测，结果用于更新顶栏图标和当前页状态提示

远程命令执行统一由 Agent 调用 `remote_exec` 完成，避免板端 UI 同时承担配置和命令控制两种职责

## 已验证行为

### 2026-05-01 x86 本机烟测

验证环境：本机启动 `scripts/t113claw_remote_agent.py --bind 127.0.0.1 --port 8765 --username demo --password secret`，随后用临时 harness 直接调用 `tool_remote_exec_execute()`。

验证命令：

```json
{"command":"printf hello_remote","timeout":5}
```

实际结果：

```text
Remote target: 127.0.0.1:8765
Exit code: 0
Duration: 1 ms
Timed out: no

Stdout:
hello_remote
```

## 安全边界

- 当前版本使用 HTTP Basic Auth，适合可信局域网环境
- 守护程序默认不限制命令内容；若需要约束，请使用 `--allow-prefix`
- 对 `rm`、`reboot`、`systemctl stop` 等高风险命令，建议由 Agent 先征求用户确认
- 当前未启用 TLS；如后续要跨子网或公网使用，应切换到反向代理 + HTTPS 或改为 SSH 客户端方案

## 后续扩展

1. 把单服务器 `[remote]` 扩展为多服务器配置
2. 为 `SERVER LINK` 增加更细的错误诊断信息（鉴权失败、超时、DNS 失败等）
3. 支持文件上传/下载与服务启停的结构化接口