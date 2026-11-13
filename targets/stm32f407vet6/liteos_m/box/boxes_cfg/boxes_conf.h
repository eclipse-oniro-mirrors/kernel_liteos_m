#ifndef BOXES_CONF_H
#define BOXES_CONF_H

#include "los_typedef.h"

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

#define LOS_BOXA_ID             (0U + (UINT16)LOS_BOX_BASE_ID)
#define LOS_BOXA_STACK_SIZE     0x1000
extern UINT8 g_boxaStack[LOS_BOXA_STACK_SIZE];
extern void boxAEntry(void);
extern char __boxA_text_start;
extern char __boxA_text_end;
extern char __boxA_rodata_start;
extern char __boxA_rodata_end;
extern char __boxA_data_start;
extern char __boxA_data_end;
extern char __boxA_data_load_start;
extern char __boxA_data_load_end;
extern char __boxA_bss_start;
extern char __boxA_bss_end;
extern char __boxA_data_end;
extern char __boxA_heap_start;
extern char __boxA_heap_end;
#define BOXA_TEXT_START         ((UINTPTR)(&__boxA_text_start))
#define BOXA_TEXT_END           ((UINTPTR)(&__boxA_text_end))
#define BOXA_RODATA_START       ((UINTPTR)(&__boxA_rodata_start))   
#define BOXA_RODATA_END         ((UINTPTR)(&__boxA_rodata_end))
#define BOXA_DATA_START         ((UINTPTR)(&__boxA_data_start))
#define BOXA_DATA_END           ((UINTPTR)(&__boxA_data_end))
#define BOXA_DATA_LOAD_START    ((UINTPTR)(&__boxA_data_load_start))
#define BOXA_DATA_LOAD_END      ((UINTPTR)(&__boxA_data_load_end)) 
#define BOXA_DATA_LOAD_SIZE     (BOXA_DATA_LOAD_END - BOXA_DATA_LOAD_START)
#define BOXA_BSS_START          ((UINTPTR)(&__boxA_bss_start))
#define BOXA_BSS_END            ((UINTPTR)(&__boxA_bss_end))
#define BOXA_BSS_SIZE           (BOXA_BSS_END - BOXA_BSS_START)
#define OS_BOXA_POOL_ADDR       ((UINTPTR)(&__boxA_heap_start))
#define OS_BOXA_POOL_END_ADDR   ((UINTPTR)(&__boxA_heap_end))
#define BOXA_HEAP_SIZE          (OS_BOXA_POOL_END_ADDR - OS_BOXA_POOL_ADDR)

#define LOS_BOXB_ID             (1U + (UINT16)LOS_BOX_BASE_ID)
#define LOS_BOXB_STACK_SIZE     0x1000
extern UINT8 g_boxbStack[LOS_BOXB_STACK_SIZE];
extern void boxBEntry(void);
extern void boxBChurnEntry(void);
extern char __boxB_text_start;
extern char __boxB_text_end;
extern char __boxB_rodata_start;
extern char __boxB_rodata_end;
extern char __boxB_data_start;
extern char __boxB_data_end;
extern char __boxB_data_load_start;
extern char __boxB_data_load_end;
extern char __boxB_bss_start;
extern char __boxB_bss_end;
extern char __boxB_data_end;
extern char __boxB_heap_start;
extern char __boxB_heap_end;
#define BOXB_TEXT_START         ((UINTPTR)(&__boxB_text_start))
#define BOXB_TEXT_END           ((UINTPTR)(&__boxB_text_end))
#define BOXB_RODATA_START       ((UINTPTR)(&__boxB_rodata_start))   
#define BOXB_RODATA_END         ((UINTPTR)(&__boxB_rodata_end))
#define BOXB_DATA_START         ((UINTPTR)(&__boxB_data_start))
#define BOXB_DATA_END           ((UINTPTR)(&__boxB_data_end))
#define BOXB_DATA_LOAD_START    ((UINTPTR)(&__boxB_data_load_start))
#define BOXB_DATA_LOAD_END      ((UINTPTR)(&__boxB_data_load_end)) 
#define BOXB_DATA_LOAD_SIZE     (BOXB_DATA_LOAD_END - BOXB_DATA_LOAD_START)
#define BOXB_BSS_START          ((UINTPTR)(&__boxB_bss_start))
#define BOXB_BSS_END            ((UINTPTR)(&__boxB_bss_end))
#define BOXB_BSS_SIZE           (BOXB_BSS_END - BOXB_BSS_START)
#define OS_BOXB_POOL_ADDR       ((UINTPTR)(&__boxB_heap_start))
#define OS_BOXB_POOL_END_ADDR   ((UINTPTR)(&__boxB_heap_end))
#define BOXB_HEAP_SIZE          (OS_BOXB_POOL_END_ADDR - OS_BOXB_POOL_ADDR)
#ifdef __cplusplus
}
#endif
#endif /* BOXES_CONF_H */
