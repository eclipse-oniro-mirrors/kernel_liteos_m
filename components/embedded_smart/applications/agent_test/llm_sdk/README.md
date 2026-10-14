# llm_sdk — 镜像内客户接口包

## 两个库

### 1. llm_client — LLM 问答
| 函数 | 通道 | key |
|---|---|---|
| `llm_chat(&gw, msg, out, len)` | 板 → 你的网关 (TLS1.2 直连, 自带 IP+域名+key) | 你提供 |
| `llm_ask(msg, out, len)` | 板 → PC 中继 (192.168.1.1:8443) | 免 key |

### 2. wifi_client — WiFi 连接
| 函数 | 作用 |
|---|---|
| `wifi_connect(conf)` | 连接(幂等, 后台守护自动重连, 热点可见即自动连上) |
| `wifi_get_ip(ip, len)` | 查当前 IP(立即返回) |
| `wifi_wait_connected(sec, ip, len)` | 阻塞等待连接(带超时) |

## 文件清单
| 文件 | 用途 |
|---|---|
| llm_client.h / libllmclient.a / .so | LLM 接口 |
| wifi_client.h / libwificlient.a / .so | WiFi 接口 |
| chat.c / chat_direct.c / wifi_test.c | 官方示例源码 |
| build_llmclient.sh / build_wificlient.sh | 构建脚本(全套交叉编译旗标) |

## 快速接入

```c
#include "llm_client.h"
#include "wifi_client.h"

/* WiFi: 连上再说 */
wifi_connect(NULL);                 /* 默认 /userdata/wpa_supplicant.conf */
char ip[32];
wifi_wait_connected(30, ip, sizeof(ip));

/* LLM: 客户网关直连 */
llm_gateway_t gw = {
    .ip = "139.196.152.242", .vhost = "api.siliconflow.cn",
    .port = 443, .api_key = "sk-你的key",
    .model = "deepseek-ai/DeepSeek-V4-Flash",
};
char reply[4096];
int n = llm_chat(&gw, "你的消息", reply, sizeof(reply));
```

## 两种链接方式（.a 静态 / .so 动态）——按场景选

| 场景 | 选择 | 理由 |
|---|---|---|
| 只有一个程序用 SDK（**默认推荐**） | `.a` 静态链 | 产物单文件、无部署依赖，不踩板上 ldso 的坑 |
| 镜像里多个进程都要调 SDK | `.so` 动态链 | 多进程共享一份代码省 flash；升级 SDK 只换一个 .so，业务程序不用重编 |

### 静态链（板上验收走的就是这条）

```bash
clang --target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 \
      --sysroot=$SYSROOT -I. 你的代码.c libllmclient.a libwificlient.a \
      -L$OUT/usr/lib -lmbedtls -o 你的程序
```

产物单文件，烧上板就能跑（用 `exec /bin/你的程序 参数` 调用）。

### 动态链

```bash
clang --target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 \
      --sysroot=$SYSROOT -I. 你的代码.c -L. -lllmclient -lwificlient \
      -L$OUT/usr/lib -lmbedtls -o 你的程序
```

部署时**必须**把 `libllmclient.so`、`libwificlient.so` 放到板上 **`/usr/lib/`**：
- ⚠️ 板上 ldso 不认 `LD_LIBRARY_PATH`，放别的目录加载不到
- 交付的 `rootfs_LLM_v1.img` 已预置 `libllmclient.so` 于 /usr/lib，可直接用

GN 集成想用 `.so`：写法同《量产集成说明》坑4——`.so` 用 `rebase_path()` 绝对路径塞进
`libs`，并确保 .so 进镜像 /usr/lib（mtools 注入或改源码树重编）。量产路径实测的是 `.a`，无特殊需求不要换。

## 错误码
- llm: -1连不上 -2发送失败 -3超时(90s内) -4非200 -5缓冲区小 -6参数 -7TLS失败 -8内存
- wifi: -1参数 -2fork失败 -3守护缺失 -4未连接/超时 -5网卡查询失败

## 注意事项(板上实测坑)
1. 网关地址传 IP(板上 DNS 会崩), vhost 传域名; IP 变了 nslookup 重查
2. 静态链优先(.a); .so 须放 /usr/lib(板上不认 LD_LIBRARY_PATH)
3. llm_chat 走 WiFi 出口, llm_ask 走网线(PC 中继) — 互为备份
4. 消息上限 1KB, 回复缓冲区 >= 4096; 两库均可多线程调用(内部串行化)
5. WiFi 热点休眠(如 iPhone 锁屏)时守护持续扫描, 热点醒来自动连上

