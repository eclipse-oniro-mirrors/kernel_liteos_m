
#ifndef _ARCH_ASM_H
#define _ARCH_ASM_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

.equ MPIDR_CPUID_MASK, 0xFF

.macro EXC_SP_SET stackBottom, stackSize, reg0, reg1
#ifdef LOSCFG_KERNEL_SMP
    mrc    p15, 0, \reg0, c0, c0, 5
    and    \reg0, \reg0, #MPIDR_CPUID_MASK /* get cpu id */
#else
    mov    \reg0, #0
#endif
    mov    \reg1, #\stackSize
    mul    \reg1, \reg1, \reg0             /* calculate current cpu stack offset */
#ifdef LOSCFG_LIB_CONFIGURABLE
    ldr    \reg0, =\stackBottom
    ldr    \reg0, [\reg0]
#else
    ldr    \reg0, =\stackBottom
#endif
    sub    \reg0, \reg0, \reg1             /* calculate current cpu stack bottom */
    mov    sp, \reg0                       /* set  sp */
.endm

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* _ARCH_ASM_H */
