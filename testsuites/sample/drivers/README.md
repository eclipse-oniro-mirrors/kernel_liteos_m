# RK3568 让渡驱动测试套件（slave-side concession drivers）

从核侧（LiteOS-M @CPU3）让渡驱动（CAN / GE 网卡 / UART / TIMER）的硬件验证用例套件。
自包含：控制器内自回环或板内组网，无需外接总线；不依赖 mica 服务框架。

## 一、编译

### 1.1 前置

- 代码树：OpenHarmony evolve 分支 repo 工程（`kernel/liteos_m` 为内核仓）
- 产品：`rk3568_m_testsuits`（vendor/hihope/rk3568_m_testsuits，测试宏产品）
- 板级依赖：`LOSCFG_MCS=y` 及各驱动宏（`LOSCFG_MCS_CAN` / `LOSCFG_MCS_NET` /
  `LOSCFG_MCS_TIMER` / `LOSCFG_RK3568_UART`——产品 config 自带）

### 1.2 编译入口（Kconfig）

```
config TEST_MCS_DRV_SUITE
    bool "testsuites/sample/drivers (concession-driver cases)"
    default n
    depends on PLATFORM_RK3568_M
    depends on MCS
```

默认 **n**（上库安全：不编入任何新增用例）。在产品的
`vendor/hihope/rk3568_m_testsuits/kernel_configs/debug.config` 中置开：

```
LOSCFG_TEST_MCS_DRV_SUITE=y
```

### 1.3 执行形态（三种固件配方）

| 形态 | debug.config 配方 | 行为 |
|---|---|---|
| **纯 drivers** | `TEST_MCS_DRV_SUITE=y`，三个 `TEST_ICUNIT_*` 全 not set | 上电只跑驱动批（~300KB 固件） |
| **形态 A（kernel+drivers）** | `KERNEL_TEST=y`（TEST_ICUNIT_KERNEL 默认 y）+ `TEST_MCS_DRV_SUITE=y` | 驱动批在 kernel 批**前**自跑（ItDriverSuite 挂 TestKernel 前） |
| 纯净产品 | TEST_MCS_DRV_SUITE 不置（默认 n） | 不编入用例（上库默认态） |

### 1.4 编译命令

```bash
cd <evolve 工程根>
./build.sh --product-name rk3568_m_testsuits
# 产物：out/rk3568_m/rk3568_m_testsuits/OHOS_Image（即从核固件 liteos-m.elf）
```

## 二、执行

驱动批**随从核启动自动执行**（无 host 命令通道）：主核拉起从核 → 从核 boot 即
进入 ItDriverSuite → 按域顺序（can → net → uart → timer）逐例执行 → 串口报告
Passed/Failed → `--- Test End ---` 收尾。

### 2.1 手工执行（hdc）

```bash
# 部署（板侧 /data；主核三 ko 需先 insmod）
hdc tconn <板IP>:8710
hdc file send out/rk3568_m/rk3568_m_testsuits/OHOS_Image /data/liteos-m.elf
hdc file send <master-code>/device/soc/rockchip/rk3568/mcs/mcs_km/mcs_km.ko /data/
hdc file send <master-code>/device/board/hihope/rk3568_amp/net/mcs_net.ko /data/
hdc file send <master-code>/device/board/hihope/rk3568_amp/can/mcs_can.ko /data/

# 起链路并拉起从核（host_server -boot = 裸拉核，从核自跑批）
hdc shell "insmod /data/mcs_km.ko && insmod /data/mcs_net.ko && insmod /data/mcs_can.ko"
hdc shell "chmod +x /data/host_server; export LD_LIBRARY_PATH=/data/lib; \
           nohup /data/host_server -f /data/liteos-m.elf -boot > /data/host_srv.log 2>&1 &"

# 结果：调试串口（1500000）观察，或归档后 grep
hdc shell "cat /data/host_srv.log"    # 主核侧拉核日志
# 串口侧：[drv-suite] enter drivers batch → [Passed/Failed]-ItDriverCan201-... → --- Test End ---
```

> host_server 二进制由 mcs 套件的 `host/build_host.sh` 产出（见 mcs/README.md §二），
> 部署集（host_server + lib 五件）也见彼处。

### 2.2 自动化执行（board-test-runner）

```bash
<skills>/board-test-runner/scripts/rk3568/run.sh          # 默认=批跑形态（含驱动批）
<skills>/board-test-runner/scripts/rk3568/proxy/run.sh    # 代理版（串口在对端 PC 时）
```

