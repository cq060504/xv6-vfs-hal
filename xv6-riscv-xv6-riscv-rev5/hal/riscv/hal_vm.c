#include "types.h"
#include "hal/hal.h"
#include "defs.h"

extern char etext[];

void
hal_vm_map_kernel(pagetable_t kpgtbl)
{
  kvmmap(kpgtbl, PGROUNDDOWN(UART0), PGROUNDDOWN(UART0),
         PGSIZE, PTE_R | PTE_W);
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
#if defined(VIRTIO1) && VIRTIO_NDISK > 1
  kvmmap(kpgtbl, VIRTIO1, VIRTIO1, PGSIZE, PTE_R | PTE_W);
#endif
#if defined(VIRTIO2) && VIRTIO_NDISK > 2
  kvmmap(kpgtbl, VIRTIO2, VIRTIO2, PGSIZE, PTE_R | PTE_W);
#endif

  kvmmap(kpgtbl, KERNBASE, KERNBASE,
         (uint64)etext - KERNBASE, PTE_R | PTE_X);
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext,
         PHYSTOP - (uint64)etext, PTE_R | PTE_W);
}

void
hal_vm_enable(pagetable_t kpgtbl)
{
  hal_tlb_flush_all();
  hal_write_satp(MAKE_SATP(kpgtbl));
  hal_tlb_flush_all();
}

int
hal_vm_map_user_trap(pagetable_t pagetable,
                     uint64 trampoline_pa,
                     uint64 trapframe_pa)
{
  if(mappages(pagetable, TRAMPOLINE, PGSIZE, trampoline_pa,
              PTE_R | PTE_X) < 0)
    return -1;

  if(mappages(pagetable, TRAPFRAME, PGSIZE, trapframe_pa,
              PTE_R | PTE_W) < 0){
    uvmunmap(pagetable, TRAMPOLINE, 1, 0);
    return -1;
  }

  return 0;
}

void
hal_vm_unmap_user_trap(pagetable_t pagetable)
{
  uvmunmap(pagetable, TRAMPOLINE, 1, 0);
  uvmunmap(pagetable, TRAPFRAME, 1, 0);
}

int
hal_vm_reserve_user_low(pagetable_t pagetable, uint64 *initial_sz)
{
  (void)pagetable;
  *initial_sz = 0;
  return 0;
}
