// Physical memory layout abstraction.
// Platform-specific macros (UART0, PLIC, KERNBASE, etc.) are defined in
// hal/{riscv,loongarch}/memlayout.h and included through hal.h.

#ifndef _HAL_MEMLAYOUT_H_
#define _HAL_MEMLAYOUT_H_

#include "types.h"

// ---- Kernel boundaries (platform-defined) ----
// HAL_KERNBASE, HAL_PHYSTOP, HAL_UART0, HAL_VIRTIO0, HAL_PLIC,
// HAL_TRAMPOLINE, HAL_TRAPFRAME, HAL_KSTACK

extern char HAL_ETEXT[];   // end of kernel code
extern char HAL_END[];     // end of kernel image

#endif
