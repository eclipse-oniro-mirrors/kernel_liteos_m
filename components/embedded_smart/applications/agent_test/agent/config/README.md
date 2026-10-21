# LiteCrab 配置文件说明

## 正式配置

设备正式启动使用两个文件：

```text
config/base_config.json   Agent、Gateway、日志、Workspace 和 MCP 配置
config/llm_config.json    LLM Provider 配置，只引用 API Key 环境变量
```

这两个文件默认不提交到 Git。首次部署可复制示例：

```sh
cp config/base_config.example.json config/base_config.json
cp config/llm_config.example.json config/llm_config.json
```

`base_config.example.json` 已包含完整原有字段和 MCP 节点。默认 `mcp.enabled=false`，配置好真实 MCP 地址、CA 和 Token 后再改为 `true`。

## MCP 示例

| 文件 | 用途 | 是否用于5091正式部署 |
|---|---|---|
| `mcp/local-lan.example.json` | 展示 Agent 通过 HTTPS 连接局域网 MCP Server 的完整配置 | 可作为参考，但应把 `mcp` 节点合并到正式 `base_config.json` |
| `mcp/wsl-all-in-one.example.json` | Agent 与测试 MCP Server 同机运行，使用 `http://127.0.0.1:18081/mcp` | 否，仅开发集成测试 |

`wsl-all-in-one.example.json` 使用 loopback 明文 HTTP，仅因为流量不离开本机；不能把地址改成局域网 IP 后用于生产。

## 证书与秘密

```text
config/certs/ca-bundle.crt   公网 HTTPS CA bundle，可通过相对路径加载
```

启动目录为仓库/部署根目录时：

```sh
export SSL_CERT_FILE=./config/certs/ca-bundle.crt
```

LLM API Key 和 MCP Token 不写入 JSON：

```sh
export SILICONFLOW_API_KEY='实际 LLM API Key'
export LITECRAB_DEVICE_01_MCP_TOKEN='与 MCP Server 一致的随机 Token'
```

如果 MCP Server 使用企业或自签名证书，应把签发它的私有 CA 单独放入受控文件，并将设备配置的 `ca_file` 指向该文件；公网 CA bundle 不一定包含私有 CA。
## 长期记忆

`base_config.example.json` 中的 `memory.enabled` 默认是 `false`。设置为 `true`
后，Agent 注册 `memory_remember`，每轮首次模型调用前加载当前用户的摘要和近期事件。
默认保存在 `workspace_root/.crab/memory`。`store_dir` 只能填写工作目录内的相对路径，
不能包含 `..` 或符号链接。

`summary_max_bytes` 最大 2048，`recent_max_bytes` 最大 65536，
`recent_context_items` 最大 8，`max_context_bytes` 最大 4096。
初始化失败时 Agent 继续运行，但本次进程不提供记忆工具。关闭功能不会删除已有记忆文件。
