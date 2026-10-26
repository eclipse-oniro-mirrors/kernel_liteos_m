# RK3588 AMP 从核 UART 驱动使用指南

> 适用板卡：BQ3588HM V3 工控板（核心板+底板，迈克生物 SH-5000044480-02）
> 适用软件：主核 OpenHarmony 标准系统（linux 6.6.101），从核 liteos_m（CPU7, AArch64 EL1）
> 本文所有命令与数据均经 2026-10-25 实板验证。

## 1. 驱动概述

`board/uart.c` 实现面向 Modbus-RTU 的**协议无关** UART 驱动（DW 8250 / 16550 兼容 IP，
中断模式），支持 UART1~UART9：

- 每 port 独立 RX/TX 环形缓冲 + 信号量阻塞语义（读单任务、写超时含 FIFO/环形排空）
- 线同步写：`uart_write` 返回时数据已完全离开移位器（LSR TEMT 锚定）
- 上电自举：CRU 时钟门（双寄存器）+ sclk 父源锚定 xin24m + IOC iomux + GIC SPI 声明
- `uart_test` shell 自测命令（6 种模式，见 §4）
- API 契约在 `include/uart_drv.h`（7 个 P0 接口，纯标准 C，协议层可直接包含）

与 console 驱动（`uart_porting.c`，UART2 共享主核 ttyFIQ0）**相互独立**：协议层
传 port id（1..9）即得一个初始化好的串口；UART2 为共享口，调用方自担风险。

Kconfig 开关：`RK3588_UART`（依赖 `PLATFORM_RK3588_M` + `PLATFORM_HWI_WITH_ARG`），
产品配置 `LOSCFG_RK3588_UART=y` 已在 vendor 两份 config 中打开。

## 2. 硬件事实（BQ3588HM）

### 2.1 信号链路

```
从核 SoC pad(GPIO3_C4/C5 等, iomux=BUS_IOC)
  → 核心板走线 → 金手指 BTB → 底板 → MAX3232 电平转换 → RS232 端子
```

### 2.2 串口引出一览（底板 RS232 区，ZX-HY2.0-4P 端子，2.0mm 间距）

| 端子位号 | 实际 SoC UART | 引脚 | 信号 | 备注 |
|---|---|---|---|---|
| P1 | UART1 | 1/2/3/4 | 5V0 / GND / RX / TX | 空闲 |
| P4 | UART2 | 同上 | 同上 | **位号≠串口号**；UART2 为共享 console |
| P2 | UART3 | 同上 | 同上 | 空闲 |
| P3 | UART4 | 同上 | 同上 | 空闲 |
| **P5** | **UART5** | **1/2/3/4** | **5V0 / GND / RX / TX** | 本驱动主验证口 |
| **P6** | **UART9**（端子网名"UART6"为误导，实际落 UART9 m2 焊盘）| 同上 | 同上 | 已验证口 |

- UART0：PMU 域，驱动不支持（板上为 ETH 交换芯片预留）
- UART6：接 BT 模块（真 UART6 控制器，GPIO1_A0~A3），主核占用，驱动默认关闭
- UART7：板卡保留——m2 组（GPIO1_B4/B5）接 4G 模块，m1 组（GPIO3_C0/C1）是以太网
  ETH2_RGMII_INTN/PME 信号（rtsn/ctsn 引脚 C2/C3 亦被 GMAC1 MDC/MDIO 占用），
  两组均不可用，驱动默认关闭
- UART8：底板无走线（板上网名 BT_UART8_* 为遗留命名，实际接的是 UART6 焊盘）
- UART5 走线：TX=GPIO3_C4（m1 组）、RX=GPIO3_C5（m1 组），BTB pin 253/251；
  UART9 走线：TX=GPIO3_D4、RX=GPIO3_D5（m2 组）

### 2.3 端子引脚定义（以 P5 为例）

```
  ┌──────────────┐
  │ 1    2  3  4 │   1 = 5V0_VCC（危险，勿碰）
  │ 5V  GND RX TX│   2 = DGND
  └──────────────┘   3 = RS232_RX5（→板上 UART5_RX）
                     4 = RS232_TX5（→板上 UART5_TX）
```

⚠️ **安全警告（实测事故）**：pin1 为 5V 电源。**短接 pin1↔pin2 曾导致整板硬挂死**
（无 panic、无重启，串口/网络全灭；从核心跳监视器自下电属正常自保护）。恢复 =
完全断电 1 分钟后重上电。**跳线只碰 pin3↔pin4**，确认方法：万用表蜂鸣档
pin2 对任意 GND 导通定方向，pin1 在丝印三角/缺口标记端。

