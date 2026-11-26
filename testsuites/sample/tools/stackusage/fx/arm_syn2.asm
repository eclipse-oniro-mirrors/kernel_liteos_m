arm_syn2.elf:     file format elf32-littlearm
00000100 <FuncX>:
 100: push  {r4, lr}
 104: mov   sp, r8
 108: b     0x180 <FuncD>
00000120 <FuncY>:
 120: push  {r4, lr}
 124: subeq.w sp, #16
 128: pop   {r4, pc}
00000140 <FuncZ>:
 140: push  {r4, r5}
 144: sub   sp, #16
 148: add   sp, #16
 14c: popeq {r4, r5}
 150: b     0x180 <FuncD>
00000160 <FuncL>:
 160: push  {lr}
 164: b     0x20c <FuncA+0xc>
00000180 <FuncD>:
 180: push  {r4, r5, r6, r7, lr}
 184: sub   sp, #80
 188: add   sp, #80
 18c: pop   {r4, r5, r6, r7, pc}
00000200 <FuncA>:
 200: push  {r4, lr}
 204: sub   sp, #4
 208: bl    0x180 <FuncD>
 20c: add   sp, #4
 210: pop   {r4, pc}
00000500 <FuncP>:
 500: push  {lr}
 504: bl    0x580 <FuncQ>
 508: pop   {pc}
00000580 <FuncQ>:
 580: push  {r4, lr}
 584: pop   {r4, pc}
 588: b     0x500 <FuncP>
