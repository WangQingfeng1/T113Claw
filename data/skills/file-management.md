# 笔记与文件管理

在数据目录中读写笔记、配置和其他文件。

## 何时使用
- 用户要求记笔记、保存信息
- 用户要查看之前保存的文件
- 用户要求列出或整理文件
- 需要持久化任何重要信息

## 使用方法

### 列出文件
→ list_dir {"prefix": ""} — 列出根目录
→ list_dir {"prefix": "memory"} — 列出记忆目录

### 读取文件
→ read_file {"path": "文件路径"} — 路径相对于 data 目录

### 写入文件
→ write_file {"path": "文件路径", "content": "内容"}
- 会创建不存在的目录
- 注意：这是覆盖写入，更新文件时先读取再修改

## 目录结构说明
- `memory/` — 记忆文件（SOUL.md、USER.md、MEMORY.md）
- `sessions/` — 对话历史（自动管理）
- `skills/` — 技能文件（Markdown）
- `config/` — 配置文件

## 注意事项
- 所有路径限制在 data 目录内，不能使用 `..` 跳出
- SOUL.md 是身份文件，不建议修改
- 更新文件前先读取已有内容，避免覆盖

## 示例
用户: "帮我记个待办事项：明天买牛奶"
→ read_file {"path": "memory/notes.md"}（如果文件存在）
→ write_file {"path": "memory/notes.md", "content": "# 待办\n- [ ] 明天买牛奶\n"}
→ "已记下待办：明天买牛奶。"