RS232 电平说明：端子侧经 MAX3232 转换（±5V 电平），TX↔RX 短接做回环时数据经
两次电平转换，保真可用；MAX3232E 上限 250kbps，115200/1200 均在范围内。

## 3. 环境搭建（从上电到可测试）

### 3.1 连接与 IP 配置（每次上电后执行一次）

PC 侧：USB 串口（CH340，设备管理器看 COM 号，1500000-8N1）+ 网线（PC 端
192.168.100.1）。

经串口发送（用串口终端或 `com6_cmd.ps1` 类脚本，COM 号按实际）：

```sh
ifconfig eth0 192.168.100.2 netmask 255.255.255.0 up
dmesg -n 1          # 静默 rk628 芯片 ~113ms 周期日志，必做
```

注意：板子启动约 90s 后 console shell 才可用（看到 `#` 提示符再执行）。

### 3.2 连接 hdc（PC 侧）

```sh
hdc kill; sleep 2; hdc tconn 192.168.100.2:33517
```

- hdcd 由 init 拉起并原生监听 TCP **33517**（勿用别的端口号）
- `hdc kill` 后立即 tconn 会竞态失败，**必须 sleep 2**
- 每条 hdc 命令失败时按上述顺序重连即可

### 3.3 传输从核镜像（仅镜像更新时）

```sh
hdc file send <OHOS_Image路径> /data/liteos_uart_test.elf
hdc shell md5sum /data/liteos_uart_test.elf     # 与本地 md5 比对
```

### 3.4 装载从核 + 交互桥

板上执行（可做成脚本 `/data/run_uart_test.sh`，已部署在板）：

```sh
insmod /data/mcs_km.ko                        # 从核生命周期/tick 模块（每次上电后需重新加载）
sh /data/run_uart_test.sh                     # 建 fifo 桥 + 启动 rpmsg_demo 交互模式
```

`run_uart_test.sh` 内容（原理：fifo 以读写模式自保持，**不可用 tail -f**——
toybox 下 fifo 写端关闭即 EOF 退出，会导致从核被意外下电）：

```sh
#!/system/bin/sh
rm -f /data/cmd.fifo /data/demo_uart.log /data/nohup.out
mkfifo /data/cmd.fifo
exec 3<>/data/cmd.fifo
LD_LIBRARY_PATH=/data/lib /data/rpmsg_demo -f /data/liteos_uart_test.elf -c 7 -i <&3 > /data/demo_uart.log 2>&1 &
exec 3<&-
echo SHELL_BRIDGE_READY
```

验证就绪（`hdc shell head -7 /data/demo_uart.log` 应有 5 行）：

```
[demo] firmware=/data/liteos_uart_test.elf cpu=7 ept=rpmsg-tty mode=interactive
[demo] step1 mica_create ok
[demo] step2 mica_start ok, waiting NS announce...
[demo] NS bind: name=rpmsg-tty peer_addr=1024
[demo] interactive mode: type a line, 'quit' to exit
```

### 3.5 打开串口捕获（从核输出通道）

从核 PRINTK 输出走共享 UART2（PC 串口 COM 号），**发送命令与串口捕获互斥**
（同一 COM 口）：先完成所有串口命令（3.1），再启动捕获进程，之后全部操作走 hdc。
串口日志中过滤 `[net]` 行（GMAC 无链路时 net_demo 的 tx -11 刷屏为正常噪音）。

从核启动横幅（捕获中应出现）：

```
RK3588 AMP liteos_m @ CPU7 (AArch64 EL1)
[boot] kernel init done ... [boot] app task ok
[app] uart_test ready              ← uart_test 命令已注册
OHOS #
```

## 4. uart_test 命令手册

命令经 fifo 注入从核 shell：

```sh
hdc shell 'echo "uart_test 5 self" > /data/cmd.fifo'
```

结果从两个通道读取：**从核串口捕获**（主输出）+ `/data/demo_uart.log`（桥回显）。

### 4.1 `uart_test`（无参）

打印用法。用于验证命令链路（fifo→rpmsg→shell）是否通。

### 4.2 `uart_test <port> err` —— API 错误路径（无需硬件）

```sh
hdc shell 'echo "uart_test 5 err" > /data/cmd.fifo'
```

期望输出：

```
uart5: API error-path checks
uart5: 21/21 checks passed
```

