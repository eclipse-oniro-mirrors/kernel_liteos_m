/*
 * Copyright (c) 2013-2019 Huawei Technologies Co., Ltd. All rights reserved.
 * Copyright (c) 2020-2021 Huawei Device Co., Ltd. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification,
 * are permitted provided the following conditions are met:
 *
 * 1. Redistributions of source code must retain the above copyright notice, this list of
 * conditions and the following disclaimer.
 *
 * 2. Redistributions in binary form must reproduce the above copyright notice, this list of
 * conditions and the following disclaimer in the documentation and other materials
 * provided with the distribution.
 *
 * 3. Neither the name of the copyright holder nor the names of its contributors may be used
 * to endorse or promote products derived from this software without specific prior written
 * permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO,
 * THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR
 * PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR
 * CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL,
 * EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/*
 * X-Macro 单源定义：每行一个枚举名，由 iCunit.h 的枚举和 iCunit.c 的
 * switch 同时展开。加新项只需在此加一行，枚举/字符串自动同步。
 *
 * 功能模块宏基线 = test-designer feature_baseline/features.csv（81 项 FN，20261108 FN_Compat_MCSPthread 并入 FN_Compat_POSIX），
 * 命名规则 TEST_<FN_ID 大写>；用例-功能对应关系见
 * feature_baseline/case_feature_map.csv（由 gen_case_feature_map.py 生成）。
 * 0 用例 FN 亦保留宏定义：iCunit 报告 'Not Executed Model' 行即其审计证据。
 */

