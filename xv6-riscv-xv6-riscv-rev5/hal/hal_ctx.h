// Context switch abstraction.
// struct hal_context holds callee-saved registers.
// Layout is consumed by the platform hal_swtch.S.

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

#define CONTEXT_OFFSET(member, offset) \
  _Static_assert(__builtin_offsetof(struct hal_context, member) == (offset), \
                 "hal_context." #member " offset changed")
CONTEXT_OFFSET(ra, 0);
CONTEXT_OFFSET(sp, 8);
#ifdef ARCH_loongarch
CONTEXT_OFFSET(fp, 16);
CONTEXT_OFFSET(s0, 24);
CONTEXT_OFFSET(s1, 32);
CONTEXT_OFFSET(s2, 40);
CONTEXT_OFFSET(s3, 48);
CONTEXT_OFFSET(s4, 56);
CONTEXT_OFFSET(s5, 64);
CONTEXT_OFFSET(s6, 72);
CONTEXT_OFFSET(s7, 80);
CONTEXT_OFFSET(s8, 88);
_Static_assert(sizeof(struct hal_context) == 96, "LoongArch context size changed");
#else
CONTEXT_OFFSET(s0, 16);
CONTEXT_OFFSET(s1, 24);
CONTEXT_OFFSET(s2, 32);
CONTEXT_OFFSET(s3, 40);
CONTEXT_OFFSET(s4, 48);
CONTEXT_OFFSET(s5, 56);
CONTEXT_OFFSET(s6, 64);
CONTEXT_OFFSET(s7, 72);
CONTEXT_OFFSET(s8, 80);
CONTEXT_OFFSET(s9, 88);
CONTEXT_OFFSET(s10, 96);
CONTEXT_OFFSET(s11, 104);
_Static_assert(sizeof(struct hal_context) == 112, "RISC-V context size changed");
#endif
#undef CONTEXT_OFFSET

// Save current registers in old, load from new.
void hal_switch(struct hal_context *old, struct hal_context *new);

#endif
