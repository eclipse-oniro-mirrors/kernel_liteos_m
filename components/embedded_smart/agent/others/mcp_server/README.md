# LiteCrab Device MCP Server

独立 C11 设备端 MCP Server。它不链接 Agent、LLM、Session 或 Alarm 模块，只提供标准
Streamable HTTP `/mcp` endpoint。

支持 MCP `2026-07-28`（默认）和 `2025-11-25` 兼容生命周期，提供四个只读演示工具：
`device.get_status`、`device.echo`、`device.add` 和用于超时测试的 `device.delay`。

构建：

```sh
cmake -S others/mcp_server -B build-device-mcp
cmake --build build-device-mcp -j2
```

仅本机测试：

```sh
./build-device-mcp/litecrab_device_mcp --listen 127.0.0.1 --port 18081
```

设备网络监听必须配置 Token：

```sh
export LITECRAB_DEVICE_MCP_TOKEN='replace-with-device-secret'
./litecrab_device_mcp --listen 0.0.0.0 --port 18081 \
  --token-env LITECRAB_DEVICE_MCP_TOKEN
```

生产跨机部署应在本进程之前配置 TLS 终止或为 Server 增加原生 TLS；Agent 端对非 loopback
明文 HTTP 配置会直接拒绝。
