# LiteCrab SD5091 构建与后台运行

本文只保留 SD5091 使用的 ARM 32-bit hard-float 交叉编译和运行命令。所有编译命令均在 LiteCrab 仓库根目录执行，并假定 `arm-linux-gnueabihf-gcc` 和 `/opt/sd5091-sysroot` 已准备完成。

## 1. Agent 交叉编译

MCP Client 编译进 `litecrab_server`，随 Agent 一起启动，不需要单独运行 MCP Gateway。

```sh
cmake -S . -B build-5091 \
  -DCMAKE_TOOLCHAIN_FILE=aarch32-toolchain.cmake \
  -DSD5091_SYSROOT=/opt/sd5091-sysroot \
  -DCMAKE_BUILD_TYPE=Release \
  -DLITECRAB_BUILD_TESTS=OFF \
  -DLITECRAB_ENABLE_STATIC_LINK=ON \
  -DLITECRAB_WITH_MCP_CLIENT=ON \
  -DLITECRAB_WITH_MCP_HTTP=ON

cmake --build build-5091 -j
```

产物：

```text
build-5091/litecrab_server
```

## 2. Agent 后台启动

将 `litecrab_server`、`config/` 和 `skills/` 放入设备工作目录。仓库已经提供 `config/certs/ca-bundle.crt`；启动命令从当前工作目录使用相对路径加载它。保留设备原有的 `config/base_config.json`，仅参考 `config/mcp/local-lan.example.json`，把其中的 `mcp` 节点合并到 `base_config.json`，不要用 MCP 示例文件覆盖原配置。

```sh
cd /mnt/home/enspire/z00840189
mkdir -p logs run
chmod +x ./build/litecrab_server

export SSL_CERT_FILE=./config/certs/ca-bundle.crt
export SILICONFLOW_API_KEY='replace-with-llm-api-key'
export LITECRAB_DEVICE_01_MCP_TOKEN='replace-with-device-mcp-token'
export LITECRAB_ALARM_USER='replace-with-alarm-user'
export LITECRAB_ALARM_PASSWORD='replace-with-alarm-password'

(
  trap '' HUP
  exec ./build/litecrab_server \
    --config config/base_config.json \
    --llm-config config/llm_config.json \
    --llm-provider siliconflow \
    --workspace . \
    --alarm-base-url https://192.168.8.10 \
    --alarm-insecure-tls \
    --alarm \
    </dev/null >>logs/litecrab_server.stdout.log 2>&1
) &

echo $! > run/litecrab_server.pid
```

查看进程和日志：

```sh
kill -0 "$(cat run/litecrab_server.pid)"
tail -F logs/litecrab_server.stdout.log logs/litecrab.log
```

停止 Agent：

```sh
kill -TERM "$(cat run/litecrab_server.pid)"
```

若不使用 MCP，将 Agent 配置中的 `mcp.enabled` 设置为 `false`；本地 Tool 仍然可用。

若不使用告警监控，将启动参数中的 `--alarm-base-url`、`--alarm-insecure-tls` 和 `--alarm` 删除，改为 `--no-alarm`；此时不需要设置 `LITECRAB_ALARM_USER` 和 `LITECRAB_ALARM_PASSWORD`。

## 3. Skill 脚本交叉编译

当前需要独立编译的 Skill 程序是 `PLC_Diagnosis/scripts/main`：

```sh
make -C skills/PLC_Diagnosis/scripts clean

make -C skills/PLC_Diagnosis/scripts \
  CROSS_COMPILE=arm-linux-gnueabihf- \
  STATIC=1 \
  SYSROOT=/opt/sd5091-sysroot

make -C skills/PLC_Diagnosis/scripts check \
  CROSS_COMPILE=arm-linux-gnueabihf- \
  STATIC=1 \
  SYSROOT=/opt/sd5091-sysroot
```

产物：

```text
skills/PLC_Diagnosis/scripts/main
```

## 4. MCP Server 交叉编译

设备端 MCP Server 是独立 C11 程序，不链接 Agent、LLM、Session 或 Alarm：

```sh
cmake -S others/mcp_server -B build-device-5091 \
  -DCMAKE_TOOLCHAIN_FILE="$PWD/aarch32-toolchain.cmake" \
  -DSD5091_SYSROOT=/opt/sd5091-sysroot \
  -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_EXE_LINKER_FLAGS=-static

cmake --build build-device-5091 -j
```

产物：

