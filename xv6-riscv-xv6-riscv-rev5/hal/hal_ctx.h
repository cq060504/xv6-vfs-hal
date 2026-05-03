// Context switch abstraction.
// struct hal_context holds callee-saved registers.
// Layout is consumed by the platform swtch.S.

#ifndef _HAL_CTX_H_
#define _HAL_CTX_H_

#include "types.h"

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

// Save *old, load *new.
void hal_switch(struct hal_context **old, struct hal_context *new);

#endif
