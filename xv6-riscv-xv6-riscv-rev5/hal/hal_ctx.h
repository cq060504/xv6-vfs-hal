// Context switch abstraction.
// struct hal_context holds callee-saved registers.
// Layout is consumed by the platform swtch.S.

#ifndef _HAL_CTX_H_
#define _HAL_CTX_H_

#include "types.h"

#ifdef ARCH_loongarch
// LoongArch context (12 callee-saved registers: ra, sp, fp, s0-s8)
struct hal_context {
  uint64 ra;    // $r1
  uint64 sp;    // $r3
  uint64 fp;    // $r22 (s9)
  uint64 s0;    // $r23
  uint64 s1;    // $r24
  uint64 s2;    // $r25
  uint64 s3;    // $r26
  uint64 s4;    // $r27
  uint64 s5;    // $r28
  uint64 s6;    // $r29
  uint64 s7;    // $r30
  uint64 s8;    // $r31
};
#else
// RISC-V context (14 callee-saved registers).
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
#endif

// Save current registers in old, load from new.
void hal_switch(struct hal_context *old, struct hal_context *new);

#endif