```text
build-device-5091/litecrab_device_mcp
```

## 5. MCP Server 后台运行

将 `litecrab_device_mcp` 放到提供设备能力的 SD5091/Linux 设备。跨设备监听必须配置 Bearer Token：

```sh
cd /mnt/home/enspire/litecrab-mcp
chmod +x litecrab_device_mcp

export LITECRAB_DEVICE_MCP_TOKEN='replace-with-a-random-device-secret'

(
  trap '' HUP
  exec ./litecrab_device_mcp \
    --listen 0.0.0.0 \
    --port 18443 \
    --workers 4 \
    --max-body 262144 \
    --token-env LITECRAB_DEVICE_MCP_TOKEN \
    </dev/null >>logs/litecrab_device_mcp.log 2>&1
) &

echo $! > run/litecrab_device_mcp.pid
```

查看进程和日志：

```sh
kill -0 "$(cat run/litecrab_device_mcp.pid)"
tail -F logs/litecrab_device_mcp.log
```

停止 MCP Server：

```sh
kill -TERM "$(cat run/litecrab_device_mcp.pid)"
```

以上后台启动方式只使用 ash 的 `trap`、子 shell 和重定向，不依赖设备上不存在的 `nohup`、`setsid` 或 `disown`。`trap '' HUP` 会让 `exec` 后的程序继承“忽略 SIGHUP”，关闭终端时不会因为挂断信号退出。

当前 MCP Server 提供 HTTP endpoint `/mcp`，自身不内置 TLS。跨设备部署时必须在它前面配置 TLS 终止代理，并让 Agent 使用 `https://<device-ip>:<tls-port>/mcp`；Agent 会拒绝非 loopback 的明文 HTTP MCP 地址。

## 6. 渐进式 Kconfig 架构使用指南

### 6.1 配置分层

当前架构将“编译时选择”与“运行时参数”分开：

| 路径 | 用途 | 是否手工修改 |
|---|---|---|
| `Kconfig` | 定义 Platform、Artifact 和 Capability symbol | 新增配置项时修改 |
| `defconfigs/<product>.defconfig` | 选择一个可发布 Product 的编译时能力 | 修改 Product 能力组合时修改 |
| `products/<product>/product.yml` | 声明 Product ID、defconfig、Prompt 和产物名 | 新增或调整 Product 时修改 |
| `products/<product>/prompt.md` | Product 级系统 Prompt | 按业务需要修改 |
| `build/capabilities.yml` | Capability 与源码、依赖、平台约束的唯一映射 | 新增或拆分 Capability 时修改 |
| `config/` | LLM、MCP、端口和告警等运行时 JSON 配置 | 部署时修改 |
| `tools/kconfig/configure.py` | 校验配置并产生 CMake/GN/注册表/Prompt 文件 | 通常不修改 |

`defconfigs/` 是编译期产品选择，`config/` 是程序启动后读取的运行时配置，两者不可混用。

### 6.2 选择并构建 Product

当前提供三个正式 Profile：

| Profile | 平台/产物 | 用途 |
|---|---|---|
| `diagnostic_sd5091` | POSIX 可执行程序 | 含 Alarm、MCP、TCP Gateway、Skill 和本地 Tool 的诊断 Agent |
| `energy_inspection` | POSIX 可执行程序 | 能源巡检 Agent，不编译 Alarm 和 Process Tool |
| `robot_hi3516` | 静态库 `liblitecrab_agent.a` | Hi3516 嵌入式 Agent API、Robot Tool 和 TTS 组合 |

默认为 SD5091 构建 ARM 32-bit hard-float 诊断版。该命令必须在已准备
`arm-linux-gnueabihf-gcc` 和 `/opt/sd5091-sysroot` 的 WSL 中执行：

```sh
cmake -S . -B build-diagnostic-5091 \
  -DCMAKE_TOOLCHAIN_FILE=aarch32-toolchain.cmake \
  -DSD5091_SYSROOT=/opt/sd5091-sysroot \
  -DLITECRAB_PROFILE=diagnostic_sd5091 \
  -DCMAKE_BUILD_TYPE=Release \
  -DLITECRAB_BUILD_TESTS=OFF \
  -DLITECRAB_ENABLE_STATIC_LINK=ON
cmake --build build-diagnostic-5091 -j2
```

可部署产物为 `build-diagnostic-5091/litecrab_server`。拷贝到设备前应确认它是
ELF32 ARM，而不是 WSL 本机的 ELF64 x86-64：