覆盖 21 项 API 契约检查（参数非法/组合非法/未初始化/重复初始化/不支持端口等）。

### 4.3 `uart_test <port> self [baud] [len]` —— MCR 内环（无需跳线）

片内回环：验证 CRU 时钟门 + xin24m 父源 + GIC SPI 声明 + ISR + FIFO 全链路，
**不经过外部引脚**。默认 115200 / 256 字节。

```sh
hdc shell 'echo "uart_test 5 self" > /data/cmd.fifo'          # 115200
hdc shell 'echo "uart_test 5 self 1200" > /data/cmd.fifo'     # 低波特率
```

期望输出（基线数值）：

```
uart5: inited 8-E-1 @115200, looping 256 bytes (MCR internal loop)
uart5 tx done: 256 bytes drained (TEMT), start rx
uart5 loopback PASS (256 bytes, 115200 bps)
uart5: state=1 isr=480 rx_drop=0 rx_oe=0 rx_err=0 echo_drop=0 tx_flush_abrt=0 last_lsr=0x0
```

**基线**：256B@115200 → isr=480；256B@1200 → isr=480。isr 计数偏离基线 =
时钟配置错；任何非零错误计数 = 失败。

### 4.4 `uart_test <port> matrix` —— 线制组合矩阵（无需跳线）

MCR 内环上跑 12 组合法线制组合（E-1/O-1/N-2 × 数据位 × 波特率），验证 LCR/MCR
运行时重配。

```sh
hdc shell 'echo "uart_test 5 matrix" > /data/cmd.fifo'
```

期望输出：

```
matrix: 12/12 combos passed
```

### 4.5 `uart_test <port> [baud] [len]` —— 片外回环（需跳线）

**物理回环**：端子 TX↔RX 短接（P5 为 pin4↔pin3），验证 iomux 片外真实走线。

```sh
# P5 端子 pin4↔pin3 已短接
hdc shell 'echo "uart_test 5 115200" > /data/cmd.fifo'
hdc shell 'echo "uart_test 5 1200 64" > /data/cmd.fifo'
```

期望输出（基线数值，与 rk3568 逐项一致）：

```
uart5: inited 8-E-1 @115200, looping 256 bytes (jumper TX-RX)
uart5 loopback PASS (256 bytes, 115200 bps)
uart5: state=1 isr=480 rx_drop=0 rx_oe=0 rx_err=0 echo_drop=0 tx_flush_abrt=0 last_lsr=0x0

uart5: inited 8-E-1 @1200, looping 64 bytes (jumper TX-RX)
uart5 loopback PASS (64 bytes, 1200 bps)
uart5: state=1 isr=96 ...（全 0）
```

**基线**：256B@115200 → isr=480；64B@1200 → isr=96。

### 4.6 `uart_test <port> tx [baud] [len]` —— 仅发送检查

TX 单向写入（RX 屏蔽），用于示波器/逻辑分析仪抓波形场景。

### 4.7 `uart_test <port> stat` —— 统计

```sh
hdc shell 'echo "uart_test 5 stat" > /data/cmd.fifo'
```

输出状态机（0=未初始化 1=已初始化）与累计计数：`isr / rx_drop / rx_oe /
rx_err / echo_drop / tx_flush_abrt / last_lsr`。**全部错误计数应为 0**。

### 4.8 `uart_test <port> diag` / `tick` —— 诊断

`diag`：TX 路径硬件诊断（逐级检查时钟/寄存器可读性）；`tick`：从核 tick 心跳探针。

## 5. 协议层接入（uart_drv.h）

```c
#include "uart_drv.h"

int32_t uart_init(uint8_t port, const uart_attr_t *attr);   /* 时钟+pinmux+GIC+16550 全套自举 */
int32_t uart_deinit(uint8_t port);
int32_t uart_read(uint8_t port, uint8_t *buf, uint16_t count, int32_t timeout_ms);
int32_t uart_write(uint8_t port, const uint8_t *buf, uint16_t count, int32_t timeout_ms);
int32_t uart_tx_flush(uint8_t port);                        /* 等 TEMT，无超时语义 */
int32_t uart_set_attr(uint8_t port, uint32_t baud, uart_parity_t p, uint8_t stop);
int32_t uart_rx_flush(uint8_t port);
```

- 合法线制组合：**E-1 / O-1 / N-2**（Modbus-RTU 约束），数据位 5~8，波特率 1200~115200
- `uart_write` 超时覆盖**整个**流程（接收+排空），超时返回部分计数并 flush 残帧；
  正常返回时数据已完全上线（协议层以此锚定 t3.5）
