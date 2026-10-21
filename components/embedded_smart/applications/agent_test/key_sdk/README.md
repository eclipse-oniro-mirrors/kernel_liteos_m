# key_sdk — GPIO 按键 用户态 SDK（驱动层 + 使用层示例）

把"GPIO 按键被按下/松开"变成事件回调：软件去抖（约 20ms）+ 10ms 独立扫描线程 +
任意多个按键。纯用户态：业务进程直接调用，基于 HDF 用户态 GPIO HDI
（GpioSetDir/GpioRead，与内核版同名 API，参数一致）。

## 两层 API

### 1. key_driver.h — 驱动层（扫描/去抖/事件分发，不含业务）

| 函数 | 作用 |
|---|---|
| `KeyDriverInit(keys, num, cb)` | 一键启动：配置 GPIO 为输入 + 创建扫描线程，事件回调上报 |
| `KeyDriverDeinit()` | 停止扫描并释放（幂等），之后可重新 Init |
| `KeyDriverIsRunning()` | 驱动是否运行中（bool） |
| `KeyDriverGetState(index, &pressed)` | 查第 index 个键当前是否按下（1=按下） |
| `KeyDriverGetKeyNum()` | 当前注册按键个数 |
| `KeyDriverWatchAll(seconds)` | 诊断：监视全部 96 个 GPIO 电平跳变并打印（阻塞运行，上限 60s） |

- `KeyDesc`：`{ const char *name; uint16_t gpio; uint8_t activeLow; }`
  （gpio=全局编号 组号*8+组内序号；activeLow=1 按下为低电平，板上按键常态）
- `KeyEvent`：`KEY_EVENT_PRESS`（去抖确认按下）/ `KEY_EVENT_RELEASE`（确认松开）
- 回调原型 `void (*)(const KeyDesc *key, KeyEvent event)`，在**扫描线程上下文**执行，
  可 printf/短延时，不要长阻塞（会拖慢其他按键响应）

### 2. key_demo.h — 使用层示例（两键加减计数，可当 3 步模板抄）

| 函数 | 作用 |
|---|---|
| `KeyDemoStart()` | 启动示例（幂等保护：重复调用返回 HDF_ERR_DEVICE_BUSY） |
| `KeyDemoStop()` | 停止并释放驱动 |
| `KeyDemoGetCount()` | 读当前计数值（加键 +1 / 减键 -1） |
| `KeyDemoResetCount()` | 计数清零 |

当前示例按键（实测定位 2026-09-20）：KEY_ADD=GPIO0_2（全局 2，板上第 1 个键）、
KEY_SUB=GPIO0_1（全局 1，板上第 2 个键），均为按下低电平。
每次按下串口0（115200）打印：`[key] KEY_ADD, count = 3`。

## 文件清单

| 文件 | 用途 |
|---|---|
| key_driver.h / key_driver.c | 驱动层源码（用户态版） |
| key_demo.h / key_demo.c | 使用层示例源码 + 一次性命令行程序 |
| key_port.h | 平台垫片（PRINTK→printf / KeyDelayMs） |
| libkeydriver.a / libkeydriver.so | 预编译库（静态/动态） |
| key_driver.o / key_demo.o | 预编译目标文件 |
| key_demo | 预编译板端程序（push 到 /bin 后 exec 调用） |
| build_keydriver.sh / build_key_demo.sh | 全套交叉编译脚本（出 .o/.a/.so/程序） |
| BUILD.gn / bundle.json | 源码树集成文件（见《量产集成说明.md》） |

## 快速接入（业务代码，3 步模板）

```c
#include "key_driver.h"
#include "key_port.h"      /* PRINTK->printf 垫片 */

/* ---- 第 1 步:定义按键表 ----
 * 引脚不确定时,板上敲 exec /bin/key_demo watch 30 再按键,看哪个脚打印跳变 */
static const KeyDesc g_myKeys[] = {
    { "MY_UP",   2, 1 },   /* 名字, GPIO全局号, 按下是否低电平(板上按键填1) */
    { "MY_DOWN", 1, 1 },
    /* 可继续加:任意多个键,驱动统一扫描 */
};
#define MY_KEY_NUM (sizeof(g_myKeys) / sizeof(g_myKeys[0]))

/* ---- 第 2 步:写事件回调(你的业务都在这里) ---- */
static void MyKeyCallback(const KeyDesc *key, KeyEvent event)
{
    if (event != KEY_EVENT_PRESS) {      /* 只关心"按下";要响应松开改判 RELEASE */
        return;
    }
    if (key == &g_myKeys[0]) {
        PRINTK("MY_UP pressed: do something A\n");    /* 你的业务 A */
    } else if (key == &g_myKeys[1]) {
        PRINTK("MY_DOWN pressed: do something B\n");  /* 你的业务 B */
    }
}

/* ---- 第 3 步:常驻业务进程里一行启动;进程退出前 Deinit ---- */
KeyDriverInit(g_myKeys, MY_KEY_NUM, MyKeyCallback);   /* 返回 HDF_SUCCESS 即在扫 */
...
KeyDriverDeinit();
```

板端不用写代码，直接命令行验收（每次 exec 都是新进程）：

```
exec /bin/key_demo run 10     计数演示 10 秒:点键打印 [key] KEY_ADD, count = N,结束报总数
exec /bin/key_demo state      一次性读两键按下/松开状态
exec /bin/key_demo watch 20   监视全部 96 个 GPIO 跳变(定位按键引脚用,默认 20s)
```

