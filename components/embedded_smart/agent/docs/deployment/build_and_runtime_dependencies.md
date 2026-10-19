# 当前架构构建与运行环境、库函数依赖总结

> 本文以当前代码为准，归纳 LiteCrab Agent（`litecrab_server`）、设备端 MCP Server（`litecrab_device_mcp`）和 PLC_Diagnosis Skill（`skills/PLC_Diagnosis/scripts/main`）三个产物的构建环境、运行环境、外部库和系统头文件依赖。具体模块行为请配合 [`docs/current_architecture_index.md`](../current_architecture_index.md) 阅读。

## 1. 产物与构建关系

| 产物 | 路径 | 源代码范围 | 构建方式 | 链接形态 |
|---|---|---|---|---|
| `litecrab_server` | `build-5091/litecrab_server` | `src/*.c` + `src/main.c` + `src/alarm/alarm_app.c` | CMake 主项目 | armhf 全静态 |
| `litecrab_device_mcp` | `build-device-5091/litecrab_device_mcp` | `others/mcp_server/src/server.c` + `src/util/json.c` | CMake 子项目 | armhf 全静态（`-static`） |
| `skills/PLC_Diagnosis/scripts/main` | `skills/PLC_Diagnosis/scripts/main` | `skills/PLC_Diagnosis/scripts/main.c` + `asp/*.c` + `src/util/json.c` | Make | armhf 全静态（`STATIC=1`） |

主项目 CMake 关键开关：`LITECRAB_ENABLE_STATIC_LINK`、`LITECRAB_WITH_MCP_CLIENT`、`LITECRAB_WITH_MCP_HTTP`（后两者互为前置）。`LITECRAB_BUILD_TESTS` 控制是否额外构建 `litecrab_tests`、`litecrab_security_tests`、`litecrab_alarm_tests`、`litecrab_mcp_client_probe`、`litecrab_device_mcp_test` 等测试程序。

## 2. 编译环境（WSL 主机）

| 项目 | 要求 | 安装命令 |
|---|---|---|
| 操作系统 | WSL x86_64 Linux | — |
| C 标准 | C11（`CMAKE_C_STANDARD 11`、`CMAKE_C_EXTENSIONS ON`） | — |
| 交叉编译器 | `arm-linux-gnueabihf-gcc` | `sudo apt-get install -y gcc-arm-linux-gnueabihf` |
| 通用构建工具 | `cmake ≥ 3.16`、`make`、`perl`（OpenSSL 构建需要） | `sudo apt-get install -y build-essential cmake make perl` |
| 下载工具 | `wget` 或 `curl` | `sudo apt-get install -y wget` |
| Python 3（仅测试） | 解释器，运行 `tests/*.py` E2E 用例 | `sudo apt-get install -y python3` |

CMake 工具链文件 `aarch32-toolchain.cmake` 固定使用 `arm-linux-gnueabihf-` 前缀，并设置 `CMAKE_FIND_ROOT_PATH` 指向 sysroot，使 `find_package(OpenSSL/CURL/ZLIB)` 只在 sysroot 中查找。

WSL 本地动态编译（x86_64）也支持，不需要工具链文件，但产物不能在 SD5091 上运行：

```sh
cmake -S . -B build && cmake --build build -j
```

## 3. SD5091 armhf sysroot

由 `others/5091-build/scripts/build_sd5091_sysroot.sh` 一次性构建，默认安装到 `/opt/sd5091-sysroot`。三个外部库全部以静态 `.a` 形态提供，版本与 5091 设备匹配：

| 库 | 版本 | 静态产物 | 头文件 |
|---|---|---|---|
| zlib | 1.3.1 | `usr/lib/libz.a` | `usr/include/zlib.h` |
| OpenSSL | 3.0.9 | `usr/lib/libssl.a`、`usr/lib/libcrypto.a` | `usr/include/openssl/ssl.h` 等 |
| libcurl | 8.11.0（默认） | `usr/lib/libcurl.a` | `usr/include/curl/curl.h` |

libcurl 配置已关闭 brotli/zstd/nghttp2/ngtcp2/nghttp3/libpsl/libssh2/librtmp/libidn2 等可选依赖，确保全静态链接无遗漏。链接顺序敏感，CMake 和 Makefile 都按 `libcurl → libssl → libcrypto → libz → libdl → libpthread` 顺序提供。

