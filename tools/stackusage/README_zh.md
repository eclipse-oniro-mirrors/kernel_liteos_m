# stackusage 静态栈使用分析工具

stackusage 解析 ARM(Thumb/Thumb-2)与 RISC-V(RV32IMAC、Nuclei Zc 扩展)
的反汇编文件,静态计算**任务/中断函数调用链的峰值栈使用量**,用于:

- 为任务栈规格配置(`LOS_TaskCreate` 的 stackSize)提供量化依据;
- 评估中断栈规格;
- 在构建期发现深调用链/递归等栈溢出风险,而不用等到运行时踩内存。

工具为纯 Python 标准库实现,免安装、无第三方依赖,可嵌入 CI。

## 环境要求

| 项 | 要求 |
|---|---|
| Python | 3.6 及以上 |
| 反汇编文件 | 用与目标架构匹配的工具链 objdump 生成(见下) |

```bash
# 基础(仅地址,支持全部估值功能)
<toolchain->objdump -d OHOS_Image > OHOS_Image.asm

# 推荐(额外启用 -m 1/2 源码定位;需固件编译带 -g)
<toolchain->objdump -d -l -S OHOS_Image > OHOS_Image.asm
```

其中 `<toolchain->` 为目标架构工具链前缀,如 `arm-none-eabi-`、
`riscv32-unknown-elf-` 等。

说明:

- 架构自动识别(按文件头),通常无需 `--arch` 指定;
- 工具可在任意目录执行,产物 `cfg/config.ini`、`out/funcstack.json`
  生成在执行时的当前目录,不影响工具目录。

## 快速上手

```bash
# 1) 全量扫描:找出栈峰值最高的任务入口(结果存入 JSON)
python3 stackusage.py --asm OHOS_Image.asm -j

# 2) 单点分析:查看指定任务的峰值与调用树
python3 stackusage.py --asm OHOS_Image.asm -sp -t OsIdleTask -out

# 3) 中断栈:统计异常/中断路径栈(需先在 cfg/config.ini 配置,见下文)
python3 stackusage.py --asm OHOS_Image.asm -irq
```

## 参数说明

```
必选:
  --asm <file>       反汇编文件路径

可选:
  --arch <arch>      arm | riscv(默认按文件头自动识别)
  -t FuncA,FuncB     指定任务入口函数名(逗号分隔)
  -sp                单点模式(配合 -t,只分析指定入口)
  -r N               递归深度:递归函数按总共 N 层计(默认 1,须 >= 1)
  -out               输出结果表格与调用树到终端(给人看)
  -j                 输出 JSON 到 ./out/funcstack.json(给程序读)
  -L N               调用树过滤:max_size >= N 才显示
  -l N               调用树过滤:local_size >= N 才显示
  -m N               0=仅地址(默认) 1=源码行号 2=行号+函数指针变量名
  -irq               中断栈模式(与 -sp/-t 互斥,误用报错退出)
  --config <path>    指定 config.ini 路径(默认 ./cfg/config.ini)
```

组合规则:

| 组合 | 行为 |
|---|---|
| `-sp` + `-t` | 单点模式,表格默认输出 |
| 不加 `-sp` | 全量模式,入口 = 所有未被调用的函数;表格/树仅 `-out` 时输出(默认只有告警+统计) |
| `-out` + `-j` | 可同时使用,终端 + JSON 双输出 |
| `-L`/`-l` | 仅影响 `-out` 调用树显示(AND 交集),不影响 JSON 与表格 |
| `-irq` 与 `-sp`/`-t` | 互斥,同时使用报错退出 |

## 输出解读

终端输出共 5 部分(空行分隔):**递归告警 → 函数指针缺失告警 →
结果表格 → 调用树 → 统计**。

### 告警

```
[WARNING][Recursive function]:        <- 递归环,值受 -r 影响
<LOS_MemFree> -> <OsMemFree> -> <LOS_MemFree>

[WARNING]Miss_funcptr:<dns_call_found>:0x210032ce
                                      <- 经寄存器的间接调用(BLX Rm)目标静态不可知
```

`-m 1/2` 下告警后缀变为源码位置(如 `dns_call 46`)。

### 结果表格

