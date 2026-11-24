# 内核代码覆盖率统计操作指南

**开启宏：`LOSCFG_TEST_KERNEL_COVERAGE=y`**
（写在产品的 `kernel_configs/debug.config` 里。两板均已配置：
qemu=`vendor/ohemu/qemu_mini_system_demo`，ws63=`vendor/hihope/ws63_m_testsuits`；
改过宏后需重新编译。）

前置条件：`lcov` 已安装（`sudo apt-get install -y lcov`）。

qemu 与 ws63 流程完全相同，仅脚本与产物路径不同，按下表替换即可。

---

## 操作步骤

### 步骤 1：编译

```bash
# qemu
bash ~/.config/opencode/skills/board-test-runner/scripts/qemu-system-arm/compile.sh
# ws63
bash ~/.config/opencode/skills/board-test-runner/scripts/ws63/compile.sh
```

### 步骤 2：运行测试（自动捕获串口日志）

```bash
# qemu（日志 logs/qemu_test_<时间戳>.log）
bash ~/.config/opencode/skills/board-test-runner/scripts/qemu-system-arm/run.sh
# ws63（日志 logs/ws63_capture_<时间戳>.txt）
bash ~/.config/opencode/skills/board-test-runner/scripts/ws63/run.sh
```

### 步骤 3：生成覆盖率报告

```bash
bash kernel/liteos_m/testsuites/src/gcov/gen_coverage.sh <步骤2的日志文件>
```

一条命令自动完成：重建 .gcda → 按板子匹配 gcov 工具 → lcov → genhtml。
报告输出：`out/<构建目录>/coverage_html/index.html`
（与构建产物同树，不单独建 logs 目录；中间数据 `<构建目录>.info` 同级，
由脚本从串口日志自动推导；原始 .gcda 落盘在
`out/<构建目录>/obj/**/*.gcda`）。

---

## 检查要点

| 检查 | 方法 |
|---|---|
| 覆盖率宏已生效 | `grep LOSCFG_TEST_KERNEL_COVERAGE out/<板>/<产品>/config.h` 应有定义 |
| dump 正常完成 | 日志末尾有 `GCOV_DUMP_START` ... `GCOV_DUMP_DONE`，中间每组文件一个 `GCOV_BEGIN`/`GCOV_END` 帧 |
| 改了 .h 不生效 | 工具链不跟踪头文件依赖，必须 touch 对应 .c（或全部源文件）后重编 |
| 报告为 0% / 版本报错 | 不要手工指定 `--gcov-tool`，`gen_coverage.sh` 已自动匹配 |

备用工具：`gen_coverage.py`（自研层级 HTML，用法
`python3 gen_coverage.py <日志> [构建目录] [输出目录]`，gcov 工具同样自动匹配）。

---

## stm32f407vet6（BOX 移植树）流程

与 qemu/ws63 有三点不同：**覆盖率片段不单独入库**——`run_coverage.sh`
每次运行从 `test.config` 现场 /tmp 派生（拷贝 + 追加
`LOSCFG_TEST_KERNEL_COVERAGE=y`，套件集永不漂移；`LITEOS_CONFIG` 接受
绝对路径，liteos.gni 经 rebase_path 直通 genconfig）；测试在 QEMU
netduinoplus2 复现（无实体板步骤）；报告走 lcov + genhtml
（`gen_coverage.sh`，gcov 工具自动匹配）。

### 一键运行（推荐）

```bash
bash kernel/liteos_m/testsuites/src/gcov/run_coverage.sh
# 加 --skip-build 复用 out/ 里的镜像，不重编
```

一条命令完成：cov.config 构建 → QEMU 全套用例（UART base64 dump）→
重建 .gcda → lcov → genhtml。

- 套件日志：`/tmp/cq.log`；报告落在 out 产品目录（不单独建 logs）：
  `out/stm32f407vet6/stm32f407vet6_mini/coverage_html/index.html`
  （`.info` 同目录）。
