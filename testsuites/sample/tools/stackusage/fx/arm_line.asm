arm_line.elf:     file format elf32-littlearm
00000480 <FuncG>:
/src/los_demo.c:10
    ret = DoPrepare(ctx);
 480: push  {r4, lr}
/src/los_demo.c:11
    ret = (*pfnTask)(ctx);
 484: blx r3
/src/los_demo.c:12
    return ret;
 488: pop  {r4, pc}
00000180 <FuncB>:
/src/los_demo.c:20
 180: push  {r4, r5, lr}
 184: sub sp, #4
 188: add sp, #4
 18c: pop  {r4, r5, pc}
