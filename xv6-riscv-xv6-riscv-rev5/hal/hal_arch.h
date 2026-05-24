// CPU architecture abstraction interface.
// Platform implementations provide the actual register access.

#ifndef _HAL_ARCH_H_
#define _HAL_ARCH_H_

#include "types.h"

// ---- Core ID ----
uint64 hal_get_hartid(void);

// ---- Interrupt control (wrappers around CSR) ----
// Defined as inline in arch.h; these are higher-level wrappers.
// For compatibility, kernel code may call intr_on/off/get directly.

// ---- Exception state ----
uint64 hal_read_sstatus(void);
void   hal_write_sstatus(uint64);
uint64 hal_read_sepc(void);
void   hal_write_sepc(uint64);
uint64 hal_read_scause(void);
uint64 hal_read_stval(void);

// ---- Trap vector ----
uint64 hal_read_stvec(void);
void   hal_write_stvec(uint64);

// ---- Page table base ----
uint64 hal_read_satp(void);
void   hal_write_satp(uint64);

// ---- Stack pointer ----
uint64 hal_read_sp(void);

// ---- Return address ----
uint64 hal_read_ra(void);

#endif