CMake 主项目对 `LITECRAB_ENABLE_STATIC_LINK=ON` 自动设置 `OPENSSL_USE_STATIC_LIBS=TRUE` 并显式追加 `CURL::libcurl ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto ${CMAKE_DL_LIBS} Threads::Threads`。

## 4. SD5091 设备运行环境

| 项目 | 实际值 | 影响 |
|---|---|---|
| 内核 | Linux 5.10.0 aarch64 SMP PREEMPT | 用户空间仍为 32 位 ARM |
| 用户空间架构 | 32 位 ARM（armv7 hard-float），ELF32 / EM_ARM | 必须使用 `arm-linux-gnueabihf-gcc` |
| 动态加载器 | `/lib/ld-linux.so.3` | aarch64 加载器无法运行本产物 |
| OS | RTOS 208.8.1.SPC0700（busybox 1.34.1） | 无 systemd、无 `nohup`/`setsid`/`disown`；后台启动用 `trap '' HUP` + 子 shell |
| C 运行库 | glibc（libc.so.6 + 独立 libpthread.so.0） | — |
| OpenSSL | 3.0.9 动态库（`/lib/libssl.so.3`） | 全静态产物不依赖；环境变量 `SSL_CERT_FILE` 仍生效 |
| zlib | 1.3.1（`/lib/libz.so.1`） | 全静态产物不依赖 |
| libcurl | **无** | 三个产物必须全静态，不能动态链接 libcurl |
| 编译工具 | gcc/cmake/make/ar 全部缺失 | 不能在设备上编译，必须从 WSL 交叉编译 |
| Python3 | **无** | Transfer Station 和部分 E2E 测试需在 PC 上运行 |
| CA 证书 | **无** | 必须从 WSL 复制 `ca-bundle.crt` 并设置 `SSL_CERT_FILE` |
| DNS | `/etc/resolv.conf` 为空 | 启动前需 `su -c 'echo nameserver 8.8.8.8 > /etc/resolv.conf'` |
| 可写目录 | 仅 `/mnt/home/enspire`（HOME） | 日志、Session、Trace、Alarm spool 必须落在 HOME 下 |
| 内存 | 724 MB total，约 320 MB available | LLM `max_tokens` 建议 ≤ 2048 |
| `ulimit -n` | 1024 | 高并发需 root 提升到 65536 |
| init | busybox init | 进程托管需 `start-stop-daemon` 或自有拉起脚本 |

## 5. 外部库函数依赖

### 5.1 libcurl（HTTP/HTTPS 客户端）

使用文件：

| 文件 | 用途 |
|---|---|
| `src/alarm/asp_client.c` | ASP 告警源 polling/subscription，`CURLM` multi 接口 |
| `src/mcp_client/client_manager.c` | 全局 `curl_global_init`，进程级初始化 |
| `src/mcp_client/http_transport.c` | Streamable HTTP MCP 传输，easy 接口 POST + SSE |
| `skills/PLC_Diagnosis/scripts/main.c` | 进程级 `curl_global_init`/`curl_global_cleanup` |
| `skills/PLC_Diagnosis/scripts/asp/http.c` | PLC ASP HTTP GET/POST，TLS 校验开关 |

调用的核心函数族：

- 进程级：`curl_global_init`、`curl_global_cleanup`、`curl_global_*`
- Easy 句柄：`curl_easy_init`、`curl_easy_setopt`、`curl_easy_perform`、`curl_easy_getinfo`、`curl_easy_strerror`、`curl_easy_cleanup`
- Multi 句柄（仅 `asp_client.c`）：`curl_multi_init`、`curl_multi_add_handle`、`curl_multi_remove_handle`、`curl_multi_perform`、`curl_multi_poll`、`curl_multi_info_read`、`curl_multi_cleanup`
- MIME 表单：`curl_mime_init`、`curl_mime_addpart`、`curl_mime_name`、`curl_mime_data`、`curl_mime_free`
- Headers：`curl_slist_append`、`curl_slist_free_all`
- 常用 `CURLOPT_*`：`URL`、`HTTPGET`、`POST`、`POSTFIELDS`、`POSTFIELDSIZE`、`HTTPHEADER`、`MIMEPOST`、`WRITEFUNCTION`、`WRITEDATA`、`HEADERFUNCTION`、`HEADERDATA`、`CONNECTTIMEOUT_MS`、`TIMEOUT_MS`、`FOLLOWLOCATION`、`NOSIGNAL`、`COOKIEFILE`、`SSL_VERIFYPEER`、`SSL_VERIFYHOST`、`CAINFO`、`SSLCERT`、`SSLKEY`、`PROTOCOLS`、`REDIR_PROTOCOLS`
- 常用 `CURLINFO_*`：`RESPONSE_CODE`
- 错误码/常量：`CURLM_OK`、`CURLMSG_DONE`、`CURLcode`、`CURLE_OK`、`CURLE_OPERATION_TIMEDOUT`、`CURLPROTO_HTTP`、`CURLPROTO_HTTPS`、`CURL_ZERO_TERMINATED`、`CURL_GLOBAL_DEFAULT`

