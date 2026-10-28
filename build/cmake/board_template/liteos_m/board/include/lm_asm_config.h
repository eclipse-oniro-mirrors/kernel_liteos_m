/*
 * lm_asm_config.h — assembly-safe minimal config, force-included into
 * ASM TUs only (CMakeLists.txt COMPONENT_CCFLAGS). target_config.h pulls
 * musl typedefs into asm and is NOT assembly-safe; this header provides
 * the minimal definitions ASM sources reference, with no C typedefs.
 *
 * Copy and fill in your board's values. Mirror these two from your port's
 * target_config.h — they are the ASM/C consistency points that prevent
 * layout split (TaskContext) and bring-up crashes (hwiNum mask):
 *
 *   LOSCFG_ARCH_FPU_DOUBLE — FPU reg save width; MUST match -march *d
 *     in CCFLAGS (1 = RV32D: fsd/fld, FPUREGBYTES=8). Mismatch → C/ASM
 *     TaskContext layouts diverge → runtime stack corruption
 *     (los_dispatch.obj emitted fsw with 4-byte offsets while C TUs
 *     compiled the 64-bit layout — the P1 bug this file exists to close).
 *
 *   LOSCFG_ARCH_LINX_M — LinxCore131/170: mcause carries SoC-specific
 *     priority/custom bits above the 12-bit IRQ number, so the hwiNum
 *     mask must be 0xFFF (cpu.h gates MCAUSE_MASK_INT_NUM on this).
 *     Undefined → 0x7FFFFFFF default → priority bits leak into hwiNum →
 *     &g_hwiHandleForm[hwiNum] = base + (huge << 4) aliases RAM
 *     (bit 31 set, 0x9xxxxxxx) → load access fault in OsIntHandle
 *     (mtval=0x90017ebc, mepc=OsIntHandle+0x14 — the bring-up crash).
 *
 * Keep minimal — only what ASM source references (stack sizes, IRQ count,
 * heap layout). See the 3322 reference:
 * src/kernel/liteos_m_port/include/lm_asm_config.h.
 */
#ifndef LM_ASM_CONFIG_H
#define LM_ASM_CONFIG_H

/* Stack sizes — must match your port's target_config.h */
#define OS_SYS_CLOCK                        32000000UL
#define LOSCFG_BASE_CORE_TSK_MIN_STACK_SIZE 1024

/* === CRITICAL: ASM/C consistency points (mirror from target_config.h) ===
 * Defaults below reproduce the 3322 reference (RV32D + LinxCore131).
 * Adjust to your SoC; the #ifndef guards let your board override. */
#ifndef LOSCFG_ARCH_FPU_DOUBLE
#define LOSCFG_ARCH_FPU_DOUBLE 1
#endif

#ifndef LOSCFG_ARCH_LINX_M
#define LOSCFG_ARCH_LINX_M 1
#endif

/* IRQ / trap layout — fill in per your SoC */
#define OS_HWI_MAX_NUM                      64

#endif /* LM_ASM_CONFIG_H */