- **一帧一写**：拆成多次 write 会破坏帧内 t1.5 间隙
- 每 port 单读者单写者模型；`timeout_ms < 0` 不支持（返回 `UART_ERR_NOT_SUP`）
- 错误码仅 5 个：`UART_OK(0) / UART_ERR_PARAM(-1) / UART_ERR_STATE(-2) /
  UART_ERR_BUSY(-3) / UART_ERR_NOT_SUP(-4)`，不得自行扩展

## 6. 排障手册（实测踩坑集）

| 症状 | 原因 | 处置 |
|---|---|---|
| 命令发进去无响应 | ① fifo 桥死了（tail -f 方案）② 从核已下电 | 用 §3.4 的 `exec 3<>` 桥；`head /data/demo_uart.log` 看从核状态 |
| `mica_start failed: -1` | 上次从核未干净下电（killall 不会触发 mica_stop） | `rmmod mcs_km; insmod /data/mcs_km.ko` 后重跑桥；仍失败则整板重启（串口 `reboot` 可靠，`hdc shell reboot` 曾无效） |
| 板子完全无响应（无 panic 无重启日志） | 电源轨短路（如 P5 pin1↔pin2）| **完全断电 1 分钟**再上电；按 §2.3 检查保险丝（底板输入 F1=0453010.MR ~10A；核心板 5V 出口 F1）与适配器锁存（从墙插拔） |
| 串口命令全部无效/被挤掉 | 串口捕获进程占用 COM 口（命令与捕获互斥） | 先完成串口命令再开捕获；或停捕获→命令→重开 |
| 串口切入 "FIQ debugger" 模式 | 串口 break 误触发 | 发 `console` 命令退出（会回放全量内核日志，属正常） |
| `[net] tx FAILED: -11` 刷屏 | GMAC 无链路，net_demo 正常噪音 | 过滤 `[net]` 行即可；勿当成故障 |
| console shell 算术结果离谱 | 板上 shell 算术为 32 位有符号（0xFD5F0000 溢出） | 板上脚本避免大数运算；寄存器 dump 用预计算地址 |
| COM 号变了 | USB 重枚举 | 设备管理器重新确认（曾 COM6→COM7） |
| hdc tconn 竞态失败 | kill 后服务器未就绪 | `hdc kill; sleep 2; tconn ...` 固定节奏 |

## 7. 回归基线数据（2026-10-25/26 实测）

| 测试 | 基线 |
|---|---|
| rpmsg echo（MCS 回归） | 1000000/1000000 PASS，rtt 21~27us |
| uart5 err | 21/21 |
| uart5 self @115200 / @1200 | 256/256（isr=480）/ 256/256（isr=480） |
| uart5 matrix | 12/12 |
| uart9 self / matrix | 256/256（isr=480）/ 12/12 |
| uart5 片外 @115200 / @1200 | 256/256（isr=480）/ 64/64（isr=96） |
| uart9 片外（P6 端子）@115200 / @1200 | 256/256（isr=480）/ 64/64（isr=96） |
| uart1 片外（P1 端子）@115200 / @1200 | 256/256（isr=480）/ 64/64（isr=96） |
| uart3 片外（P2 端子）@115200 / @1200 | 256/256（isr=480）/ 64/64（isr=96） |
| uart4（P3 端子） | 引脚组 m1 按核心板球位图核实（UART4_TX/RX 落 GPIO3_D0/D1），片外回环待补测 |
| 错误计数 | rx_drop/rx_oe/rx_err/echo_drop/tx_flush_abrt 全 0 |

日志存档：`logs/rk3588_uart5_ext_loopback_1025.log`。

## 8. 重新构建（改代码后）

```sh
cd /root/work/rk3588Fit
hb set -p rk3588_m@hihope     # 会重生成 out 配置，属正常
hb build                      # tail 日志看 "build success"（退出码不可靠）
# 产物 out/rk3588_m/rk3588_m/OHOS_Image，符号自查：
nm out/rk3588_m/rk3588_m/OHOS_Image | grep -e uart_init -e OsShellCmdUartTest
```

改配置（`vendor/hihope/rk3588_m/kernel_configs/debug.config`）后同样重跑
`hb build`；注意构建实际读取的是 **vendor/hihope** 下这份（kernel 仓
`vendor/rk3588_m` 下的 config 是镜像，保持同步修改）。
