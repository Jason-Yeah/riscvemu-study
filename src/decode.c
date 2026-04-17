#include "../inc/riscvemu.h"

// 00 01 10是压缩指令的象限，11是普通指令
#define QUADRANT(data) ((data >> 0) & 0x3) // RVC指令的象限，RVC指令的编码格式中，最低两位用于表示指令的象限，共有四个象限（0、1、2、3）

void insn_decode(insn_t *insn, u32 data)
{

}