# RK3568 MCS 测试套件（主从核协同，host 调度）

mcs 框架域测试：mica 服务（rpc/tty/umt）、np socket 代理、pfs 代理文件系统、
system 服务生命周期、combo 跨服务并发。**与驱动套件相反：从核不随 boot 跑批**，
而是注册 `rpmsg-mcs-test` 服务等待主核命令（host 调度形态）——用例按模块/单例
按需执行，结果经 rpmsg 回传主核。

## 一、编译

### 1.1 从核固件（evolve 工程）

产品 `rk3568_m_testsuits`，形态 B 配方（**先打本地配套 patch**，见
`patches/README.md`——0001 必打=TEST_MCS_SUITE 编译依赖，0002/0003 强烈
建议=已知缺陷规避）：

```bash
# kernel/liteos_m 仓根：打本地 patch（上游仓边界外的配套改动）
for p in kernel/liteos_m/testsuites/sample/mcs/patches/000[1-3]*.patch; do
    patch -p1 < "$p"
done
```

debug.config（标准 Kconfig 语义，`is not set` 行正常生效）：

```
LOSCFG_KERNEL_TEST=y                    # 保留：LOSCFG_TEST 使能→关板级 demo
# LOSCFG_TEST_ICUNIT_KERNEL is not set  # 关 kernel 批（与调度并发=禁）
LOSCFG_TEST_MCS_SUITE=y                 # 编入 mcs 用例 + 测试调度服务
```

> **改 config 后必须删缓存再编**：`rm -rf out/rk3568_m out/preloader`
> （解析缓存会让 config 修改不生效——增量不可信）。

```bash
cd <evolve 工程根>
./build.sh --product-name rk3568_m_testsuits
# 产物：out/rk3568_m/rk3568_m_testsuits/OHOS_Image（~350KB，纯调度形态）
```

编译入口（默认 n，上库安全）：

```
config TEST_MCS_SUITE
    bool "testsuites/sample/mcs (mcs framework cases, host-scheduled)"
    default n
    depends on PLATFORM_RK3568_M
    depends on MCS
```

### 1.2 主核侧工具（host/）

```bash
cd kernel/liteos_m/testsuites/sample/mcs/host
./build_host.sh          # 依赖 master-code 树（RK3568_MASTER_ROOT 可覆盖）
                         # 产物：out/host_server + out/mcs_cmd
```

- `host_server`：主核常驻服务（见附录 B.1）
- `mcs_cmd`：命令行客户端（AF_UNIX socket → host_server）
- 交叉编译：OH clang + musl sysroot（`MUSL=` 可覆盖）；libmica/open-amp 预编译件
  来自 master-code 的 soc 仓

### 1.3 部署集

| 文件 | 板上路径 |
|---|---|
| OHOS_Image（从核固件） | /data/liteos-m.elf |
| host_server / mcs_cmd | /data/（chmod +x） |
| mcs_km.ko / mcs_net.ko / mcs_can.ko | /data/ |
| libmica.so / libmetal.so.1 / libopen_amp.so.1 / libc.so / ld-musl-aarch64.so.1 | /data/lib/（loader 路径写死 ELF，目录不可改名） |

## 二、执行

### 2.1 手工执行（hdc）

```bash
# ① 起链路
hdc tconn <板IP>:8710
hdc shell "insmod /data/mcs_km.ko && insmod /data/mcs_net.ko && insmod /data/mcs_can.ko"

# ② 起 host_server（拉起从核 + 常驻被动服务 + 命令 socket）
hdc shell "export LD_LIBRARY_PATH=/data/lib; \
           nohup /data/host_server -f /data/liteos-m.elf > /data/host_srv.log 2>&1 &"

# ③ 命令执行（mcs_cmd）
hdc shell "/data/mcs_cmd status"                 # state=IDLE/RUNNING
hdc shell "/data/mcs_cmd run rpc 1"              # 单用例（模块内 1-based 序号）
hdc shell "/data/mcs_cmd run rpc all"            # 单模块全部
hdc shell "/data/mcs_cmd run all"                # 全部模块（见 §2.3 已知限制）
hdc shell "/data/mcs_cmd stop"                   # 当前用例后停

# ④ 结果
hdc shell "cat /data/host_srv.log"               # ACK/DONE 与被动服务日志
```

