# 本地配套 patch（测 mcs 前手动应用，不入上游）

上游仓边界约束：以下改动位于 testsuites 之外（kernel/ 与 compat/mcs/），
不由测试侧直接修改上库——以 patch 形式随套件携带，**跑 mcs 套件前在
kernel/liteos_m 仓根手动应用**（`patch -p1 < testsuites/sample/mcs/patches/XXXX.patch`）。
后续逐个走正规渠道（向对应模块负责人提缺陷+修复 MR）上游化，上游化
一个删一个。

| patch | 位置 | 作用 | 性质 |
|---|---|---|---|
| 0001-libmica-test-service-hook.patch | compat/mcs/mica（mica.h/service.h/mica_service.c） | MICA_SERVICE_TEST 服务枚举挂载 + mica_get_rpdev 公开声明（rpmsg-mcs-test 调度端点注册进 mica 框架） | 功能依赖（TEST_MCS_SUITE 编译必需，实测缺之编译失败） |
| 0002-libmica-tty-rx-buffer-leak.patch | compat/mcs/mica/tty_service.c | tty rx buffer hold 后无人释放，泄漏耗尽 master TX 池 | 缺陷修复（待反馈上游） |
| 0003-compat-pthread-stack-16k.patch | compat/mcs/compat/pthread.h | mica 线程 8KB 栈在压测下溢出——默认 16KB + attr 接线 | 缺陷修复（待反馈上游） |

应用顺序即编号顺序；0001 为必打（TEST_MCS_SUITE 功能），0002/0003 为
强烈建议（已知缺陷规避）。回退：`patch -p1 -R < XXXX.patch`。

> 形态 B（关 kernel 批）**无需任何 Kconfig patch**：标准
> `# LOSCFG_TEST_ICUNIT_KERNEL is not set` 行在 debug.config 中正常生效
> （2026-11-16 判定实验：行 grep 确认写入 + 删除 out/rk3568_m 与
> out/preloader 全量重建后 config.h 无该定义、固件 354KB 形态 B）。
> 注意两点：① 改 config 后必须删 out/<平台> 与 out/preloader 再编
> （解析缓存会让修改不生效）；② debug.config 里该产品原本无此行
> （依赖 Kconfig default y），脚本替换 '=y' 行时注意空操作陷阱。

> 历史注：早期版本尚有 los_arch.h 的 AArch64 编译修复 patch——上游
> （rk3588_m 9b8e4dd2）已将 ArchGetPsp/MspGet/Ipsr 以 `#ifndef __aarch64__`
> 守卫，当前 testsuites 无符号引用，该 patch 已删除。
