# LiteCrab Kconfig 方案导航

## 1. 当前权威方案

后续 Kconfig 设计与实现以 [`LiteCrab能力原子化Kconfig组合架构方案.md`](./LiteCrab能力原子化Kconfig组合架构方案.md) 为准。

针对当前仓库，推荐先按 [`LiteCrab渐进式Kconfig最小改造方案.md`](./LiteCrab渐进式Kconfig最小改造方案.md) 落地：保持能力原子化目标，但首期保留现有目录、使用一个中央 Manifest 和一个配置脚本，只抽取合并机器人分叉必需的扩展点。完整 Capability Package 结构作为后续可选治理，不作为首期前置条件。

逐阶段落地、文件修改、代码示例、流程图和测试门槛见 [`litecrab_kconfig_implementation/README.md`](./litecrab_kconfig_implementation/README.md)。

核心原则：

- Alarm、MCP、TCP Gateway、Skills、Memory、Workspace Tools、Process Tool、Robot Tools、TTS 等全部是原子 Capability；
- Product/场景不拥有功能源码，只通过 defconfig 选择 Capability、Platform、产物类型和资源预算；
- 所有可选 Capability 默认关闭，正式 Product Profile 显式选择，不使用 `default y if SCENE_*`；
- 构建器根据 Capability Manifest 自动生成 CMake/GN 源码列表、依赖和生命周期注册表；
- 已有能力范围内新增场景，只增加 defconfig、Product 数据和测试数据，不修改 C 代码；
- 新需求如果包含新协议、硬件或业务状态机，先实现一次新的 Capability，之后所有场景配置复用。

## 2. 历史设计参考

[`LiteCrab单仓多场景Kconfig架构设计方案.md`](./LiteCrab单仓多场景Kconfig架构设计方案.md) 保留两份代码差异、Core 扩展点、Platform、CMake/GN 和迁移分析。

其中以下设计不再作为实施依据：

- 固定 `SCENE_DIAGNOSTIC/SCENE_ROBOT/SCENE_CUSTOM` choice；
- Feature 或资源参数按场景名给默认值；
- 每个 Scenario 编写 C composition 并手工维护 Feature 数组；
- Alarm 依赖 TCP Gateway、MCP 归属于 diagnostic、Robot Tools 归属于 robot 等场景绑定。

## 3. 新场景交付物

在能力已经存在时，新场景只需要：

```text
defconfigs/<product>.defconfig
products/<product>/product.yml
products/<product>/prompt.md          # 可选
tests/products/<product>_smoke.yml    # 数据驱动测试
```

构建示例：

```sh
cmake -S . -B build/<product> -DLITECRAB_PROFILE=<product>
cmake --build build/<product> -j2
```

或者在 OpenHarmony/GN 产品中使用统一包装器预生成 Kconfig/GNI 后执行产品构建。

