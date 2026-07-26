// Virtual memory / MMU abstraction interface.

#ifndef _HAL_VM_H_
#define _HAL_VM_H_

#include "types.h"

// ---- TLB flush ----
void hal_tlb_flush_all(void);

// ---- Kernel address space ----
// Build platform mappings, then activate the completed kernel page table on
// the current hart. The common VM layer maps the trampoline and kernel stacks.
void hal_vm_map_kernel(pagetable_t kpgtbl);
void hal_vm_enable(pagetable_t kpgtbl);

// ---- User trap support ----
// Map/unmap any platform-required trampoline and trapframe aliases.
int  hal_vm_map_user_trap(pagetable_t pagetable,
                          uint64 trampoline_pa,
                          uint64 trapframe_pa);
void hal_vm_unmap_user_trap(pagetable_t pagetable);

// Reserve platform-required low user pages. On success, *initial_sz is the
// first address available for ELF segments and is owned by uvmfree().
int hal_vm_reserve_user_low(pagetable_t pagetable, uint64 *initial_sz);

// pte must be valid. Returns non-zero only for a terminal mapping.
int hal_pte_is_leaf(pte_t pte);

// ---- Page table constants (platform-defined) ----
// HAL_PGSIZE, HAL_PGSHIFT, PTE flags are provided by arch.h.

// ---- User memory layout (platform-defined) ----
// MAXVA defined in arch.h
// TRAMPOLINE, TRAPFRAME, KSTACK defined in memlayout.h

#endif
