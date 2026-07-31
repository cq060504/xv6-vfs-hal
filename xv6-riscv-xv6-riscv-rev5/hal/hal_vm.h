// Virtual memory / MMU abstraction interface.

#ifndef _HAL_VM_H_
#define _HAL_VM_H_

#include "types.h"

// ---- TLB flush ----
void hal_tlb_flush_all(void);

// --- judge whether a PTE is a leaf PTE for freeing page table pages ----
int hal_pte_is_leaf(pte_t pte);

// ---- Page table constants (platform-defined) ----
// HAL_PGSIZE, HAL_PGSHIFT, PTE flags are provided by arch.h.

// ---- User memory layout (platform-defined) ----
// MAXVA defined in arch.h
// TRAMPOLINE, TRAPFRAME, KSTACK defined in memlayout.h

#endif
