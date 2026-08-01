#include "types.h"
#include "hal/hal.h"
#include "defs.h"

#define USER_LOW_GUARD_PAGES 2

extern char _data_lma[];
extern char _trampoline[];
extern char _data_start[];
extern char _bss_end[];
extern char end[];
extern void tlb_refill_entry(void);

void
hal_vm_map_kernel(pagetable_t kpgtbl)
{
  kvmmap(kpgtbl, PGROUNDDOWN(UART0), PGROUNDDOWN(UART0),
         PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, EIOINTC, EIOINTC, 0x4000000, PTE_R | PTE_W);

  uint64 rodata_end = (uint64)_data_lma;
  uint64 tramp_va = (uint64)_trampoline;
  if(tramp_va > KERNBASE)
    kvmmap(kpgtbl, KERNBASE, KERNBASE,
           tramp_va - KERNBASE, PTE_R | PTE_X);
  uint64 after_tramp = tramp_va + PGSIZE;
  if(rodata_end > after_tramp)
    kvmmap(kpgtbl, after_tramp, after_tramp,
           rodata_end - after_tramp, PTE_R | PTE_X);

  uint64 ram_begin = PGROUNDDOWN((uint64)_data_start);
  uint64 ram_end = PGROUNDUP((uint64)_bss_end);
  uint64 free_begin = PGROUNDUP((uint64)end);
  kvmmap(kpgtbl, ram_begin, ram_begin,
         ram_end - ram_begin, PTE_R | PTE_W);
  if(free_begin < PHYSTOP)
    kvmmap(kpgtbl, free_begin, free_begin,
           PHYSTOP - free_begin, PTE_R | PTE_W);
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

int
hal_vm_reserve_user_low(pagetable_t pagetable, uint64 *initial_sz)
{
  uint64 mapped = 0;
  *initial_sz = 0;

  for(int i = 0; i < USER_LOW_GUARD_PAGES; i++){
    char *guard = kalloc();
    if(guard == 0)
      goto fail;
    memset(guard, 0, PGSIZE);
    if(mappages(pagetable, mapped, PGSIZE,
                (uint64)guard, PTE_R) < 0){
      kfree(guard);
      goto fail;
    }
    mapped += PGSIZE;
  }

  *initial_sz = mapped;
  return 0;

fail:
  if(mapped != 0)
    uvmunmap(pagetable, 0, mapped / PGSIZE, 1);
  return -1;
}
