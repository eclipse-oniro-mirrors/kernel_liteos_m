#ifndef _SOC_H
#define _SOC_H

/* Hi3516DV300 SoC peripheral base addresses (1:1 mapped, VA = PA).
 * GIC base + offsets: GICD at 0x10301000, GICC at 0x10302000.
 */
#define GIC_BASE_ADDR                         0x10300000U
#define GICD_OFFSET                           0x1000
#define GICC_OFFSET                           0x2000

#define UART0_BASE                            0x120a0000U

#endif /* _SOC_H */
