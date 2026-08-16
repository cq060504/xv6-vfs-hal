// Hardware Abstraction Layer — unified include.
// Kernel code includes this instead of "riscv.h" and "memlayout.h".

#ifndef _HAL_H_
#define _HAL_H_

#include "types.h"

// Platform-specific definitions (CSR inlines, PTE format, memory layout).
#include "arch.h"
#include "memlayout.h"

// Subsystem HAL interface declarations.
#include "hal_arch.h"
#include "hal_vm.h"
#include "hal_intr.h"
#include "hal_timer.h"
#include "hal_console.h"
#include "hal_disk.h"
#include "hal_ctx.h"

#endif