```
Entry           PeakStack  CallChain
OsIdleTask      128        OsIdleTask>HalIrqHandler>HalIrqDispatch
OsHwiBhTask     64*Rec     OsHwiBhTask>OsHwiBhProcess*Rec
```

- `PeakStack` 单位字节;`*Rec` = 该值受递归深度 `-r` 影响;
- `CallChain` 超过 10 跳截断。

### 调用树(仅 -out)

```
function<OsIdleTask>:0x2102ec78 stack cost=2656
symbol task_entry   task_stack_max_cost local_cost
symbol+--<OsIdleTask>: 2656 32
|     symbol+--<__aeabi_uldivmod>: 48 16
```

每入口一个块;`-L`/`-l` 可过滤小节点,大树不至于刷屏。
`*Rec`/`*Tail`/`*Ignore` 分别标记递归、尾调用、忽略函数。

### JSON(仅 -j,`./out/funcstack.json`)

key 为函数地址(十六进制字符串),每函数 10 字段:

| 字段 | 说明 |
|---|---|
| FuncName / FuncAddr | 函数名 / 地址 |
| LocalCost | 本函数栈帧(字节) |
| MaxCost | 到叶子的最大路径栈(字节) |
| Called | 子函数地址数组 |
| Location | 源码位置(`-m 1/2`,如 `los_task.c:821`) |
| IsCalled / IsRec / Depth | 是否被调 / 递归 / 深度 |
| PtrVarName | 函数指针变量名(`-m 2` 为源码变量名,其余为寄存器名) |

`-irq -j` 时 JSON 另含保留键 `Interrupt`(中断栈统计)。

## 间接调用补全(config.ini 闭环)

函数指针/间接调用(`blx r3`)的目标是运行时值,静态分析不可穷举。
工具的策略是**只告警、不猜测**(不会漏报,只会多告警),由人工补全后闭环:

```bash
# 1) 首跑:生成 cfg/config.ini(含每个未解决站点的空桩)
python3 stackusage.py --asm OHOS_Image.asm -j

# 2) 编辑 config.ini,把关心的告警站点补上目标函数:
#    Remarks_here:<函数名>:<地址>:<目标函数名>
#    Remarks_here:<函数名>:<目标函数名>
#    Ignore_function:<函数名>        <- 或整体忽略某函数

# 3) 复跑:该告警消失,估值按补全边更新
python3 stackusage.py --asm OHOS_Image.asm -j
```

补全行原样保留;同输入重复运行生成的模板部分逐字节一致(幂等)。

中断栈模式需在 config.ini 中配置:

```
Interrupt_entry: HalExcHandleEntry          # 中断入口函数
Interrupt_isr: 3: TIM001_IRQHandler         # IRQ 号 -> ISR
```

## 常见问题(FAQ)

**Q1:Miss_funcptr 告警几百条,正常吗?**
正常。镜像里函数表/回调注册很多,逐站点告警是设计行为;按上文补全
关心的入口即可,其余不影响已解析部分的估值。

**Q2:递归函数的值怎么理解?**
`-r N` 控制递归按总共 N 层展开(默认 1)。实际深度是运行时属性,
需结合业务设置;受影响的值会带 `*Rec` 标记。

**Q3:`-m 1/2` 没有行号输出?**
行号来自调试信息(-g 编译)+ objdump `-l`/`-S`。固件没编 -g 时
三模式输出一致(模式退化是预期行为)。

**Q4:`-irq` 加 `-t` 报错?**
互斥设计:`-irq` 只统计中断栈,入口来自 config.ini 配置,不需要
任务入口参数。

**Q5:结果和真实栈占用完全一致吗?**
静态估值,方向保守(无法精确解析的栈操作按未释放处理,宁可高估);
中断嵌套深度、动态栈操作等运行时行为不在静态范围内。

**Q6:如何确认环境就绪?**
用任意镜像跑一遍全量(`--asm xxx.asm -j`),能正常输出告警、统计
并生成 `out/funcstack.json` 即可。

## 约束与限制

1. **间接调用**:目标静态不可知,靠 config.ini 人工补全;未补全时
   告警而非猜测;
2. **递归深度**:`-r` 由用户指定,实际深度需结合业务判断;
3. **源码定位**:`-m 1/2` 依赖编译带 -g 及 objdump `-l`/`-S` 生成。
