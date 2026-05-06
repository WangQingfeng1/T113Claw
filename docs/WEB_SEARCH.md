# web_search 工具文档

## 概述

`web_search` 是注册在 `tool_registry` 中的工具，让 Agent 在 ReAct 循环中可以发起外部联网检索。它调用 `component/net/http_client.c`（libcurl 封装）发出 HTTP 请求，将结果格式化为纯文本后回填给 LLM。

整个设计保留了两条搜索路径。`auto` 模式下，若已配置 `tavily_api_key` 且 `domestic_only=0`，优先使用 Tavily；若 Tavily 未配置、被禁用或请求失败，则回退到搜狗移动搜索。

## 实现文件

| 文件 | 职责 |
|------|------|
| `src/tools/tool_web_search.c` | 解析请求、执行搜索、格式化输出 |
| `src/tools/tool_web_search.h` | 声明 `tool_web_search_execute` |
| `src/tools/tool_registry.c` | 注册工具及 JSON Schema |
| `src/agent/context_builder.c` | 向 LLM 说明何时应使用该工具 |
| `data/skills/web-search.md` | 技能层引导（搜索时机、查询构造、回答风格） |

## 搜索路径

### 搜狗移动搜索（回退路径 / 可强制）

请求：`GET https://m.sogou.com/web/searchList.jsp?keyword=<url_encoded_query>`

- 请求头固定：`User-Agent: Mozilla/5.0`、`Accept-Language: zh-CN,zh;q=0.9`
- 若传入 `site` 参数，查询词在编码前被改写为 `site:<domain> <query>`
- 搜狗页面的结果数据以 JSON 形式内嵌在页面里，格式是：
  ```html
  <script id="data-*" type="application/json">{ ... }</script>
  ```
  代码扫描所有这类 script 标签，逐一解析其中的 JSON 对象。
- 每个 JSON 对象按以下优先级取字段：
  - 标题：`title`（必须，否则丢弃该条）
  - 链接：`url` → `h5Url` → `urlEncrypt`（取第一个 `http://` 或 `https://` 开头的值；否则丢弃）
  - 摘要：`content` → `summary` → `rightTextArr[].content`
  - 来源：`showName` → `authorName` → `source` → `siteName`；均为空时从链接主机名派生
  - 日期：`date` → `urlDate` → `uploadTime`
- 已出现过的 URL 会被去重
- 所有文本字段经 `normalize_text` 处理：剥除 HTML 标签、解码常见 HTML 实体（`&amp;` `&nbsp;` `&quot;` `&#39;` `&lt;` `&gt;`）、折叠连续空白；摘要超过 220 字节时在 UTF-8 字符边界处截断到 216 字节，末尾追加 `...`

### Tavily API（可选）

请求：`POST https://api.tavily.com/search`，`Authorization: Bearer <tavily_api_key>`

请求体（JSON）：

```json
{
  "query": "<query>",
  "max_results": <N>,
  "include_answer": false,
  "search_depth": "basic",
  "topic": "general",
  "include_domains": ["<site>"]
}
```

`include_domains` 字段仅在传入 `site` 参数时添加。

响应解析：读取顶层 `results` 数组，每条取 `title`、`url`、`content`（摘要）、`published_date`，同样经过去重和文本清洗。

### provider 决策逻辑

`resolve_provider()` 的判断顺序（对应 `config.c` 中的 `[search]` 配置段）：

1. `provider=sogou` → 强制使用搜狗
2. `provider=tavily` → 强制使用 Tavily
3. `provider=auto`（编译默认值）：
   - `domestic_only=1`，**或** `tavily_api_key` 为空 → 使用搜狗
   - 否则 → 使用 Tavily
4. 若 auto 模式下选中 Tavily 但请求返回错误，自动降级到搜狗，并在输出的 `Note:` 行注明降级原因

## 输出格式

`format_results()` 生成的固定纯文本结构：

```text
Web search results for: <query>
Provider: <sogou|tavily>
[Site filter: <site>
][Note: <降级说明>
]
1. <title>
   Source: <source> | Date: <date>
   URL: <url>
   Summary: <summary>

2. ...
```

来源和日期均为可选行，仅在字段非空时输出。若无结果，输出：

```text
No web results found. Try rephrasing the query with more specific keywords.
```

## 工具注册参数

`tool_registry.c` 中注册的 JSON Schema：

| 参数 | 类型 | 必填 | 默认 | 上限 |
|------|------|------|------|------|
| `query` | string | ✅ | — | 256 字节 |
| `site` | string | ❌ | 空 | 128 字节 |
| `max_results` | integer | ❌ | 5 | 8 |

## 配置

三层优先级（低 → 高）：编译时默认值 → `t113claw_secrets.h` 密钥 → `config.ini` 运行时覆盖。

```ini
[search]
provider = auto          # auto / sogou / tavily
domestic_only = 0        # 0=优先使用 Tavily（有 key 时）；1=强制走搜狗
tavily_api_key =         # 留空则 auto 模式固定走搜狗
```

`t113claw_secrets.h` 对应宏：

```c
#define T113CLAW_SECRET_SEARCH_PROVIDER      "auto"
#define T113CLAW_SECRET_SEARCH_DOMESTIC_ONLY "0"
#define T113CLAW_SECRET_TAVILY_API_KEY       ""
```

## 验证方法

### x86 功能测试

```bash
cd T113Claw
./build.sh -linux
./build/src/t113claw
```

在 `T113Claw>` 提示符下输入搜索请求：

```
帮我搜索一下 T113 Linux 快速启动优化
```

预期日志（`[tool_search]` 标签）：

```
[INFO] tool_search: Searching provider=sogou query=T113 Linux 快速启动优化
[INFO] tool_search: Search complete: provider=sogou results=N
```

若已配置 `tavily_api_key` 且保持 `domestic_only=0`，日志中的 provider 应为 `tavily`；留空 key 或将 `domestic_only` 改为 `1`，则会走搜狗路径。

### T113 板端测试

```bash
./build.sh -t113
adb push build/src/t113claw /usr/share/t113claw/
adb shell "cd /usr/share/t113claw && ./t113claw"
```

发送相同搜索请求。若配置了 `tavily_api_key` 且 `domestic_only=0`，板端将优先走 Tavily；否则走搜狗。

## 已知限制

- 搜狗 HTML 中的 JSON 嵌入结构依赖其当前页面实现，若搜狗改版此结构，解析可能失效
- 每次 `web_search` 调用在 Agent 线程内同步执行，超时受 libcurl `CURLOPT_TIMEOUT=60s` 控制
