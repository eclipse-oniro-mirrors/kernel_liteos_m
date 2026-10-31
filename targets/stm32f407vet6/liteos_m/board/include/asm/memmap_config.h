#ifndef _MEMMAP_CONFIG_H
#define _MEMMAP_CONFIG_H

#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/*
 * Memory map manifest for the kernel/user MPU isolation layout.
 *
 * The fixed slot geometry below must match liteos.ld MEMORY and
 * box/boxes_cfg/boxes.ld. Segment content bounds are taken from linker
 * symbols, so they track the linker scripts automatically.
 *
 * MPU region recipe (v7-M, 8 regions, 5 used):
 *   R0 common text (static)   R1 common data (static)
 *   R2 user read-only (static)
 *   R3 current box code (dynamic, rewritten on box switch)
 *   R4 current box data (dynamic, rewritten on box switch)
 * Kernel flash/RAM, CCM and peripherals carry no region at all: the kernel
 * reaches them through the PRIVDEFENA background map, user access faults.
 */

/* ---------------- fixed slot geometry (MPU region bases) ---------------- */

/* Flash 1MB @0x08000000 (die measured 1MB via FSIZE; part number says 512KB) */
#define KERNEL_FLASH_BASE           0x08000000UL   /* 128K, no MPU region */
#define KERNEL_FLASH_SIZE           0x00020000UL
#define COMMON_FLASH_BASE           0x08020000UL   /* 128K, MPU R0 */
#define COMMON_FLASH_SIZE           0x00020000UL
#define BOX_A_CODE_SLOT_BASE        0x08040000UL   /* 64K,  MPU R3 (dynamic) */
#define BOX_A_CODE_SLOT_SIZE        0x00010000UL
#define BOX_B_CODE_SLOT_BASE        0x08050000UL
#define BOX_B_CODE_SLOT_SIZE        0x00010000UL

/* SRAM 128KB @0x20000000 */
#define KERNEL_RAM_BASE             0x20000000UL   /* 32K,  no MPU region */
#define KERNEL_RAM_SIZE             0x00008000UL
#define COMMON_RAM_BASE             0x20008000UL   /* 16K,  MPU R1 */
#define COMMON_RAM_SIZE             0x00004000UL
#define USER_READONLY_RAM_BASE      0x2000C000UL   /* 16K,  MPU R2 */
#define USER_READONLY_RAM_SIZE      0x00004000UL
#define BOX_A_DATA_SLOT_BASE        0x20010000UL   /* 32K,  MPU R4 (dynamic) */
#define BOX_A_DATA_SLOT_SIZE        0x00008000UL
#define BOX_B_DATA_SLOT_BASE        0x20018000UL
#define BOX_B_DATA_SLOT_SIZE        0x00008000UL

/* CCM 64KB @0x10000000, kernel only, DMA cannot reach it */
#define CCM_RAM_BASE                0x10000000UL
#define CCM_RAM_SIZE                0x00010000UL

/* ---------------- linker symbols (content bounds) ---------------- */

extern CHAR __boot_vector_start;
extern CHAR __boot_vector_end;
extern CHAR __text_start;
extern CHAR __kernel_text_end;
extern CHAR __data_start;
extern CHAR __data_end;
extern CHAR __bss_start;
extern CHAR __bss_end;
/* match target_config.h: extern unsigned int __heap_start */
extern UINT32 __heap_start;
extern UINT32 __heap_end;
extern CHAR __common_text_start;
extern CHAR __common_rodata_start;
extern CHAR __common_rodata_end;
extern CHAR __common_text_end;
extern CHAR __common_data_start;
extern CHAR __common_data_end;
extern CHAR __common_bss_start;
extern CHAR __common_bss_end;
extern CHAR __user_readonly_data_start;
extern CHAR __user_readonly_data_end;
extern CHAR __user_readonly_bss_start;
extern CHAR __user_readonly_bss_end;
extern CHAR __ccm_start;
extern CHAR __ccm_end;
extern CHAR __boxA_text_start;
extern CHAR __boxA_text_end;
extern CHAR __boxA_rodata_start;
extern CHAR __boxA_rodata_end;
extern CHAR __boxA_data_start;
extern CHAR __boxA_data_end;
extern CHAR __boxA_data_load_start;
extern CHAR __boxA_data_load_end;
extern CHAR __boxA_bss_start;
extern CHAR __boxA_bss_end;
extern CHAR __boxA_heap_start;
extern CHAR __boxA_heap_end;
extern CHAR __boxB_text_start;
extern CHAR __boxB_text_end;
extern CHAR __boxB_rodata_start;
extern CHAR __boxB_rodata_end;
extern CHAR __boxB_data_start;
extern CHAR __boxB_data_end;
extern CHAR __boxB_data_load_start;
extern CHAR __boxB_data_load_end;
extern CHAR __boxB_bss_start;
extern CHAR __boxB_bss_end;
extern CHAR __boxB_heap_start;
extern CHAR __boxB_heap_end;

/* ---------------- segment start/end (MPU config input) ---------------- */

