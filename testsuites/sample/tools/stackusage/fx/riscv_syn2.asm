riscv_syn2.elf:     file format elf32-littleriscv
00000100 <RvP>:
 100: addi sp,sp,-16
 104: jalr  a5
 108: addi sp,sp,16
 10c: jalr  ra,0(ra)
00000180 <RvB>:
 180: addi sp,sp,-16
 184: addi sp,sp,16
 188: jalr  ra,0(ra)
00000200 <RvC>:
 200: addi sp,sp,-8
 204: beq   a0,a1,0x180 <RvB>
 208: addi sp,sp,8
 20c: jalr  ra,0(ra)