## GPIO 引脚说明

- 全局编号 = 组号*8 + 组内序号（GPIO3_7 = 31）；板上按键接 GPIO 与 GND 之间，
  按下为低（activeLow=1）
- 板上两个实体按键默认已复用为 GPIO，直接用；若换到非默认复用的脚，需先切
  复用（用户态走 HDF Pin HDI，PinGet/PinSetFunc，同样在 libhdf_platform.so 里）
- 实测板上两键 = GPIO0_2 / GPIO0_1（vendor input_config.hcs 的 31/32 不是本板按键）
- 去抖/扫描参数在 key_driver.c 顶部宏：`KEY_SCAN_PERIOD_MS`（10ms）、
  `KEY_DEBOUNCE_TIMES`（连续 2 次同电平才确认，约 20ms）

## 两种链接方式（.a 静态 / .so 动态）——按场景选

| 场景 | 选择 | 理由 |
|---|---|---|
| 只有一个程序用 SDK（**默认推荐**） | `.a` 静态链 | 产物单文件、无部署依赖 |
| 镜像里多个进程都要调 SDK | `.so` 动态链 | 共享一份代码；升级只换 .so |

### 静态链

```bash
clang --target=arm-liteos-eabi -mcpu=cortex-a7 -mfloat-abi=softfp -mfpu=neon-vfpv4 \
      --sysroot=$SYSROOT <HDF头8个-I> 你的代码.c ./libkeydriver.a \
      -L$OUT/usr/lib -lhdf_platform -o 你的程序
```

⚠️ 显式给 `./libkeydriver.a` 路径，**不要**写 `-L. -lkeydriver`——同目录有 .so 时
链接器会优先动态链，板上没有该 .so 会 exec 失败（实测踩过）。

### 动态链

```bash
clang ... 你的代码.c -L. -lkeydriver -L$OUT/usr/lib -lhdf_platform -o 你的程序
```

部署时必须把 `libkeydriver.so` 放板上 **`/usr/lib/`**（板上 ldso 不认
LD_LIBRARY_PATH）。

HDF 用户态头（编译时 -I，8 个）：

```
drivers/hdf_core/framework/include/{core,osal,platform,utils}
drivers/hdf_core/interfaces/inner_api/{core,osal/shared,osal/uhdf,utils}
```

链接依赖 `libhdf_platform.so`（板上 /usr/lib 已有，镜像自带）。
build_keydriver.sh / build_key_demo.sh 里有可直接照抄的完整旗标。

## 错误码

| 返回值 | 含义 |
|---|---|
| HDF_SUCCESS (0) | 成功 |
| HDF_ERR_INVALID_PARAM | keys/num/cb 非法（空表/0 个/空回调）或 index 越界 |
| HDF_ERR_DEVICE_BUSY | 已在运行（先 Deinit 再 Init） |
| HDF_ERR_MALLOC_FAIL | 状态内存分配失败 |
| HDF_FAILURE | GPIO HDI 配置失败/线程创建失败 |

## 与内核版（mytest/key）的差异（可追溯）

扫描/去抖/事件分发逻辑逐行一致，仅三处平台替换 + 一处访问方式替换：

| 内核版 | 用户态 SDK 版 |
|---|---|
| `LOS_TaskCreate`(TSK_INIT_PARAM_S, 4KB 栈, 优先级 25) | `pthread_create`(detached, musl 默认栈, 系统调度) |
| `LOS_TaskDelay`(1 tick=10ms) | `KeyDelayMs(10)`（key_port.h，nanosleep） |
| `PRINTK` | `printf`（key_port.h 宏） |
| PL061 寄存器直读(OsalIoRemap+OSAL_READL) | HDF 用户态 GPIO HDI(GpioSetDir/GpioRead) |

内核版走寄存器直读的原因：内核模块挂载早于 GPIO 控制器注册，开机阶段 HDF
调用必失败且刷 HDF_LOGE 灌爆 hilog（实测 2026-09-20）。用户态程序开机后才
运行，HDF 早已就绪，不存在该时序问题，走标准 HDI 即可。
另：`KeyDriverIsRunning` 返回 `bool`（内核版 BOOL）；watch 诊断逐脚 GpioRead
经 dev_mgr 转发，CPU 开销比内核版寄存器直读高（96 脚 x 100 轮/s），诊断用途足够。

## 注意事项（板上实测坑）

1. SDK 状态**不跨进程共享**：每次 `exec` 都是新进程，计数从 0 开始；
   跨进程"中止另一进程的扫描"做不到，常驻业务用 API（Deinit）管理生命周期
2. 串口调用必须 `exec /bin/key_demo ...`（LiteOS shell 超长命令名静默丢弃的坑）
3. **单使用者**：驱动同一时刻只支持一个"按键表 + 回调"，第二个业务 Init 返回
   HDF_ERR_DEVICE_BUSY；多业务在一个回调里分发，或先 Deinit 再换注册
4. 按一下打印多次 → 加大 `KEY_DEBOUNCE_TIMES`；按键完全没反应 → 先 `watch`
   找跳变脚（引脚不对/复用错是九成原因）
5. `run`/`state` 命令启动失败（start fail）→ 确认板上 /usr/lib/libhdf_platform.so
   在位（镜像自带），HDF 平台服务正常（其他 HDF 用户态程序能跑即正常）
6. 若板上同时烧了含内核版 key 模块（key1 命令）的内核：两者读同一批 GPIO 电平
   不冲突（都是只读），但各自独立计数/打印，注意别被两份输出混淆