/* ---- Kernel ---- */
ICUNIT_MOD(TEST_FN_KERNEL_TASKMGMT)  /* 任务管理 */
ICUNIT_MOD(TEST_FN_KERNEL_TASKSCHED)  /* 任务调度 */
ICUNIT_MOD(TEST_FN_KERNEL_IRQ)  /* 中断管理 */
ICUNIT_MOD(TEST_FN_KERNEL_TIME)  /* 时间管理 */
ICUNIT_MOD(TEST_FN_KERNEL_DYNMEM)  /* 动态内存 */
ICUNIT_MOD(TEST_FN_KERNEL_MEMBOX)  /* 静态内存池 */
ICUNIT_MOD(TEST_FN_KERNEL_SEM)  /* 信号量 */
ICUNIT_MOD(TEST_FN_KERNEL_MUTEX)  /* 互斥锁 */
ICUNIT_MOD(TEST_FN_KERNEL_MSGQUEUE)  /* 消息队列 */
ICUNIT_MOD(TEST_FN_KERNEL_EVENT)  /* 事件标志 */
ICUNIT_MOD(TEST_FN_KERNEL_RWSEM)  /* 读写信号量 */
ICUNIT_MOD(TEST_FN_KERNEL_SWTMR)  /* 软件定时器 */
ICUNIT_MOD(TEST_FN_KERNEL_SIGNAL)  /* 信号处理 */
ICUNIT_MOD(TEST_FN_KERNEL_EXC)  /* 异常管理 */
ICUNIT_MOD(TEST_FN_KERNEL_BACKTRACE)  /* 栈回溯 */
ICUNIT_MOD(TEST_FN_KERNEL_ARCH)  /* 上下文切换与架构 */
ICUNIT_MOD(TEST_FN_KERNEL_MPU)  /* 内存保护 */
ICUNIT_MOD(TEST_FN_KERNEL_CPUP)  /* CPU占用率 */
/* ---- KernelExt ---- */
ICUNIT_MOD(TEST_FN_KERNELEXT_POWER)  /* 电源管理 */
/* ---- Kernel ---- */
ICUNIT_MOD(TEST_FN_KERNEL_INIT)  /* 内核初始化 */
ICUNIT_MOD(TEST_FN_KERNEL_ERROR)  /* 错误处理 */
ICUNIT_MOD(TEST_FN_KERNEL_PRINTF)  /* 打印与日志 */
/* ---- KernelExt ---- */
ICUNIT_MOD(TEST_FN_KERNELEXT_TRACE)  /* 软件跟踪 */
ICUNIT_MOD(TEST_FN_KERNELEXT_LMS)  /* LMS内存检测 */
ICUNIT_MOD(TEST_FN_KERNELEXT_EXCHOOK)  /* 异常钩子 */
ICUNIT_MOD(TEST_FN_KERNELEXT_SHELL)  /* Shell命令行 */
/* ---- Compat ---- */
ICUNIT_MOD(TEST_FN_COMPAT_POSIX)  /* POSIX兼容 */
ICUNIT_MOD(TEST_FN_COMPAT_CMSIS)  /* CMSIS兼容 */
/* ---- FS ---- */
ICUNIT_MOD(TEST_FN_FS_VFS)  /* 文件系统 */
/* ---- Lib ---- */
ICUNIT_MOD(TEST_FN_LIB_MATH)  /* musl数学与随机 */
ICUNIT_MOD(TEST_FN_LIB_STRING)  /* musl字符与字符串 */
ICUNIT_MOD(TEST_FN_LIB_STDIO)  /* musl标准IO */
ICUNIT_MOD(TEST_FN_LIB_TIME)  /* musl时间 */
ICUNIT_MOD(TEST_FN_LIB_MISC)  /* musl标准库 */
ICUNIT_MOD(TEST_FN_LIB_SAFEC)  /* 安全C函数库 */
/* ---- BSP ---- */
ICUNIT_MOD(TEST_FN_BSP_QEMU)  /* QEMU板级支持 */
ICUNIT_MOD(TEST_FN_BSP_WS63)  /* WS63板级支持 */
/* ---- Lib ---- */
ICUNIT_MOD(TEST_FN_LIB_ARMOPT)  /* ARM优化例程 */
/* ---- BuildTool ---- */
ICUNIT_MOD(TEST_FN_BUILDTOOL_RAMPRESET)  /* 内核内存挡位预设 */
/* ---- TestTool ---- */
ICUNIT_MOD(TEST_FN_TESTTOOL_FOOTPRINT)  /* 内核RAM占用统计工具 */
ICUNIT_MOD(TEST_FN_TESTTOOL_STACKUSAGE)  /* 静态栈使用分析工具 */
/* ---- KernelExt ---- */
ICUNIT_MOD(TEST_FN_KERNELEXT_HPL)  /* 高性能日志压缩 */
/* ---- RK3568MKernel ---- */
ICUNIT_MOD(TEST_FN_RK3568MKERNEL_MCSKM)  /* RK3568主核-mcs_km内核让渡框架 */
ICUNIT_MOD(TEST_FN_RK3568MKERNEL_MCSNET)  /* RK3568主核-GMAC1让渡驱动(mcs_net) */
ICUNIT_MOD(TEST_FN_RK3568MKERNEL_MCSCAN)  /* RK3568主核-CAN让渡驱动(mcs_can单模块多口) */
/* ---- RK3568MUserlib ---- */
ICUNIT_MOD(TEST_FN_RK3568MUSERLIB_MICA)  /* RK3568主核-libmica通信库 */
ICUNIT_MOD(TEST_FN_RK3568MUSERLIB_UMT)  /* RK3568主核-user_msg UMT用户态大块传输 */
ICUNIT_MOD(TEST_FN_RK3568MUSERLIB_MICACOMPAT)  /* RK3568主核-libmica-compat musl兼容shim */
/* ---- RK3568MLib ---- */
ICUNIT_MOD(TEST_FN_RK3568MLIB_OPENAMP)  /* RK3568主核-open-amp/libmetal预编译库 */
/* ---- RK3568MImage ---- */
ICUNIT_MOD(TEST_FN_RK3568MIMAGE_DTS)  /* RK3568主核-dts让渡补丁(boot_linux.img) */
/* ---- RK3568SPort ---- */
ICUNIT_MOD(TEST_FN_RK3568SPORT_ARM64)  /* arm64 AMP架构端口 */
/* ---- RK3568SMcs ---- */
ICUNIT_MOD(TEST_FN_RK3568SMCS_MICA)  /* mcs/mica从核框架(openamp移植) */
ICUNIT_MOD(TEST_FN_RK3568SMCS_MICASVC)  /* mica内置服务×4 */
ICUNIT_MOD(TEST_FN_RK3568SMCS_NPPROXY)  /* np_* socket代理API */
/* ---- RK3568SDrv ---- */
ICUNIT_MOD(TEST_FN_RK3568SDRV_UART)  /* UART驱动(Modbus-RTU契约) */
ICUNIT_MOD(TEST_FN_RK3568SDRV_CAN)  /* CAN数据面驱动(2.0B+FD) */
/* ---- RK3568SDrvRsv ---- */
ICUNIT_MOD(TEST_FN_RK3568SDRVRSV_GMAC)  /* GMAC数据面驱动(在树未编入) */
/* ---- RK3568MRsv ---- */
ICUNIT_MOD(TEST_FN_RK3568MRSV_PEDESTAL)  /* 异构pedestal+三方库重编(预留) */
/* ---- Compat ---- */
/* ---- RK3568MKernel ---- */
ICUNIT_MOD(TEST_FN_RK3568MKERNEL_MCSUART)  /* RK3568主核-UART3让渡驱动(mcs_uart) */
/* ---- RK3568SDrv ---- */
ICUNIT_MOD(TEST_FN_RK3568SDRV_TIMER)  /* 从核TIMER数据面驱动(TIMER1) */
/* ---- Kernel ---- */
ICUNIT_MOD(TEST_FN_KERNEL_DATASTRUCT)  /* 内核基础数据结构 */
ICUNIT_MOD(TEST_FN_KERNEL_DEBUG)  /* 调试支持 */
/* ---- Net ---- */
ICUNIT_MOD(TEST_FN_NET_LWIPPORT)  /* 网络协议栈移植 */
/* ---- Lib ---- */
ICUNIT_MOD(TEST_FN_LIB_NET)  /* musl网络支撑 */
ICUNIT_MOD(TEST_FN_LIB_MBEDTLS)  /* mbedTLS加密库 */
/* ---- Net ---- */
ICUNIT_MOD(TEST_FN_NET_LWIPCORE)  /* lwIP协议栈内核 */
ICUNIT_MOD(TEST_FN_NET_LWIPAPI)  /* lwIP网络API */
ICUNIT_MOD(TEST_FN_NET_LWIPNETIF)  /* lwIP网络接口 */
/* ---- SysService ---- */
ICUNIT_MOD(TEST_FN_SYSSERVICE_HUKS)  /* HUKS密钥管理 */
ICUNIT_MOD(TEST_FN_SYSSERVICE_BOOTSTRAP)  /* 系统启动与参数 */
ICUNIT_MOD(TEST_FN_SYSSERVICE_SAMGR)  /* 服务管理框架 */
ICUNIT_MOD(TEST_FN_SYSSERVICE_HILOG)  /* HiLog轻量日志 */
ICUNIT_MOD(TEST_FN_SYSSERVICE_HIVIEW)  /* HiView/HiEvent维测 */
/* ---- KernelExt ---- */
ICUNIT_MOD(TEST_FN_KERNELEXT_CPP)  /* C++支持 */
ICUNIT_MOD(TEST_FN_KERNELEXT_DYNLINK)  /* 动态加载与链接 */
/* ---- FS ---- */
ICUNIT_MOD(TEST_FN_FS_FATFS)  /* FATFS 适配 */
/* ---- Compat ---- */
ICUNIT_MOD(TEST_FN_COMPAT_IAR)  /* IAR 工具链适配 */
/* ---- KernelExt ---- */
ICUNIT_MOD(TEST_FN_KERNELEXT_LMK)  /* LMK 低内存杀手 */
ICUNIT_MOD(TEST_FN_KERNELEXT_SANDBOX)  /* 安全沙箱 */
/* ---- Compat ---- */
ICUNIT_MOD(TEST_FN_COMPAT_NEWLIB)  /* newlib 适配 */
/* ---- BSP ---- */
ICUNIT_MOD(TEST_FN_BSP_HDF)  /* HDF 驱动框架接入 */

#if defined(LOSCFG_3RDPARTY_TEST)
ICUNIT_MOD(TEST_THTTPD)
ICUNIT_MOD(TEST_BIDIREFC)
ICUNIT_MOD(TEST_CJSON)
ICUNIT_MOD(TEST_CURL)
ICUNIT_MOD(TEST_FFMPEG)
ICUNIT_MOD(TEST_FREETYPE)
ICUNIT_MOD(TEST_INIPARSER)
ICUNIT_MOD(TEST_JSONCPP)
ICUNIT_MOD(TEST_LIBICONV)
ICUNIT_MOD(TEST_LIBJPEG)
ICUNIT_MOD(TEST_LIBPNG)
ICUNIT_MOD(TEST_OPENEXIF)
ICUNIT_MOD(TEST_OPENSSL)
ICUNIT_MOD(TEST_OPUS)
ICUNIT_MOD(TEST_SQLITE)
ICUNIT_MOD(TEST_TINYXML)
#endif
