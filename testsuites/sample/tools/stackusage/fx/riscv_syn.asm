riscv_syn.elf:     file format elf32-littleriscv
00000100 <RvA>:
 100: addi sp,sp,-32
 104: jal  ra,0x180 <RvB>
 108: addi sp,sp,32
 10c: jalr  ra,0(ra)
00000180 <RvB>:
 180: addi sp,sp,-16
 184: addi sp,sp,16
 188: jalr  ra,0(ra)
00000200 <RvTail>:
 200: addi sp,sp,-24
 204: addi sp,sp,24
 208: j    0x180 <RvB>
00000280 <RvPush>:
 280: c.push -16
 284: c.pop   16
 288: jalr  ra,0(ra)