run.sh 一键闭环：板锁 → 单板复位校验（board_verify 门卫）→ 部署物 md5 校验传输 →
insmod → 拉核批跑 → 串口采集（Test End / 崩溃横幅 / 空闲超时三类检测）→
Passed/Failed 汇总 + 日志包（工程 logs/ 目录）。

### 2.3 结果判读

| 串口标志 | 含义 |
|---|---|
| `[drv-suite] enter drivers batch` | 驱动批开始 |
| `[Passed]-ItDriverCan201-...` | 单例通过（用例名即编号） |
| `--- Test End ---` | 批正常收尾（结果可信） |
| `Exception Information` | 从核异常崩溃（捕获摘要后中止） |

### 2.4 各域环境依赖

| 域 | 依赖 |
|---|---|
| can | 无（控制器内自回环） |
| net | 无强制（接收用例偏好网段有背景广播流量；Net210 需对端 ping GMAC1 网段） |
| uart | 无（MCR 内环） |
| timer | 无 |

## 三、维护（加用例 / 屏蔽）

```
drivers/
├── it_driver.c            # 总聚合 ItDriverSuite（osTest 挂 TestKernel 前）
├── can/  it_driver_can.c  # 域聚合（顺序执行）
│         it_driver_can.h  # 域自持头（inline 工具 + 用例声明；域间零耦合）
│         it_driver_can_001.c ... _304.c
├── ge/   it_driver_net.*  # GE 网卡域（net 命名、ge 目录）
├── uart/ it_driver_uart.*
└── timer/ it_driver_timer.*
```

- **编号规则**：`it_driver_<域>_<编号>.c`，用例名驼峰同构 `ItDriverCan<编号>`；
  **001 起=API 测试 / 201 起=功能测试 / 301 起=压力测试**（用例名不携带接口名）
- **加用例**：写 `it_driver_can_305.c`（Testcase + TEST_ADD_CASE + `ItDriverCan305` 入口）
  → `it_driver_can.h` 加声明 → `it_driver_can.c` 聚合加调用行 → BUILD.gn 加源文件行
- **屏蔽用例**：注释聚合中的调用行即可（一行，零副作用）
- 新增外设域：建目录 + 域头 + 域聚合 + `it_driver.c` 挂接 + BUILD.gn

## 附录 A：设计说明

### A.1 套件定位（让渡驱动验证）

RK3568 AMP：CAN/GMAC1/UART1/TIMER1 等外设由主核（Linux）**让渡**给从核直驱
（mcs_net/mcs_can/mcs_uart ko 撤出主核侧驱动，从核持有寄存器与 IRQ）。本套件
从核侧直接调用 board 驱动 API（can.h / net.h / uart_drv.h / timer.h）验证数据面：
收发回环、错误码族、速率/压力、中断路径。

### A.2 批跑挂接点与 Hwi057 契约

`ItDriverSuite` 在 osTest.c 中挂于 **TestKernel 之前**：Hwi057（GICD 门控故障注入，
设计为**全序列终结者**）位于 kernel 批尾部——驱动批在前可保证其序列末位契约不被破坏。
Hwi057 仅在 `TEST_ICUNIT_KERNEL=y`（kernel 批编入）时生效。

### A.3 用例分类与编号基线

- **API 测试（001-199）**：入参无效族（NULL/零长/越界/未初始化），断言错误码
- **功能测试（201-299）**：正常数据面路径（收发回环、配置组合、场景）
- **压力测试（301-399）**：长循环/高速率（SlowCase 计时自动打印）

### A.4 与 mcs 套件的关系

两套件编译入口独立（`TEST_MCS_DRV_SUITE` / `TEST_MCS_SUITE`）互不依赖；驱动批为
boot 自跑形态，mcs 为 host 调度形态——**两种形态不建议同固件共存**（驱动批的
Test End 会污染调度模式的完成判定，详见 mcs/README.md 附录）。

### A.5 已知问题基线（当前屏蔽项）

| 用例 | 状态 |
|---|---|
| ItDriverCan208 | -11 EAGAIN 已知问题（在案待反馈） |
| ItDriverNet210 | GE PHY 协商 timing 高概率失败（间歇） |
| ItDriverCan/Net 301+ | 压力族（部分环境敏感） |
| ItDriverUart001-003/201-204/301-302 | 断言失败族（调试中） |
| ItDriverTimer204 | 间歇失败 RetCode:0x4 |

屏蔽方式=聚合行注释；修复后逐个放回。
