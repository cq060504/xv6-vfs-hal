// Virtual memory / MMU abstraction interface.

#ifndef _HAL_VM_H_
#define _HAL_VM_H_

#include "types.h"

// ---- TLB flush ----
void hal_tlb_flush_all(void);

// --- judge whether a PTE is a leaf PTE for freeing page table pages ----
int hal_pte_is_leaf(pte_t pte);

//--- riscv:trampoline map to user space ,loongarch uses DMW0 ---
int hal_vm_map_trampoline(pagetable_t pagetable, uint64 trampoline_pa,  uint64 trapframe_pa);

void hal_vm_unmap_trampoline(pagetable_t pagetable);

// Boot-time kernel address-space construction and per-hart activation.
void hal_vm_map_kernel(pagetable_t kpgtbl);
void hal_vm_enable(pagetable_t kpgtbl);

// ---- Page table constants (platform-defined) ----
// HAL_PGSIZE, HAL_PGSHIFT, PTE flags are provided by arch.h.

// ---- User memory layout (platform-defined) ----
// MAXVA defined in arch.h
// TRAMPOLINE, TRAPFRAME, KSTACK defined in memlayout.h

#endif