### 5.2 OpenSSL（TLS + SHA-256）

OpenSSL 在主项目中以两种方式使用：`src/kernel/llm.c` 用显式 `extern` 声明调用稳定 ABI，避免依赖完整开发头；`src/alarm/alarm_notify.c` 和 `src/kernel/skill.c` 使用具体函数。

使用文件：

| 文件 | 用途 |
|---|---|
| `src/kernel/llm.c` | LLM HTTPS 直连，`SSL_CTX` + `SSL` 全套 TLS 客户端握手与读写 |
| `src/alarm/alarm_notify.c` | 告警去重 key，`SHA256()` 一次性摘要 |
| `src/kernel/skill.c` | Skill run 唯一 ID，`EVP_*` 增量 SHA-256 |

LLM TLS 路径调用的函数：

- 上下文：`TLS_client_method`、`SSL_CTX_new`、`SSL_CTX_free`、`SSL_CTX_set_default_verify_paths`、`SSL_CTX_set_verify`
- 连接：`SSL_new`、`SSL_free`、`SSL_set_fd`、`SSL_ctrl`（`SSL_CTRL_SET_TLSEXT_HOSTNAME`）、`SSL_get0_param`、`X509_VERIFY_PARAM_set1_host`、`SSL_connect`、`SSL_get_verify_result`、`SSL_shutdown`
- 读写：`SSL_write`、`SSL_read`
- 常量：`SSL_VERIFY_PEER`、`SSL_CTRL_SET_TLSEXT_HOSTNAME`、`TLSEXT_NAMETYPE_host_name`

告警/Skill 哈希路径调用的函数：

- `SHA256`、`SHA256_DIGEST_LENGTH`
- `EVP_MD_CTX_new`、`EVP_MD_CTX_free`、`EVP_sha256`、`EVP_DigestInit_ex`、`EVP_DigestUpdate`、`EVP_DigestFinal_ex`

## 6. POSIX / libc 系统头文件依赖

按用途分组（来源：`src/*.c`、`others/mcp_server/src/*.c`、`skills/PLC_Diagnosis/scripts/**/*.c`）：

### 6.1 标准库

| 头文件 | 用途 | 主要使用文件 |
|---|---|---|
| `stdio.h` | 文件 I/O、`snprintf`、日志 | 几乎全部 `.c` |
| `stdlib.h` | 内存分配、退出、字符串转数字 | 几乎全部 `.c` |
| `string.h` | 内存/字符串操作 | 几乎全部 `.c` |
| `strings.h` | `strcasecmp`/`strncasecmp` | `llm.c`、`observability.c`、`builtin_tools.c`、`server.c` |
| `stdint.h` | 固定宽度整数 | `alarm_service.c`、`config.c`、`skill.c`、`session.c`、`server.c` |
| `stdarg.h` | 变参 | `observability.c`、`builtin_tools.c`、`json.c` |
| `limits.h` | 数值上限 | `skill.c`、`builtin_tools.c` |
| `ctype.h` | 字符分类 | `alarm_notify.c`、`config.c`、`tcp_line.c`、`skill.c`、`working_memory.c`、`client_manager.c`、`http_transport.c`、`observability.c`、`builtin_tools.c`、`json.c`、`server.c` |
| `errno.h` | 错误码 | `main.c`、`alarm_service.c`、`config.c`、`tcp_line.c`、`hub.c`、`llm.c`、`skill.c`、`observability.c`、`builtin_tools.c`、`session.c`、`json.c`、`server.c` |

### 6.2 时间与时钟

| 头文件 | 用途 |
|---|---|
| `time.h` | `clock_gettime(CLOCK_MONOTONIC)`、`localtime_r`、`strftime`、`usleep` |
| `sys/time.h` | `gettimeofday`、`struct timeval` |

