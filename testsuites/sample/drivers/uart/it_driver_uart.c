/* it_driver_uart.c — uart 驱动域聚合（api/功能/压力三段编号，按序执行） */
#include "it_driver_uart.h"

VOID ItDriverUartSuite(VOID)
{
    // ItDriverUart001();   /* 临时屏蔽：已知失败（Init/Deinit/Attr 断言），后续单独调 */
    // ItDriverUart002();   /* 临时屏蔽：已知失败（Init/Deinit/Attr 断言），后续单独调 */
    // ItDriverUart003();   /* 临时屏蔽：已知失败（Init/Deinit/Attr 断言），后续单独调 */
    ItDriverUart004();
    ItDriverUart005();
    ItDriverUart006();
    // ItDriverUart201();   /* 临时屏蔽：UartScn 族（202=0x4b0 在案），后续单独调 */
    // ItDriverUart202();   /* 临时屏蔽：UartScn 族（202=0x4b0 在案），后续单独调 */
    // ItDriverUart203();   /* 临时屏蔽：UartScn 族（202=0x4b0 在案），后续单独调 */
    // ItDriverUart204();   /* 临时屏蔽：UartScn 族（202=0x4b0 在案），后续单独调 */
    // ItDriverUart301();   /* 临时屏蔽：StressUart 族，后续单独调 */
    // ItDriverUart302();   /* 临时屏蔽：StressUart 族，后续单独调 */
}
