# 定时任务

创建、查看、删除定时提醒和周期任务。

## 何时使用
- 用户要求设置闹钟、提醒、定时任务
- 用户说"每隔X分钟/小时提醒我…"
- 用户说"X点提醒我…"
- 用户要求查看或取消已有的定时任务

## 使用方法

### 创建周期任务
1. 用 `get_current_time` 确认当前时间
2. 将用户描述的间隔换算为秒数
3. 用 `cron_add` 创建：
   - `schedule_type`: `"every"`
   - `interval_s`: 间隔秒数（如 3600 = 1小时）
   - `message`: 触发时执行的提示语
   - `name`: 简短描述

### 创建定点任务
1. 用 `get_current_time` 获取当前时间
2. 计算目标时间的 Unix 时间戳
3. 用 `cron_add` 创建：
   - `schedule_type`: `"at"`
   - `at_epoch`: 目标 Unix 时间戳

### 管理任务
- `cron_list` — 查看所有任务
- `cron_remove` — 按 job_id 删除任务

## 时间换算参考
- 1 分钟 = 60 秒
- 1 小时 = 3600 秒
- 1 天 = 86400 秒

## 示例
用户: "每两小时提醒我喝水"
→ cron_add {"name": "drink_water", "schedule_type": "every", "interval_s": 7200, "message": "提醒用户该喝水了，关心一下用户"}
→ "好的，已设置每 2 小时提醒你喝水。"

用户: "下午3点提醒我开会"
→ get_current_time（获取当前时间，计算下午3点的时间戳）
→ cron_add {"name": "meeting", "schedule_type": "at", "at_epoch": 1713265200, "message": "提醒用户现在该开会了"}
→ "好的，下午 3 点我会提醒你开会。"