两段应答：`ACK`（命令受理）→ `DONE: total=N failed=M`（执行汇总）；
串口侧同步打印用例 `[Passed]/[Failed]` 报告。

### 2.2 自动化执行（board-test-runner）

```bash
<skills>/board-test-runner/scripts/rk3568/run.sh sched all     # 全模块
<skills>/board-test-runner/scripts/rk3568/run.sh sched rpc     # 单模块
<skills>/board-test-runner/scripts/rk3568/run.sh sched rpc 5   # 单用例
# 代理版（串口在对端 PC）：scripts/rk3568/proxy/run.sh sched <模块>
```

sched 模式完成判定 = host 汇总 DONE 行（串口仅诊断）；日志包归档于工程 logs/。

### 2.3 已知限制与工作模式

**RUN_ALL 单次连续 dispatch 的机制缺陷**：`run all` 一次调度全部模块时，后段
（system/combo）出现 master TX buffer 耗尽（host send_resp_frag 失败）——
模块间让拍（20tick/500tick/10s）、vring 刷新帧、模块重排均无效；**同一套用例
逐模块 dispatch 全绿**。**工作模式：逐模块调度**（`run <mod> all` 依次执行；
runner `sched all` 亦按此模式执行）。根因待后续定位（疑 openamp master 侧
buffer 回收依赖会话边界）。

### 2.4 环境依赖（np echo 用例）

| 用例 | 依赖 |
|---|---|
| it_mcs_np_003/005/007/208/209/301、combo 403/410/414 | **外部 echo 主机**：网段内可达主机 UDP:9000 + TCP:9001（对端 PC，`peer_agent.sh installecho` 一键部署；验证从核经 np 代理真实到达外部网络） |
| it_mcs_np_202（板内回环） | 无（host_passive 内置 127.0.0.1:7000/7001） |
| 其余（rpc/tty/umt/pfs/system） | 无外部依赖 |

## 三、维护（加用例 / 屏蔽 / 加模块）

```
mcs/
├── it_mcs.h                  # mcs 公共头（np_* API 声明、echo 宏、E2E opcode）
├── mcs_test_proto.h          # 调度协议（host/slave 共用；帧格式+命令字）
├── mcs_test_service.c        # 调度服务（rpmsg-mcs-test 端点+模块表+dispatch 线程）
├── mica/{rpc,tty,umt}/       # mica 服务域：it_mcs_rpc.c（注册表）+ it_mcs_rpc_001.c...
├── service/{np,pfs,system}/  # 代理服务域：it_mcs_np.c（注册表）+ 用例
├── combo/                    # 跨服务并发：it_mcs_combo.c + it_mcs_combo_401.c...
└── host/                     # 主核侧（host_server/mcs_cmd/build_host.sh）
```

- **编号规则**：`it_mcs_<模块>_<编号>.c`，用例名 `ItMcs<模块><编号>`；
  **001=API / 201=功能 / 301=压力 / combo 401 起**
- **加用例**：写 `it_mcs_rpc_212.c` → 该域注册表 `it_mcs_rpc.c` 加
  extern 声明 + 数组行各一行（**host 侧零修改**）
- **屏蔽用例**：注释注册表数组行（count 由 sizeof 自动收缩）
- **加模块**：建目录+注册表（`mcs_module_t` 自拼 sizeof）→
  `mcs_test_service.c` 模块表加一行

## 附录 A：调度协议（rpmsg-mcs-test）

### A.1 帧格式（mcs_test_proto.h，host/slave 共用）

```
struct mcs_test_msg { magic, ver, cmd, seq, status, total, failed, module[12], index }
```

