#include "It_posix_io.h"

VOID ItSuitePosixIo(void)
{
    dprintf("*********** Begin sample posix pipe test ************\n");
    ItPosixPipe001();
    ItPosixPipe002();
    ItPosixPipe003();
    ItPosixPipe004();
    ItPosixPipe005();
    ItPosixPipe006();
    ItPosixPipe007();
    ItPosixPipe008();
    ItPosixPipe009();
    ItPosixPipe010();
    ItPosixPipe011();
    ItPosixPipe012();
    ItPosixPipe013();
    ItPosixPipe014();
    ItPosixPipe015();
    ItPosixPipe016();
    /* BUG_17(resleak 批次): poll() 多无事件 fd 等待节点泄漏,
     * 修复前隔离(红灯暴露用例, 断言修复后契约) */
#if defined(POSIX_BUG_17_POLL_WAITNODE_LEAK) && POSIX_BUG_17_POLL_WAITNODE_LEAK == 1
    ItPosixPipe017();
#endif
}