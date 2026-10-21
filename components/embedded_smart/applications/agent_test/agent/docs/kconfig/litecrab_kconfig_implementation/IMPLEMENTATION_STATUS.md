# LiteCrab 渐进式 Kconfig 实施状态

更新时间：2026-09-29

## 已完成并通过 host 验证

- 阶段 0：冻结原始 20 项 CTest 基线，增加公开头文件编译契约和可复现基线脚本。
- 阶段 1：实现 Kconfig、三个 Product defconfig、中央 Manifest、严格且确定性的单一生成器，输出 `.config`、`autoconf.h`、CMake/GN 清单、Registry、Prompt C 和能力报告。
- 阶段 2：`main.c` 已缩为转发入口；Capability Manager 管理 Observability、Agent/Memory、MCP、Alarm、Gateway，支持逆序 quiesce/stop/destroy 和启动失败回滚。Alarm/MCP/Gateway 关闭构建均有 `nm` 裁剪证据。
- 阶段 3：已建立 Prompt Provider、可注入 LLM Transport、External Tool Backend 和 Embedded Agent API；POSIX/OpenSSL 已迁入独立 platform 实现并保留取消语义；Workspace/Process/Skill/Memory Tool 已拆分为能力源集，Robot Tool 的非法参数在 backend 调用前由 Runtime 拒绝。
- 阶段 4（主仓 host 部分）：生成 `robot_hi3516` 静态库 `liblitecrab_agent.a`、GN 源清单、GN 包装入口和 Hi3516 transport 包装；host 产物不含 Alarm/MCP/Gateway 符号。
- 阶段 5：`energy_inspection` 仅新增 Product 配置、Prompt 和 smoke 数据即可构建，产物不含 Alarm。
- 阶段 6（host 部分）：增加正式矩阵描述、runner、符号检查器、Product diff guard 和资源预算文件；默认 diagnostic 全量回归以及五个非默认组合构建均已执行。

## 当前验证结果

- 默认 `diagnostic_sd5091`：28/28 CTest 通过。
- 配置生成器：12 项正向/负向测试通过，覆盖未定义 symbol、重复 YAML key、核心/能力/平台缺源、依赖缺失、冲突、环和路径穿越。
- 裁剪矩阵：`no_alarm`、`no_mcp`、`no_gateway`、`energy_inspection`、`robot_host` 均构建通过，拒绝符号检查通过。
- 原 install tree 默认仍只包含 `bin/litecrab_server`；发布元数据通过 `LITECRAB_INSTALL_METADATA=ON` 显式启用。

## 未完成的外部门禁与保留限制

- 当前工作区不存在实施计划指定的 Hi3516 `device/soc/hisilicon/common/platform/agent` 工程，无法修改其 `BUILD.gn`/`agent_core.c`，也无法执行正式 GN/Ninja、真机 ASR→Agent→Tool→TTS、8/24/72 小时 soak 或回滚演练。
- SD5091 正式交叉工具链和目标板未在本轮提供；资源预算仍只有 host 基线，不能替代板端 text/data/bss、heap、线程和 fd 实测。

以上未完成项不得标记为发布完成，也不得在没有外部工程和真机证据时勾选阶段 4/6 的目标板验收项。
