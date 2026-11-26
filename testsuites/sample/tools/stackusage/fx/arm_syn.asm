arm_syn.elf:     file format elf32-littlearm
00000100 <FuncA>:
 100: push  {r4, r5, r6, r7, lr}
 104: sub sp, #16
 108: bl  0x180 <FuncB>
 10c: add sp, #16
 110: pop  {r4, r5, r6, r7, pc}
00000180 <FuncB>:
 180: push  {r4, r5, lr}
 184: sub sp, #4
 188: add sp, #4
 18c: pop  {r4, r5, pc}
00000200 <FuncC>:
 200: push  {r4, lr}
 204: sub sp, #12
 208: add sp, #12
 20c: pop  {r4, pc}
 210: b.w  0x280 <FuncD>
00000280 <FuncD>:
 280: push  {r4, r5, r6, r7, lr}
 284: sub sp, #80
 288: add sp, #80
 28c: pop  {r4, r5, r6, r7, pc}
00000300 <FuncE>:
 300: push  {r4, r5, r6, r7, lr}
 304: sub sp, #20
 308: add sp, #20
 30c: pop  {r4, r5, r6, r7, pc}
 310: b  0x380 <FuncF>
00000380 <FuncF>:
 380: push  {lr}
 384: pop  {pc}
00000400 <FuncR>:
 400: push  {r4, lr}
 404: sub sp, #4
 408: bl  0x400 <FuncR>
 40c: add sp, #4
 410: pop  {r4, pc}
00000480 <FuncG>:
 480: push  {r4, lr}
 484: blx r3
 488: pop  {r4, pc}
00000500 <FuncM>:
 500: push  {lr}
 504: sub sp, #4
 508: bl  0x580 <FuncN>
 50c: add sp, #4
 510: pop  {pc}
00000580 <FuncN>:
 580: push  {r4, lr}
 584: sub sp, #12
 588: bl  0x500 <FuncM>
 58c: add sp, #12
 590: pop  {r4, pc}
00000600 <FuncH>:
 600: push  {lr}
 604: blx r3
 608: pop  {pc}
00000700 <dup>:
 700: push  {r4, lr}
 704: pop  {r4, pc}
00000800 <dup>:
 800: push  {r4, r5, r6, r7, lr}
 804: sub sp, #4
 808: add sp, #4
 80c: pop  {r4, r5, r6, r7, pc}
00000900 <ExcEntry>:
 900: push  {r4, lr}
 904: sub sp, #12
 908: bl  0x980 <ExcDeep>
 90c: add sp, #12
 910: pop  {r4, pc}
00000980 <ExcDeep>:
 980: push  {r4, r5, r6, r7, lr}
 984: sub sp, #80
 988: add sp, #80
 98c: pop  {r4, r5, r6, r7, pc}
00000a00 <IsrOne>:
 a00: push  {r4, lr}
 a04: sub sp, #2
 a08: add sp, #2
 a0c: pop  {r4, pc}