### A.2 命令字

| cmd | 方向 | 含义 |
|---|---|---|
| PING(1) | host→slave | 探活（应答 status=0） |
| RUN_CASE(2) | host→slave | module + index(1-based) 跑单例 |
| RUN_MODULE(3) | host→slave | module 跑整模块 |
| RUN_ALL(4) | host→slave | 全部已注册模块 |
| STOP(5) | host→slave | 当前用例后停（协作式） |
| STAT(6) | host→slave | 查询状态（IDLE/RUNNING） |
| RSP=0x80 | — | 应答标志位（\| 到 cmd 上） |

两段应答：**ACK**（受理，`| RSP`）→ **DONE**（汇总 total/failed）。
序号语义：`run <mod> <N>` 的 N = 注册表数组第 N 行（**1-based**；屏蔽行不占位——
count 收缩后序号顺延）。

### A.3 slave 端点就绪机制

从核 rpc/tty 端点以 `dest=RPMSG_ADDR_ANY` 创建；`is_rpmsg_ept_ready` 要求
**先收到主核一帧**学习 dest_addr 才置 ready。host_passive 对 rpc（PING 探测）/
umt（addr-probe）/tty（换行探测）bind 后各发一帧激活——否则从核服务永不就绪
（2026-11-14 实测坑：tty 无探测导致 ItMcsTty 用例与 Combo401 失败）。

## 附录 B：host_server 架构（三线程模型）

### B.1 组成

- **命令服务**（host_server.c）：AF_UNIX socket `/data/mcs_test.sock`，收
  mcs_cmd 命令行，起测试线程（互斥：一次一个）
- **被动服务**（host_passive.c，常驻）：np socket 代理 worker（0x100-0x12x 全
  opcode，主核代执行真实 BSD 调用）+ pfs 代理（0x300-0x310 PF_*）+ 板内回环
  echo（127.0.0.1:7000/7001）+ E2E 配合（反向 call/UMT 按需下行/TTY 注入）
  + PING 快路径（rx 回调直答，防慢操作堵队连坐）
- **测试线程**（host_slave.c）：五步生命周期 create→start→回调→stop→delete；
  `-boot` 参数 = 裸拉核模式（从核自跑批，驱动套件用）

### B.2 生命周期与轮次

每条 `run` 命令一个轮次：mica_create → mica_start（拉核+NS 绑定）→ 被动端点
attach → 发命令帧 → 等 ACK/DONE → 端点 detach → mica_stop → mica_remove。
**轮间状态清零**（umt mmap 等）——上一轮的映射/端点随固件重载失效，复用即崩
（2026-11-14 实测：umt copy-mem 脏映射写 SIGBUS）。

### B.3 tx 串行化

openamp 无内部锁：host 侧所有 rpmsg 发送（RUN 帧/STOP 帧/worker 响应/E2E）
共用一把 `g_tx_lock`；从核侧由 rpc_tx_sem/call_sem 对应保护。

## 附录 C：设计要点备忘

- **用例编号分段**：api=001 / 功能=201 / 压力=301 / combo=401；命名三段式
  （文件小写 it_mcs_xxx_nnn.c，用例名驼峰 ItMcsXxxNnn）
- **调度器与 boot 批互斥**：iCunit 全局用例表非线程安全——TEST_MCS_SUITE 与
  TEST_ICUNIT_KERNEL 不可同开（形态 B 配方见 §1.1）
- **combo 破坏性用例契约**：涉及任务创建的用例必须在失败路径回收任务
  （EXIT 统一收尾 + join 语义：等任务持锁的 rpc_call 自然超时返回后再删，
  强删持锁任务=锁永丢→下例挂死）
- **np echo 意图**：echo 用例目标为**网段外部主机**（验证 ARP/交换机/对端栈
  全路径），非主核自连（local 路由丢失验证意图）；对端 echo 由 runner 侧
  echo_agent.ps1 常驻提供
- **RUN_ALL 已知缺陷**：见 §2.3
