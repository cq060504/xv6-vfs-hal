// CPU architecture abstraction interface.
// Platform implementations provide the actual register access.

#ifndef _HAL_ARCH_H_
#define _HAL_ARCH_H_

#include "types.h"

// ---- Core ID ----
uint64 hal_get_hartid(void);

// ---- Interrupt control ----
void hal_intr_on(void);
void hal_intr_off(void);
int  hal_intr_get(void);

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

// Make the current process trapframe discoverable by the platform trampoline.
void hal_trap_bind_user_frame(uint64 trapframe_kva);

// ---- Page table base ----
uint64 hal_read_satp(void);
void   hal_write_satp(uint64);

// ---- Stack pointer ----
uint64 hal_read_sp(void);

// ---- Return address ----
uint64 hal_read_ra(void);

// ---- CPU idle (platform-specific wait-for-interrupt) ----
void hal_cpu_idle(void);

#endif
