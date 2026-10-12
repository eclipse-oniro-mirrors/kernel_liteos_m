# components/mcs — 混合部署（mcs）从核侧组件（LiteOS-M / RK3568 AMP）

本目录汇集 AMP 从核（CPU3，bare-metal LiteOS-M）混合部署框架的全部源码，
参照产品仓组件方式管理（vanilla / 适配边界清晰分离）：

```
components/mcs/
  compat/       freestanding POSIX/libc 垫片（stdio/pthread/errno/string/stdlib.h）
                —— libmetal(lib)、libmica(umt/tty) 源码假定宿主 OS 提供 POSIX
                   子集；声明在此，实现在板级 los_libc.c + securec
  openamp/      openamp/libmetal 的 LiteOS 适配层（见 openamp/README.md）
                vanilla 快照在 kernel/liteos_m/third_party/{libmetal,open-amp}
  mica/         libmica 从核侧
    libmica/      openEuler mcs library 子树的原样快照，零修改
    platform/     libmica 平台抽象的 LiteOS 实现（8 头：barrier/irq/sem/io/log...）
    mica_cfg.h    主从协议常量（共享内存窗口/vring/SGI 中断号/buffer 尺寸），
                  必须与主核侧（mcs_km.ko + libmica）约定一致
```

## 从核固件如何消费本组件

从核镜像由板级目录（vendor/hihope/dayu210/）的独立 Makefile 构建（-nostdlib ELF，不编入标准
LiteOS-M 内核镜像，不接内核 GN），引用方式：

    MCS_ROOT      = $(OH_ROOT)/kernel/liteos_m/components/mcs
    MCS_COMPAT    = $(MCS_ROOT)/compat
    METAL_BACKEND = $(MCS_ROOT)/openamp/libmetal
    MICA_DIR      = $(MCS_ROOT)/mica        # mica_cfg.h / libmica/ / platform/

- libmetal 头集在构建时生成：vanilla 头 + cmake 占位符替换 + openamp/libmetal/
  libmetal-system/ 叠加为 `metal/system/liteos/`；
- libmica 头集同理：`mica/libmica/lib/*.h`（含 @PROJECT_SYSTEM@ 占位）+
  `mica/platform/*.h` 叠加为 `mica/platform/system/liteos/`；
- libmica vanilla 的 pthread/errno 依赖经编译期 `-include $(MCS_COMPAT)/...` 注入。

从核裁剪记录：libmetal 只编 init/device/io/log；open-amp 只编 virtio/virtqueue/
remoteproc_virtio/rpmsg/rpmsg_virtio（loader 系主核专用）；libmica 只编
mica_init/mica_service/pedestals/{baremetal,ped_rsc_table}/services/{tty,umt}。

## 与主核侧的关系

主核（CPU0-2，OpenHarmony Linux）侧的 mcs 实现（mcs_km.ko + 用户态 libmica +
三方 so）在 kernel/linux/common_modules/mcs/（树内），与本目录无共享源码；两侧通过 mica_cfg.h 中的协议
常量（共享内存窗口 0x74000000、vring 布局、SGI 7）对齐。
