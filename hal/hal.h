// Hardware Abstraction Layer — unified include.
// Kernel code includes this instead of "riscv.h" and "memlayout.h".

#ifndef _HAL_H_
#define _HAL_H_

#include "types.h"

// Platform-specific definitions (CSR inlines, PTE format, memory layout).
// Each architecture provides its own arch.h and memlayout.h.
#ifdef ARCH_riscv
#include "arch.h"
#include "memlayout.h"
#elif defined(ARCH_loongarch)
#include "arch.h"
#include "memlayout.h"
#else
#error "ARCH_xxx not defined"
#endif

// Subsystem HAL interface declarations.
#include "hal_arch.h"
#include "hal_vm.h"
#include "hal_intr.h"
#include "hal_timer.h"
#include "hal_memlayout.h"
#include "hal_console.h"
#include "hal_ctx.h"

#endif