```sh
file build-diagnostic-5091/litecrab_server
readelf -h build-diagnostic-5091/litecrab_server | grep -E 'Class|Machine'
```

预期结果为 `ELF 32-bit` 和 `Machine: ARM`。交叉编译产物不能在 x86-64 WSL
中直接运行，因此该构建关闭了测试，不对它执行 `ctest`。

如需在 WSL 本机运行单元测试，必须使用独立构建目录，且不得将该 x86-64
产物部署到 SD5091：

```sh
cmake -S . -B build-diagnostic-host \
  -DLITECRAB_PROFILE=diagnostic_sd5091 \
  -DCMAKE_BUILD_TYPE=Release
cmake --build build-diagnostic-host -j2
ctest --test-dir build-diagnostic-host --output-on-failure
```

构建其他 Product 只需替换 Profile：

```sh
cmake -S . -B build-energy -DLITECRAB_PROFILE=energy_inspection
cmake --build build-energy -j2

cmake -S . -B build-robot -DLITECRAB_PROFILE=robot_hi3516 -DLITECRAB_BUILD_TESTS=OFF
cmake --build build-robot --target litecrab -j2
```

### 6.3 生成器和生成产物

在不编译的情况下检查 Product 配置：

```sh
python3 tools/kconfig/configure.py \
  --profile diagnostic_sd5091 \
  --check
```

CMake 配置时会自动调用生成器，默认输出到：

```text
<cmake-build-dir>/generated/<product>/
├── .config
├── include/litecrab/autoconf.h
├── litecrab_sources.cmake
├── litecrab_sources.gni
├── generated_capability_registry.inc
├── generated_product_prompt.c
└── litecrab_capabilities.json
```

这些文件是构建产物，不应手工编辑或提交。需要改变能力时，修改对应 defconfig 后重新运行 CMake。

### 6.4 能力开关与裁剪

defconfig 中：

```text
CONFIG_LITECRAB_CAP_MCP_CLIENT=y
# CONFIG_LITECRAB_CAP_ALARM is not set
```

`=y` 表示该 Capability 会进入源码列表和生命周期注册表；`is not set` 表示其实现不编译、不链接、不启动。生成器会拒绝以下错误：

- 未定义的 symbol；
- 缺失的 Capability 依赖；
- 互斥 Capability 同时启用；
- 当前 Platform 不支持的能力；
- Manifest 源文件不存在或路径越界。

`LITECRAB_CONFIG_FRAGMENT` 仅用于测试临时裁剪，不应代替正式 defconfig：

```sh
cmake -S . -B build-no-alarm \
  -DLITECRAB_PROFILE=diagnostic_sd5091 \
  -DLITECRAB_CONFIG_FRAGMENT="$PWD/tests/config_matrix/fragments/no_alarm.conf" \
  -DLITECRAB_BUILD_TESTS=OFF
cmake --build build-no-alarm -j2
```

### 6.5 新增一个复用已有能力的 Product

新增 Product 时不复制 C 源码，只增加：

```text
defconfigs/<product>.defconfig
products/<product>/product.yml
products/<product>/prompt.md
tests/products/<product>_smoke.yml
```

`product.yml` 的 `id` 、`defconfig` 路径与 `CONFIG_LITECRAB_PRODUCT_ID` 必须一致。完成后先检查配置，再构建：

```sh
python3 tools/kconfig/configure.py --profile <product> --check
cmake -S . -B build-<product> -DLITECRAB_PROFILE=<product>
cmake --build build-<product> -j2
```

如果新场景需要新协议、硬件或业务状态机，应先实现一个独立 Capability，在 `Kconfig` 定义 symbol，并在 `build/capabilities.yml` 声明源码、依赖和平台约束；不应将新功能直接写进 Product 目录。

### 6.6 验证命令

运行配置生成器测试：

```sh
python3 -B -m unittest -v tests/config_matrix/test_configure.py
```

运行全部 Product/裁剪矩阵：

```sh
python3 tests/config_matrix/run_matrix.py
```

架构设计、阶段实施和已验证边界见 [`docs/kconfig/README.md`](docs/kconfig/README.md) 和 [`docs/kconfig/litecrab_kconfig_implementation/IMPLEMENTATION_STATUS.md`](docs/kconfig/litecrab_kconfig_implementation/IMPLEMENTATION_STATUS.md)。
