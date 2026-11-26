arm_tree.elf:     file format elf32-littlearm
00000100 <TreeRoot>:
 100: push  {r4, lr}
 104: sub   sp, #8
 108: bl    0x180 <TA>
 10c: bl    0x280 <TB>
 110: add   sp, #8
 114: pop   {r4, pc}
00000180 <TA>:
 180: push  {r4, r5, r6, r7, lr}
 184: sub   sp, #44
 188: add   sp, #44
 18c: pop   {r4, r5, r6, r7, pc}
00000280 <TB>:
 280: push  {r4, lr}
 284: sub   sp, #4
 288: add   sp, #4
 28c: pop   {r4, pc}
