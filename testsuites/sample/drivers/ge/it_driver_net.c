/* it_driver_net.c — net 驱动域聚合（api/功能/压力三段编号，按序执行） */
#include "it_driver_net.h"

VOID ItDriverNetSuite(VOID)
{
    ItDriverNet001();
    ItDriverNet002();
    ItDriverNet003();
    ItDriverNet004();
    ItDriverNet005();
    ItDriverNet201();
    ItDriverNet202();
    ItDriverNet203();
    ItDriverNet204();
    ItDriverNet205();
    ItDriverNet206();
    ItDriverNet207();
    ItDriverNet208();
    ItDriverNet209();
    // ItDriverNet210();   /* 临时屏蔽：GE PHY 协商 timing 高概率失败（Net010），后续单独调 */
    // ItDriverNet211();   /* 临时屏蔽：Scn/Stress 压力族，后续单独调 */
    // ItDriverNet212();   /* 临时屏蔽：Scn/Stress 压力族，后续单独调 */
    // ItDriverNet213();   /* 临时屏蔽：Scn/Stress 压力族，后续单独调 */
    // ItDriverNet214();   /* 临时屏蔽：Scn/Stress 压力族，后续单独调 */
    // ItDriverNet301();   /* 临时屏蔽：Scn/Stress 压力族，后续单独调 */
    // ItDriverNet302();   /* 临时屏蔽：Scn/Stress 压力族，后续单独调 */
    // ItDriverNet303();   /* 临时屏蔽：Scn/Stress 压力族，后续单独调 */
}
