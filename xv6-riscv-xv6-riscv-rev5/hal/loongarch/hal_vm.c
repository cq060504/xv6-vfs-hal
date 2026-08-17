#include "types.h"
#include "hal/hal.h"

_Static_assert(KSTACK_TOP % PGSIZE == 0, "KSTACK_TOP must be page aligned");
_Static_assert(KSTACK_TOP - KSTACK_REGION_BOTTOM == NPROC * 3 * PGSIZE,
               "kernel stack window must contain every process stack and guard");

extern void tlb_refill_entry(void);

// DMW0 (PLV0, VSEG=0) identity-maps all low physical addresses for kernel
// mode, so the kernel never walks PGDL/PGDH for low-address accesses.
// High-address kernel stacks are mapped separately via PGDH and
// proc_mapstacks().  Therefore hal_vm_map_kernel() is intentionally a
// no-op — building dead page-table entries would waste memory (~40 pages)
// and imply a page-table dependency that does not exist.
void
hal_vm_map_kernel(pagetable_t kpgtbl)
{
  (void)kpgtbl;
}

void
hal_vm_enable(pagetable_t kpgtbl)
{
  hal_tlb_flush_all();
  hal_write_satp(MAKE_SATP(kpgtbl));
  w_pgdh(MAKE_SATP(kpgtbl));
  hal_tlb_flush_all();

  // PLV0, cacheable, VSEG=0. High kernel stacks still use PGDH.
  w_dmw0(0x0000000000000011ULL);
  w_dmw1(0);
  hal_tlb_flush_all();

  // Four 9-bit levels at VA[47:39], [38:30], [29:21], and [20:12].
  w_pwcl((12UL << 0) | (9UL << 5) | (21UL << 10) |
         (9UL << 15) | (30UL << 20) | (9UL << 25));
  w_pwch((39UL << 0) | (9UL << 6));
  w_rvacfg(8);
  w_tlbrentry((uint64)tlb_refill_entry);

  uint64 crmd = r_crmd();
  crmd &= ~CRMD_DA;
  crmd |= CRMD_PG;
  w_crmd(crmd);
}

int
hal_vm_map_trampoline(pagetable_t pagetable,
                      uint64 trampoline_pa,
                      uint64 trapframe_pa)
{
  (void)pagetable;
  (void)trampoline_pa;
  (void)trapframe_pa;
  return 0;
}

void
hal_vm_unmap_trampoline(pagetable_t pagetable)
{
  (void)pagetable;
}