- 预期结果：**372 PASS / 0 FAIL / 0 fault**（= test.config 全集 376 例
  减 `QUEUE_COV_SKIP` 门控的 queue 088、减覆盖率构建下 skip 的
  ItLosHwi036/037/038——见下表）；日志尾部 `GCOV_DUMP_DONE` 恰 1 个。

### 手动分步（等效备用）

```bash
export PATH="prebuilts/build-tools/linux-x86/bin:$PATH"
LITEOS_CONFIG=cov.config hb build
timeout 600 qemu-system-arm -M netduinoplus2 -cpu cortex-m4 -nographic \
  -monitor none -kernel out/stm32f407vet6/stm32f407vet6_mini/OHOS_Image \
  > /tmp/cq.log 2>&1   # QEMU 跑完不退出，timeout 收尾属正常
bash kernel/liteos_m/testsuites/src/gcov/gen_coverage.sh /tmp/cq.log
```

### 本板适配点（改这些需重新核账）

| 适配点 | 位置 | 原因 |
|---|---|---|
| 插桩白名单 | `testsuites/BUILD.gn`（`-fprofile-filter-files`） | 固定 flash/RAM 配额下全量插桩溢出；白名单 = kernel/box、base 的 queue/event/mux/task 四文件、arch box。**los_sem.c 不在**（issue 1109 扩容后 sem 插桩引发 81×0x3000200 资源级联，实证移出） |
| usr 世界排除 | `kernel/box/BUILD.gn` box_usr（`no_test_coverage`） | box_usr 源码在白名单目录内但跑非特权态，须显式排除 |
| spawn 栈 0x1000 | `testsuites/include/osTest.h`（本板 COVERAGE 特化） | 上游 0x5000 下 27K 剩余堆取不出连续块，62 例 NO_MEMORY |
| 主任务栈 0x2000 | `testsuites/include/osTest.h`（`OS_TSK_MAIN_TEST_STACK_SIZE`，定义于 OS_TSK_TEST_STACK_SIZE 旁；osTest.c 仅引用宏名，与上游仅差一行） | 主任务需走完 gcov dump 调用链（子任务仍用 0x1000 水位宏，抬高共享宏会饿死 ~27K 内核堆） |
| queue 088 门控 | `testsuites/sample/kernel/queue/It_los_queue.c`（`QUEUE_COV_SKIP`） | 12K 连续堆块在覆盖率构建的堆预算下不可得，注册期门控 |
| 时序用例容差/skip | sem 034（±20 容差）、hwi 036/037/038（整体 skip）、event 034（计数 10..15） | 插桩开销使 delay 校准漂移，036/037/038 同族时序测量（不同轮次不同成员触发）。036/037 带"原子弹"结构：IntLock 循环内断言 return 1 跳过 LOS_IntRestore → PRIMASK=1 泄漏 → 一挂带 ~20 连挂（037 于 2026-11-23 实爆：19 HWI + box004 级联；036 曾同形态，gdb 实证）。skip 无覆盖损失（均不在插桩白名单）（box-fusion-design.md M6Z-V） |
| 链接脚本模板 | `targets/.../board/liteos.ld.in` + `gen_liteos_ld.py`（GN action） | GNU ld 不跑 cpp；cov 布局（KERNEL 256K / BOX_A·B 128K×2 / COMMON 512K，共 640K，QEMU-only 不可烧真板）在模板 `#ifdef` 里展开，demo/test 产物与 stock 布局 bit 级一致（双锚点不变实证） |

当前基线（2026-11-23，套件 376 例 + issue 1111 的 los_task 新 API 用例）：
**372 PASS / 0 FAIL**，**行 80.5%（1992/2474）/ 函数 79.9%（230/288）/
分支 56.6%（1000/1766）**；cov 镜像 text+data 351212B（扩槽布局实占
343.0K/640K），RAM data+bss 88552B（86.5K/192K）。usr 侧（BOX 内非特权
代码）覆盖率是已知限制。
