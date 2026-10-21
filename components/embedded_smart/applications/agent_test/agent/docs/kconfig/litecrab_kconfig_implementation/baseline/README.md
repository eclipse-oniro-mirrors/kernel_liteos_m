# LiteCrab Kconfig 改造基线

阶段 0 基线采样于源码提交 `c18da842b38ce58f143439b9e463e4500be2d588`，宿主环境为
WSL Ubuntu、GCC 11.4.0、CMake 3.22.1。采样命令：

```sh
tests/config_matrix/baseline.sh build-baseline
```

基线脚本执行全量 configure、build、CTest，并采集 `litecrab_server` 的 SHA-256、
text/data/bss、动态依赖以及 `liblitecrab.a` 的公开符号数量。报告写入构建目录，避免把
与工具链和绝对环境绑定的二进制快照提交到仓库。

初始基线结果：原有 20 项测试全部通过；加入只包含公开头文件的 API 契约测试后应为
21 项。原始服务器段大小为 text=279722、data=21912、bss=3326536 bytes；这些数值仅
用于当前宿主工具链上的回归比较，不作为 SD5091 或 Hi3516 的资源预算。

当前工作区没有实施计划指定的 Hi3516 `device/soc/hisilicon/common/platform/agent`
源码树，因此阶段 0 只能冻结主仓接口。真机 API、产物 size、ASR→Agent→TTS 日志和
回滚包必须在外部 3516 工程可用后补采，不能以 host 结果代替。