使用文件：`alarm_service.c`、`tcp_line.c`、`hub.c`、`llm.c`、`skill.c`、`observability.c`、`builtin_tools.c`、`session.c`、`server.c`。

### 6.3 线程与原子

| 头文件 | 用途 |
|---|---|
| `pthread.h` | mutex/cond/timedjoin_np/create/join；告警 listener+dispatcher 线程、Agent 主线程、Gateway worker、Hub、Skill run store、Session |
| `stdatomic.h` | LLM cancel epoch、curl init once、server 状态 |

使用文件：`alarm_service.c`、`asp_client.c`、`tcp_line.c`、`hub.c`、`kernel.c`、`skill.c`、`client_manager.c`、`observability.c`、`session.c`、`server.c`、`llm.c`。

`pthread_condattr_setclock(CLOCK_MONOTONIC)`、`pthread_timedjoin_np` 是 glibc 扩展，依赖 `_GNU_SOURCE`（CMake 已为相关 target 设置）。

### 6.4 文件系统与目录

| 头文件 | 用途 |
|---|---|
| `dirent.h` | 目录遍历（Skill 发现、Session 扫描、Workspace walker） |
| `fcntl.h` | `open`、文件锁、`O_*` 标志 |
| `sys/stat.h` | `stat`、`mkdir`、权限位 |
| `sys/types.h` | `mode_t`、`pid_t` 等类型 |

使用文件：`alarm_service.c`、`skill.c`、`observability.c`、`builtin_tools.c`、`session.c`。

### 6.5 进程与信号

| 头文件 | 用途 |
|---|---|
| `signal.h` | `SIGTERM`/`SIGINT`/`SIGHUP`/`SIGPIPE` 处理 |
| `sys/wait.h` | `waitpid`、`WIFEXITED` 等 |
| `sys/resource.h` | `setrlimit`（RLIMIT_CPU/AS/NOFILE/NPROC） |
| `sys/syscall.h` | `SYS_*`、`syscall()` |

使用文件：`main.c`、`tcp_line.c`、`builtin_tools.c`、`server.c`。`builtin_tools.c` 是 `exec_program` 资源隔离的核心。

### 6.6 网络

| 头文件 | 用途 |
|---|---|
| `sys/socket.h` | socket/connect/bind/listen/accept/send/recv/setsockopt |
| `netinet/in.h` | `sockaddr_in`、`INADDR_ANY`、`htons` |
| `netinet/tcp.h` | `TCP_NODELAY` |
| `arpa/inet.h` | `inet_addr`、`inet_ntop` 等 |
| `netdb.h` | `getaddrinfo`/`freeaddrinfo`（LLM 域名解析） |
| `poll.h` | `poll`、`POLLIN`/`POLLOUT` |

使用文件：`tcp_line.c`（Gateway）、`llm.c`（LLM 客户端）、`server.c`（MCP Server）、`builtin_tools.c`（子进程 IO 复用）。

### 6.7 其他

| 头文件 | 用途 |
|---|---|
| `unistd.h` | `read`/`write`/`close`/`fork`/`execve`/`pipe`/`dup2`/`access`/`getcwd` |
| `fnmatch.h` | glob 模式匹配（`builtin_tools.c`） |
| `curl/curl.h` | 见 §5.1 |
| `openssl/sha.h` | 见 §5.2 |

## 7. CMake / Make 链接关系

主项目 `litecrab_server`：

```
litecrab (STATIC)
  → Threads::Threads  OpenSSL::SSL  OpenSSL::Crypto  m
litecrab_alarm_core (STATIC) → litecrab
litecrab_alarm_curl (STATIC) → litecrab_alarm_core + CURL::libcurl
litecrab_mcp_client (STATIC) → litecrab + CURL::libcurl   [LITECRAB_WITH_MCP_CLIENT=ON]
litecrab_server (EXE)
  → litecrab + litecrab_alarm_curl [+ litecrab_mcp_client]
  → 静态时追加：CURL::libcurl ZLIB::ZLIB OpenSSL::SSL OpenSSL::Crypto
              ${CMAKE_DL_LIBS} Threads::Threads
```

设备 MCP Server `litecrab_device_mcp`（`others/mcp_server/CMakeLists.txt`）：

```
litecrab_device_mcp → Threads::Threads + ../../src/util/json.c
CMAKE_EXE_LINKER_FLAGS=-static
```

PLC_Diagnosis `main`（`skills/PLC_Diagnosis/scripts/Makefile`）：

