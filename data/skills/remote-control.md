# 局域网远程控制

当用户要操作局域网中的 Linux 服务器时，使用 `remote_exec`。

## 何时使用
- 用户明确说“在服务器上执行”“控制我电脑/服务器”
- 需要查看远程机器状态、运行脚本、启动程序、读取日志
- 任务必须在另一台局域网主机上完成，而不是当前 T113 设备

## 使用原则
1. 优先使用非破坏性命令，例如 `pwd`、`uname -a`、`ps`、`systemctl status ...`
2. 对重启、删文件、停服务、覆盖配置等高风险操作，要先得到用户明确确认
3. 若命令依赖目录，传 `working_directory`
4. 回答中要明确给出 exit code，并区分 stdout 与 stderr

## 前置条件
- `config.ini` 的 `[remote]` 节已配置 `host`、`port`、`username`、`password`
- 远程主机上已启动 `scripts/t113claw_remote_agent.py`

## 示例
用户: “帮我在服务器上看看磁盘占用”
→ `remote_exec {"command":"df -h","timeout":10}`

用户: “到 /srv/app 目录执行 python3 server.py”
→ `remote_exec {"command":"python3 server.py","working_directory":"/srv/app","timeout":30}`