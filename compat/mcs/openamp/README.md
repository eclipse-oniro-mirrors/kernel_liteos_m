# components/mcs/openamp - LiteOS-M adaptation layer for open-amp/libmetal

This directory follows the same two-layer convention as the other
third-party integrations in this tree (e.g. components/net/lwip-2.1 +
third_party/lwip, components/fs/fatfs + third_party/FatFs):

  third_party/libmetal, third_party/open-amp   vanilla upstream snapshots
  components/mcs/openamp/                       this directory: LiteOS glue

## Contents

    libmetal/metal_sys.c          libmetal "liteos" system backend (sys entry)
    libmetal/libmetal-system/     backend headers overlaid onto the generated
                                   metal include set at build time (io, irq,
                                   cache, sys, mutex, sleep, ...)

## How the slave image is built

The AMP slave firmware (bare-metal LiteOS-M on CPU3) is currently built by a
standalone Makefile (kernel/liteos_m/vendor/hihope/dayu210/Makefile), NOT by the GN
kernel build: the slave image is a separate -nostdlib ELF, not part of the
standard LiteOS-M kernel image. That Makefile references this tree as:

    METAL_ROOT   ?= $(OH_ROOT)/kernel/liteos_m/third_party/libmetal
    OPENAMP_ROOT ?= $(OH_ROOT)/kernel/liteos_m/third_party/open-amp
    METAL_BACKEND = $(OH_ROOT)/kernel/liteos_m/components/mcs/openamp/libmetal

and generates the libmetal include set by copying the vanilla headers,
resolving the cmake @PROJECT_*@ placeholders, then overlaying
libmetal-system/ as metal/system/liteos/.

## Slave-side trimming (recorded from the working build)

libmetal: only init.c, device.c, io.c, log.c (+ backend metal_sys.c);
dma.c/shmem.c/irq.c/softirq.c/version.c are unused on the bare-metal slave.

open-amp: only virtio.c, virtqueue.c, remoteproc_virtio.c, rpmsg.c,
rpmsg_virtio.c; elf_loader.c/remoteproc.c/rsc_table_parser.c are
master-side only (the slave is the loaded image).

## GN/Kconfig integration (reserved)

BUILD.gn and Kconfig in this directory are placeholders. Wiring this
component into the standard LiteOS-M GN build would be needed only if
openamp ever becomes part of the standard single-core kernel image.
