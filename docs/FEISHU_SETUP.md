# T113Claw 飞书机器人配置指南

## 概述

T113Claw 通过飞书开放平台的 **长连接（WebSocket）模式** 接收消息事件。设备主动连接飞书服务器，无需公网 IP 或域名，适合嵌入式设备（如 T113-S3）部署。

消息收发流程：
```
用户发消息 → 飞书服务器 → WebSocket推送 → T113Claw处理 → REST API回复
```

---

## 1. 创建飞书应用

1. 访问 [飞书开放平台](https://open.feishu.cn/app)，使用飞书管理员账号登录
2. 点击 **创建企业自建应用**
3. 填写应用名称（如 `T113Claw`）和描述
4. 记录 **App ID** 和 **App Secret**（在「凭证与基础信息」页面）

## 2. 配置应用权限

进入应用的 **权限管理** 页面，添加以下权限：

| 权限标识                         | 说明                   | 必需 |
|----------------------------------|------------------------|------|
| `im:message`                     | 获取与发送单聊、群组消息 | ✅   |
| `im:message:send_as_bot`         | 以应用身份发送消息       | ✅   |
| `im:message.group_at_msg:readonly` | 接收群聊中@机器人的消息  | ✅   |
| `im:chat:readonly`               | 获取群组信息             | 推荐 |
| `contact:user.base:readonly`     | 获取用户基本信息         | 推荐 |

添加后需要 **发布版本** 并由管理员审批通过。

## 3. 启用事件订阅（长连接模式）

1. 进入应用的 **事件与回调** → **事件配置** 页面
2. **加密策略**：选择任意可用选项（长连接模式不使用 Encrypt Key）
3. **事件请求方式**：选择 **使用长连接接收事件**（⚠️ 关键步骤！）
4. 添加事件：
   - `im.message.receive_v1` — 接收消息事件

> ⚠️ 必须选择「长连接」模式。如果选择了「将事件发送到请求地址」，T113Claw 将无法收到消息。

## 4. 发布应用

1. 进入 **版本管理与发布** 页面
2. 创建新版本，填写版本号和更新说明
3. 提交审核，等待管理员审批
4. 审批通过后应用即可使用

## 5. 配置 T113Claw

编辑 `data/config/config.ini`：

```ini
[feishu]
app_id = cli_xxxxxxxxxxxx
app_secret = xxxxxxxxxxxxxxxxxxxxxxxxxxxxxxxx
```

将 `app_id` 和 `app_secret` 替换为你在飞书开放平台获取的实际值。

## 6. 运行与验证

```bash
# 构建
./build.sh -linux

# 运行
./build/src/t113claw
```

正常启动后日志应显示：
```
[INFO] feishu: Token refreshed (expires in 7200s)
[INFO] feishu: WS config: service_id=33554678 ping=90s reconnect=90s
[INFO] feishu: Connecting to Feishu WebSocket...
[INFO] feishu: Feishu WebSocket connected!
```

看到 `Feishu WebSocket connected!` 即表示连接成功。

## 7. 测试消息收发

1. 在飞书中搜索你创建的机器人应用名称
2. 发送一条文字消息
3. T113Claw 日志中应显示收到的消息
4. 机器人会通过 AI 生成回复并自动发送

### 群聊使用

1. 将机器人添加到群聊
2. 在群中 @机器人名称 + 消息内容
3. 机器人会回复群聊

## 常见问题

### Q: 日志显示 "Token error: code=10014"
**A:** App Secret 不正确，请重新在飞书开放平台复制。

### Q: 日志显示 "WS config error: code=10003"
**A:** 应用未启用长连接模式。请检查「事件与回调」→「事件配置」中是否选择了「使用长连接接收事件」。

### Q: 连接成功但收不到消息
**A:** 检查以下几项：
1. 是否添加了 `im.message.receive_v1` 事件订阅
2. 应用版本是否已发布且审批通过
3. 权限是否已全部授予

### Q: 日志显示 "WebSocket connection failed"
**A:** 网络问题。检查设备是否能访问 `open.feishu.cn` 和 `msg-frontier.feishu.cn`。

---

## 技术细节

### 连接流程

```
1. POST /open-apis/auth/v3/tenant_access_token/internal
   Body: {"app_id":"...","app_secret":"..."}
   → 获取 tenant_access_token（用于发送消息）

2. POST /callback/ws/endpoint
   Body: {"AppID":"...","AppSecret":"..."}  (注意: PascalCase)
   Header: locale: zh
   → 获取 WSS URL + ClientConfig

3. 连接 WSS URL
   → 接收二进制 protobuf 帧

4. POST /open-apis/im/v1/messages?receive_id_type=open_id
   Header: Authorization: Bearer <tenant_access_token>
   → 发送回复消息
```

### 心跳与重连

- 每 `PingInterval`（默认 90 秒）发送一次 protobuf ping
- 服务器返回 pong，可动态更新 PingInterval
- 断连后等待 `ReconnectInterval` 秒后重新获取 WSS URL 并重连
- 自动重连，无限次重试

### 协议格式

飞书 WS 使用自定义 protobuf 编码的二进制帧：
- Field 1 (varint): seq_id — 序列号
- Field 2 (varint): log_id — 日志 ID
- Field 3 (varint): service — 服务 ID
- Field 4 (varint): method — 方法 (0=控制帧, 非0=事件)
- Field 5 (bytes): headers — 重复的键值对子消息
- Field 8 (bytes): payload — JSON 负载

收到事件后需回复 ACK（原始帧元数据 + `{"code":200}` 作为 payload）。