```
main → $(CURL_STATIC_LIBS) = -lcurl -lssl -lcrypto -lz -ldl -lpthread   [STATIC=1]
main → -lcurl                                                           [STATIC=0, WSL 本地]
```

## 8. 编译定义

CMake 通过 `target_compile_definitions` 强制以下宏：

- `litecrab`：`_POSIX_C_SOURCE=200809L`、`_XOPEN_SOURCE=700`、`_GNU_SOURCE`
- `litecrab_alarm_core`、`litecrab_alarm_curl`、`litecrab_mcp_client`、`litecrab_device_mcp`、`litecrab_device_mcp_test`：`_GNU_SOURCE`
- `litecrab_server`：`LITECRAB_BUILD_REVISION="<git>"`、`LITECRAB_BUILD_TIME_UTC="<UTC>"`；MCP 启用时再加 `LITECRAB_WITH_MCP_CLIENT=1`
- PLC_Diagnosis Makefile：`-std=gnu11`、`-O2`，可选 `DEVICE_CA_FILE="<path>"`

`_GNU_SOURCE` 用于 `pthread_timedjoin_np`、`pthread_condattr_setclock(CLOCK_MONOTONIC)`、`strerror_r` 等扩展。

## 9. 编译选项一致性约束

- `LITECRAB_WITH_MCP_HTTP=ON` 要求 `LITECRAB_WITH_MCP_CLIENT=ON`；反之当前实现也强制 MCP Client 依赖 HTTP 传输，二者必须同时开或同时关。
- `WIN32`（非 CYGWIN/MSYS）直接 `FATAL_ERROR`，禁止原生 Windows 编译。
- 主项目 `LITECRAB_ENABLE_STATIC_LINK=ON` 时自动追加 zlib/dl/pthread，并在 GCC/Clang 下加 `-static` 链接选项。

## 10. 运行时环境变量

| 变量 | 必填 | 用途 |
|---|---|---|
| `SSL_CERT_FILE` | 是 | OpenSSL 3.x 加载 CA bundle，Agent 和 Skill 都依赖 |
| `SILICONFLOW_API_KEY` / `RELAYHUB_API_KEY` | 是（按 provider） | LLM Provider API Key，仅被 `llm_config.json` 引用，不写入 JSON |
| `LITECRAB_DEVICE_01_MCP_TOKEN` | MCP 启用时必填 | Agent 端读取后作为 MCP Server Bearer Token |
| `LITECRAB_DEVICE_MCP_TOKEN` | MCP Server 必填 | `litecrab_device_mcp` 鉴权 Token，与 Agent 端一致 |
| `LITECRAB_ALARM_USER` / `LITECRAB_ALARM_PASSWORD` | `--alarm` 启用时必填 | ASP 告警源登录凭据，不写入配置文件 |

## 11. 运行时文件依赖

启动目录（板端 HOME 下 `litecrab/`）必须包含：

```text
./litecrab_server                                  主程序
./config/base_config.json                          Agent/Gateway/Workspace/MCP 配置
./config/llm_config.json                           LLM Provider 配置
./config/certs/ca-bundle.crt                       公网 CA bundle（由 SSL_CERT_FILE 指向）
./skills/PLC_Diagnosis/scripts/main                PLC 诊断二进制（exec_program 调用）
./skills/PLC_Diagnosis/SKILL.md                    Skill 正文，运行时由 skill_read 加载
./skills/PLC_Diagnosis/references/*.md             问题分类执行指南
./skills/workspace_summary/SKILL.md                Workspace 摘要 Skill
```

运行时写入目录（必须在可写 HOME 内）：

```text
logs/                       文本日志 + JSONL Trace（2 MB × 4 代轮转）
run/                        pid 文件
.crab/alarm/                durable alarm spool（两阶段 ACK）
.crab/session/              持久 Session 快照（128 文件 / 32 MB / 7 天 TTL）
```

## 12. 参考入口

- 编译运行总览：[`README.md`](../../README.md)
- 部署详细步骤：[`sd5091_deployment.md`](sd5091_deployment.md)
- 当前实现功能与边界：[`sd5091_current_implementation_guide.md`](sd5091_current_implementation_guide.md)
- 长期运行目标架构：[`sd5091_long_running_architecture.md`](sd5091_long_running_architecture.md)
- MCP 双模架构：[`mcp_dual_mode_agent_architecture.md`](mcp_dual_mode_agent_architecture.md)
- 总体模块架构：[`agent_framework_design.md`](../agent_framework_design.md)
