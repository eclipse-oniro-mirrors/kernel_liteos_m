#include "It_posix_signal.h"

VOID ItSuitePosixSignal(void)
{
    dprintf("*********** Begin sample posix signal test ************\n");
    ItPosixSignal084();
    ItPosixSignal085();
    ItPosixSignal086();
    ItPosixSignal087();
    ItPosixSignal088();
    ItPosixSignal089();
    ItPosixSignal090();
#if defined(POSIX_BUG_03_SIGTIMEDWAIT) && POSIX_BUG_03_SIGTIMEDWAIT == 1
    /* BUG_03 修复前隔离(不注册不执行), 修复后置 1 恢复 */
    ItPosixSignal091();
#endif
    ItPosixSignal092();
    ItPosixSignal093();
    ItPosixSignal094();
    ItPosixSignal095();
    ItPosixSignal096();
    ItPosixSignal097();
    ItPosixSignal098();
    ItPosixSignal099();
}
