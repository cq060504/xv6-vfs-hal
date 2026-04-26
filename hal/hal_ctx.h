#ifndef _HAL_CTX_H_
#define _HAL_CTX_H_

#include "types.h"

// ============================================================
// 上下文切换结构体抽象接口
// ============================================================
// struct hal_context 保存 callee-saved 寄存器。
// 各平台汇编 swtch.S 实现对此结构的布局负责。
// 各平台可在本文件中直接定义 struct hal_context。

// ---------- RISC-V 实现 ----------
struct hal_context {
  uint64 ra;
  uint64 sp;
  uint64 s0;
  uint64 s1;
  uint64 s2;
  uint64 s3;
  uint64 s4;
  uint64 s5;
  uint64 s6;
  uint64 s7;
  uint64 s8;
  uint64 s9;
  uint64 s10;
  uint64 s11;
};

// ---------- 上下文切换函数 ----------
void hal_switch(struct hal_context **old, struct hal_context *new);

#endif