#define BOOT_VECTOR_TABLE_START     ((UINTPTR)(&__boot_vector_start))
#define BOOT_VECTOR_TABLE_END       ((UINTPTR)(&__boot_vector_end))
#define KERNEL_TEXT_START           ((UINTPTR)(&__text_start))
#define KERNEL_TEXT_END             ((UINTPTR)(&__kernel_text_end))
#define KERNEL_DATA_START           ((UINTPTR)(&__data_start))
#define KERNEL_DATA_END             ((UINTPTR)(&__data_end))
#define KERNEL_BSS_START            ((UINTPTR)(&__bss_start))
#define KERNEL_BSS_END              ((UINTPTR)(&__bss_end))
#define KERNEL_HEAP_START           ((UINTPTR)(&__heap_start))
#define KERNEL_HEAP_END             ((UINTPTR)(&__heap_end))
#define COMMON_TEXT_START           ((UINTPTR)(&__common_text_start))
#define COMMON_TEXT_END             ((UINTPTR)(&__common_text_end))
#define COMMON_RODATA_START         ((UINTPTR)(&__common_rodata_start))
#define COMMON_RODATA_END           ((UINTPTR)(&__common_rodata_end))
#define COMMON_DATA_START           ((UINTPTR)(&__common_data_start))
#define COMMON_DATA_END             ((UINTPTR)(&__common_data_end))
#define COMMON_BSS_START            ((UINTPTR)(&__common_bss_start))
#define COMMON_BSS_END              ((UINTPTR)(&__common_bss_end))
#define USER_READONLY_DATA_START    ((UINTPTR)(&__user_readonly_data_start))
#define USER_READONLY_DATA_END      ((UINTPTR)(&__user_readonly_data_end))
#define USER_READONLY_BSS_START     ((UINTPTR)(&__user_readonly_bss_start))
#define USER_READONLY_BSS_END       ((UINTPTR)(&__user_readonly_bss_end))
#define CCM_START                   ((UINTPTR)(&__ccm_start))
#define CCM_END                     ((UINTPTR)(&__ccm_end))
#define BOX_A_TEXT_START            ((UINTPTR)(&__boxA_text_start))
#define BOX_A_TEXT_END              ((UINTPTR)(&__boxA_text_end))
#define BOX_A_RODATA_START          ((UINTPTR)(&__boxA_rodata_start))
#define BOX_A_RODATA_END            ((UINTPTR)(&__boxA_rodata_end))
#define BOX_A_DATA_START            ((UINTPTR)(&__boxA_data_start))
#define BOX_A_DATA_END              ((UINTPTR)(&__boxA_data_end))
#define BOX_A_DATA_LOAD_START       ((UINTPTR)(&__boxA_data_load_start))
#define BOX_A_DATA_LOAD_END         ((UINTPTR)(&__boxA_data_load_end))
#define BOX_A_BSS_START             ((UINTPTR)(&__boxA_bss_start))
#define BOX_A_BSS_END               ((UINTPTR)(&__boxA_bss_end))
#define BOX_A_HEAP_START            ((UINTPTR)(&__boxA_heap_start))
#define BOX_A_HEAP_END              ((UINTPTR)(&__boxA_heap_end))
#define BOX_B_TEXT_START            ((UINTPTR)(&__boxB_text_start))
#define BOX_B_TEXT_END              ((UINTPTR)(&__boxB_text_end))
#define BOX_B_RODATA_START          ((UINTPTR)(&__boxB_rodata_start))
#define BOX_B_RODATA_END            ((UINTPTR)(&__boxB_rodata_end))
#define BOX_B_DATA_START            ((UINTPTR)(&__boxB_data_start))
#define BOX_B_DATA_END              ((UINTPTR)(&__boxB_data_end))
#define BOX_B_DATA_LOAD_START       ((UINTPTR)(&__boxB_data_load_start))
#define BOX_B_DATA_LOAD_END         ((UINTPTR)(&__boxB_data_load_end))
#define BOX_B_BSS_START             ((UINTPTR)(&__boxB_bss_start))
#define BOX_B_BSS_END               ((UINTPTR)(&__boxB_bss_end))
#define BOX_B_HEAP_START            ((UINTPTR)(&__boxB_heap_start))
#define BOX_B_HEAP_END              ((UINTPTR)(&__boxB_heap_end))

/* ------------ box framework glue (SysMemLayout in boxes_conf.c) ---------
 * The framework's shared "usr" area maps onto the common sections:
 * text+rodata from COMMON, data+bss spanning the whole COMMON_RAM slot. */
#define SYS_USR_TEXT_START          COMMON_TEXT_START
#define SYS_USR_TEXT_END            COMMON_TEXT_END
#define SYS_USR_RODATA_START        COMMON_RODATA_START
#define SYS_USR_RODATA_END          COMMON_RODATA_END
#define SYS_USR_DATA_START          COMMON_DATA_START
#define SYS_USR_DATA_END            COMMON_BSS_END

/* ---------------- v7-M MPU region recipes ---------------- */

/* Region numbers: R3/R4 are rewritten on box switch. */
#define MPU_REGION_R0_COMMON_TEXT   0
#define MPU_REGION_R1_COMMON_DATA   1
#define MPU_REGION_R2_USER_READONLY 2
#define MPU_REGION_R3_BOX_CODE      3
#define MPU_REGION_R4_BOX_DATA      4

/* RASR = XN(28) | AP(26:24) | C(17) B(16) | SIZE(7:1) | EN(0).
 * Ready-made values for the fixed slots; bases from the *_SLOT macros. */
#define MPU_RASR_COMMON_TEXT        0x03020021UL   /* 128K AP=011 RO/RO, executable, WT */
#define MPU_RASR_COMMON_DATA        0x1303001BUL   /* 16K  AP=011 RW/RW, XN, WBWA */
#define MPU_RASR_USER_READONLY      0x1203001BUL   /* 16K  AP=010 kernel RW / user RO, XN */
#define MPU_RASR_BOX_CODE           0x0302001FUL   /* 64K  AP=011 RO/RO, executable, WT */
#define MPU_RASR_BOX_DATA           0x1303001DUL   /* 32K  AP=011 RW/RW, XN, WBWA */

#define MPU_CTRL_ENABLE_PRIVDEF     0x5UL          /* ENABLE | PRIVDEFENA */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _MEMMAP_CONFIG_H */
