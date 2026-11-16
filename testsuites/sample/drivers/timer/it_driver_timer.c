/* it_driver_timer.c — timer 驱动域聚合（api/功能/压力三段编号，按序执行） */
#include "it_driver_timer.h"

VOID ItDriverTimerSuite(VOID)
{
    ItDriverTimer201();
    ItDriverTimer202();
    ItDriverTimer203();
    // ItDriverTimer204();   /* 临时屏蔽：间歇失败 RetCode:0x4，后续单独调 */
    ItDriverTimer205();
    ItDriverTimer206();
    ItDriverTimer207();
    ItDriverTimer208();
    ItDriverTimer209();
    ItDriverTimer210();
    ItDriverTimer211();
}
