#include "param.h"
#include "types.h"
#include "hal/hal.h"
#include "elf.h"
#include "defs.h"
#include "spinlock.h"
#include "proc.h"
#include "fs.h"

/*
 * the kernel's page table.
 */
pagetable_t kernel_pagetable;

extern char etext[];  // kernel.ld sets this to end of kernel code.

extern char trampoline[]; // trampoline.S

// Make a direct-map page table for the kernel.
pagetable_t
kvmmake(void)
{
  pagetable_t kpgtbl;

  kpgtbl = (pagetable_t) kalloc();
  memset(kpgtbl, 0, PGSIZE);

  // uart registers
  kvmmap(kpgtbl, PGROUNDDOWN(UART0), PGROUNDDOWN(UART0), PGSIZE, PTE_R | PTE_W);

  // PLIC/EIOINTC interrupt controller region
  kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);

#ifdef ARCH_loongarch
  // VIRTIO0 is within PLIC range on LoongArch (0x10000000 in 0x0FE00000-0x13E00000)
  // Skip separate VIRTIO0 mapping - it's already mapped by PLIC.
  // VIRTIO0 is PCI-based on LoongArch, accessed via PCI config space, not direct MMIO.

  // Map kernel code from flash, split around the TRAMPOLINE page.
  // TRAMPOLINE (0x1C008000) sits inside the read-only sections.
  // Map code before and after it; the trampoline page itself is
  // DMW0-accessible for kernel and user-page-table-mapped for user.
  extern char _data_lma[], _trampoline[];
  uint64 rodata_end = (uint64)_data_lma;
  uint64 tramp_va   = (uint64)_trampoline;
  if (tramp_va > KERNBASE)
    kvmmap(kpgtbl, KERNBASE, KERNBASE, tramp_va - KERNBASE, PTE_R | PTE_X);
  uint64 after_tramp = tramp_va + PGSIZE;
  if (rodata_end > after_tramp)
    kvmmap(kpgtbl, after_tramp, after_tramp, rodata_end - after_tramp, PTE_R | PTE_X);
#else
  // virtio mmio disk interface (RISC-V only)
  kvmmap(kpgtbl, VIRTIO0, VIRTIO0, PGSIZE, PTE_R | PTE_W);
#if defined(VIRTIO1) && VIRTIO_NDISK > 1
  kvmmap(kpgtbl, VIRTIO1, VIRTIO1, PGSIZE, PTE_R | PTE_W);
#endif
#if defined(VIRTIO2) && VIRTIO_NDISK > 2
  kvmmap(kpgtbl, VIRTIO2, VIRTIO2, PGSIZE, PTE_R | PTE_W);
#endif

  // map kernel text executable and read-only.
  kvmmap(kpgtbl, KERNBASE, KERNBASE, (uint64)etext-KERNBASE, PTE_R | PTE_X);
#endif

#ifdef ARCH_loongarch
  // LoongArch: kernel .data/.bss are in RAM at 0x00400000, not after etext in flash.
  // Map the RAM data+bss region, then free RAM for kalloc.
  extern char _data_start[], _bss_end[], end[];
  uint64 ram_begin = PGROUNDDOWN((uint64)_data_start);
  uint64 ram_end   = PGROUNDUP((uint64)_bss_end);
  uint64 free_begin = PGROUNDUP((uint64)end);
  kvmmap(kpgtbl, ram_begin, ram_begin, ram_end - ram_begin, PTE_R | PTE_W);
  if(free_begin < PHYSTOP)
    kvmmap(kpgtbl, free_begin, free_begin, PHYSTOP - free_begin, PTE_R | PTE_W);
#else
  // map kernel data and the physical RAM we'll make use of.
  kvmmap(kpgtbl, (uint64)etext, (uint64)etext, PHYSTOP-(uint64)etext, PTE_R | PTE_W);
#endif

  // map the trampoline for trap entry/exit to
  // the highest virtual address in the kernel.
  kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);

  // allocate and map a kernel stack for each process.
  proc_mapstacks(kpgtbl);
  
  return kpgtbl;
}

// add a mapping to the kernel page table.
// only used when booting.
// does not flush TLB or enable paging.
void
kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm)
{
  if(mappages(kpgtbl, va, sz, pa, perm) != 0)
    panic("kvmmap");
}

// Initialize the kernel_pagetable, shared by all CPUs.
void
kvminit(void)
{
  kernel_pagetable = kvmmake();
}

// Switch the current CPU's h/w page table register to
// the kernel's page table, and enable paging.
void
kvminithart()
{
  // wait for any previous writes to the page table memory to finish.
  sfence_vma();

  w_satp(MAKE_SATP(kernel_pagetable));

  // flush stale entries from the TLB.
  sfence_vma();

#ifdef ARCH_loongarch
  // On LoongArch, enable the MMU with DMW for kernel direct mapping.
  //
  // DMW0 maps VA[47:36]=0 → PA[47:36]=0 (identity mapping, low 64GB).
  // This covers: RAM, EIOINTC, flash, UART, TRAMPOLINE, kernel stacks —
  // all kernel addresses. No TLB needed for kernel access.
  //
  // DMW format: [3:0]=PLV, [5:4]=MAT, [27:12]=VSEG, [47:36]=PSEG
  // DMW0: PLV0-only identity map for VA[63:60]=0 → PA=VA.
  // Covers all kernel code/data/flash/UART/EIOINTC (all < 2^60).
  // KSTACK is placed at low physical addresses with actual RAM.
  // TRAMPOLINE is placed at its flash LMA so DMW0 reaches the real code.
  // TRAPFRAME accessed via KSave1 (kernel VA), not TRAPFRAME VA.
  w_dmw0(0x0000000000000011ULL);  // PLV0, MAT=01(CC), VSEG=0
  w_dmw1(0);

  // Flush all TLB entries before enabling MMU
  sfence_vma();

  // Configure 4-level page table walk (must match walk() and TLB handler):
  //   Level 3 (PGD): VA[47:39], base=39, width=9
  //   Level 2 (PUD): VA[38:30], base=30, width=9
  //   Level 1 (PMD): VA[29:21], base=21, width=9
  //   Level 0 (PTE): VA[20:12], base=12, width=9, 64-bit entries
  asm volatile("csrwr %0, 0x1C" : : "r"(
    (12UL << 0)  | (9UL << 5)  | (21UL << 10) |
    (9UL << 15)  | (30UL << 20) | (9UL << 25)
  ));
  asm volatile("csrwr %0, 0x1D" : : "r"(
    (39UL << 0)  | (9UL << 6)
  ));

  // QEMU la464 reports VALEN=48. RVACFG can reduce mapped-mode VA
  // validity by at most 8 bits, so this makes >=1<<40 trap as ADE.
  // xv6's stricter MAXVA=1<<38 is enforced in walk/copy/vmfault and
  // in the software TLB refill handler below.
  w_rvacfg(8);

  // Set TLB refill entry point for user-space TLB misses
  extern void tlb_refill_entry(void);
  uint64 tlbr_entry = (uint64)tlb_refill_entry;
  asm volatile("csrwr %0, 0x88" : "+r"(tlbr_entry));

  // Enable MMU: clear DA (direct address), set PG (page table).
  // DA must be 0 for PG to take effect. Write atomically.
  uint64 crmd = r_crmd();
  crmd &= ~CRMD_DA;     // disable direct address translation
  crmd |= CRMD_PG;      // enable page table walk
  w_crmd(crmd);

  // ldpte on QEMU la464 clears P(b7)/W(b8) when writing TLBRELO,
  // so no manual bit-clearing is needed in the TLB refill handler.
  // Verified by removing the bstrins.d clearing and passing usertests.
#endif
}

// Return the address of the PTE in page table pagetable
// that corresponds to virtual address va.  If alloc!=0,
// create any required page-table pages.
//
// LoongArch uses four levels of page-table pages in this port
// (RISC-V still uses three via its own MAXVA/PX definitions).
// pages. A page-table page contains 512 64-bit PTEs.
// A 64-bit virtual address is split into five fields:
//   48..63 -- must be zero.
//   39..47 -- 9 bits of level-3 index.
//   30..38 -- 9 bits of level-2 index.
//   21..29 -- 9 bits of level-1 index.
//   12..20 -- 9 bits of level-0 index.
//    0..11 -- 12 bits of byte offset within the page.
pte_t *
walk(pagetable_t pagetable, uint64 va, int alloc)
{
  if(va >= MAXVA)
    panic("walk");

  for(int level = PT_LEVELS - 1; level > 0; level--) {
    pte_t *pte = &pagetable[PX(level, va)];
    if(*pte & PTE_V) {
      pagetable = (pagetable_t)PTE2PA(*pte);
    } else {
      if(!alloc || (pagetable = (pde_t*)kalloc()) == 0)
        return 0;
      memset(pagetable, 0, PGSIZE);
      *pte = PA2PTE(pagetable) | PTE_V;
    }
  }
  return &pagetable[PX(0, va)];
}

// Look up a virtual address, return the physical address,
// or 0 if not mapped.
// Can only be used to look up user pages.
uint64
walkaddr(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  uint64 pa;

  if(va >= MAXVA)
    return 0;

  pte = walk(pagetable, va, 0);
  if(pte == 0)
    return 0;
  if((*pte & PTE_V) == 0)
    return 0;
  if((*pte & PTE_U) == 0)
    return 0;
  pa = PTE2PA(*pte);
  return pa;
}

// Create PTEs for virtual addresses starting at va that refer to
// physical addresses starting at pa.
// va and size MUST be page-aligned.
// Returns 0 on success, -1 if walk() couldn't
// allocate a needed page-table page.
int
mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm)
{
  uint64 a, last;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("mappages: va not aligned");

  if((size % PGSIZE) != 0)
    panic("mappages: size not aligned");

  if(size == 0)
    panic("mappages: size");

  a = va;
  last = va + size - PGSIZE;
  for(;;){
    if((pte = walk(pagetable, a, 1)) == 0)
      return -1;
    if(*pte & PTE_V)
      panic("mappages: remap");
    *pte = PA2PTE(pa) | perm | PTE_V_CACHE;
    if(a == last)
      break;
    a += PGSIZE;
    pa += PGSIZE;
  }
  return 0;
}

// create an empty user page table.
// returns 0 if out of memory.
pagetable_t
uvmcreate()
{
  pagetable_t pagetable;
  pagetable = (pagetable_t) kalloc();
  if(pagetable == 0)
    return 0;
  memset(pagetable, 0, PGSIZE);
  return pagetable;
}

// Remove npages of mappings starting from va. va must be
// page-aligned. It's OK if the mappings don't exist.
// Optionally free the physical memory.
void
uvmunmap(pagetable_t pagetable, uint64 va, uint64 npages, int do_free)
{
  uint64 a;
  pte_t *pte;

  if((va % PGSIZE) != 0)
    panic("uvmunmap: not aligned");

  for(a = va; a < va + npages*PGSIZE; a += PGSIZE){
    if((pte = walk(pagetable, a, 0)) == 0) // leaf page table entry allocated?
      continue;   
    if((*pte & PTE_V) == 0)  // has physical page been allocated?
      continue;
    if(do_free){
      uint64 pa = PTE2PA(*pte);
      kfree((void*)pa);
    }
    *pte = 0;
  }
}

// Allocate PTEs and physical memory to grow a process from oldsz to
// newsz, which need not be page aligned.  Returns new size or 0 on error.
uint64
uvmalloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz, int xperm)
{
  char *mem;
  uint64 a;

  if(newsz < oldsz)
    return oldsz;

  oldsz = PGROUNDUP(oldsz);
  for(a = oldsz; a < newsz; a += PGSIZE){
    mem = kalloc();
    if(mem == 0){
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
    memset(mem, 0, PGSIZE);
    if(mappages(pagetable, a, PGSIZE, (uint64)mem, PTE_R|PTE_U|xperm) != 0){
      kfree(mem);
      uvmdealloc(pagetable, a, oldsz);
      return 0;
    }
  }
  return newsz;
}

// Deallocate user pages to bring the process size from oldsz to
// newsz.  oldsz and newsz need not be page-aligned, nor does newsz
// need to be less than oldsz.  oldsz can be larger than the actual
// process size.  Returns the new process size.
uint64
uvmdealloc(pagetable_t pagetable, uint64 oldsz, uint64 newsz)
{
  if(newsz >= oldsz)
    return oldsz;

  if(PGROUNDUP(newsz) < PGROUNDUP(oldsz)){
    int npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);
  }

  return newsz;
}

// Recursively free page-table pages.
// All leaf mappings must already have been removed.
void
freewalk(pagetable_t pagetable)
{
  // there are 2^9 = 512 PTEs in a page table.
  for(int i = 0; i < 512; i++){
    pte_t pte = pagetable[i];
    if(pte & PTE_V) {
#ifdef ARCH_loongarch
      // LoongArch: non-leaf PTEs have PTE_V but no MAT bit.
      // Leaf PTEs always have PTE_V_CACHE (V+MAT) set.
      // This distinguishes them: non-leaf has V=1,MAT=0; leaf has V=1,MAT=1.
      if(pte & PTE_MAT) {
        panic("freewalk: leaf");
      }
#else
      // RISC-V: non-leaf PTEs have V=1 but no R/W/X flags.
      if((pte & (PTE_R|PTE_W|PTE_X)) != 0){
        panic("freewalk: leaf");
      }
#endif
      uint64 child = PTE2PA(pte);
      freewalk((pagetable_t)child);
      pagetable[i] = 0;
    }
  }
  kfree((void*)pagetable);
}

// Free user memory pages,
// then free page-table pages.
void
uvmfree(pagetable_t pagetable, uint64 sz)
{
  if(sz > 0)
    uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1);
  freewalk(pagetable);
}

// Given a parent process's page table, copy
// its memory into a child's page table.
// Copies both the page table and the
// physical memory.
// returns 0 on success, -1 on failure.
// frees any allocated pages on failure.
int
uvmcopy(pagetable_t old, pagetable_t new, uint64 sz)
{
  pte_t *pte;
  uint64 pa, i;
  uint flags;
  char *mem;

  for(i = 0; i < sz; i += PGSIZE){
    if((pte = walk(old, i, 0)) == 0)
      continue;   // page table entry hasn't been allocated
    if((*pte & PTE_V) == 0)
      continue;   // physical page hasn't been allocated
    pa = PTE2PA(*pte);
    flags = PTE_FLAGS(*pte);
    if((mem = kalloc()) == 0)
      goto err;
    memmove(mem, (char*)pa, PGSIZE);
    if(mappages(new, i, PGSIZE, (uint64)mem, flags) != 0){
      kfree(mem);
      goto err;
    }
  }
  return 0;

 err:
  uvmunmap(new, 0, i / PGSIZE, 1);
  return -1;
}

// mark a PTE invalid for user access.
// used by exec for the user stack guard page.
void
uvmclear(pagetable_t pagetable, uint64 va)
{
  pte_t *pte;
  
  pte = walk(pagetable, va, 0);
  if(pte == 0)
    panic("uvmclear");
  *pte &= ~PTE_U;
}

// Copy from kernel to user.
// Copy len bytes from src to virtual address dstva in a given page table.
// Return 0 on success, -1 on error.
int
copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len)
{
  uint64 n, va0, pa0;
  pte_t *pte;

  while(len > 0){
    va0 = PGROUNDDOWN(dstva);
    if(va0 >= MAXVA)
      return -1;
  
    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }

    pte = walk(pagetable, va0, 0);
    // forbid copyout over read-only user text pages.
    if((*pte & PTE_W) == 0)
      return -1;
      
    n = PGSIZE - (dstva - va0);
    if(n > len)
      n = len;
    memmove((void *)(pa0 + (dstva - va0)), src, n);

    len -= n;
    src += n;
    dstva = va0 + PGSIZE;
  }
  return 0;
}

// Copy from user to kernel.
// Copy len bytes to dst from virtual address srcva in a given page table.
// Return 0 on success, -1 on error.
int
copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len)
{
  uint64 n, va0, pa0;

  while(len > 0){
    va0 = PGROUNDDOWN(srcva);
    if(va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0) {
      if((pa0 = vmfault(pagetable, va0, 0)) == 0) {
        return -1;
      }
    }
    n = PGSIZE - (srcva - va0);
    if(n > len)
      n = len;
    memmove(dst, (void *)(pa0 + (srcva - va0)), n);

    len -= n;
    dst += n;
    srcva = va0 + PGSIZE;
  }
  return 0;
}

// Copy a null-terminated string from user to kernel.
// Copy bytes to dst from virtual address srcva in a given page table,
// until a '\0', or max.
// Return 0 on success, -1 on error.
int
copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max)
{
  uint64 n, va0, pa0;
  int got_null = 0;

  while(got_null == 0 && max > 0){
    va0 = PGROUNDDOWN(srcva);
    if(va0 >= MAXVA)
      return -1;

    pa0 = walkaddr(pagetable, va0);
    if(pa0 == 0)
      return -1;
    n = PGSIZE - (srcva - va0);
    if(n > max)
      n = max;

    char *p = (char *) (pa0 + (srcva - va0));
    while(n > 0){
      if(*p == '\0'){
        *dst = '\0';
        got_null = 1;
        break;
      } else {
        *dst = *p;
      }
      --n;
      --max;
      p++;
      dst++;
    }

    srcva = va0 + PGSIZE;
  }
  if(got_null){
    return 0;
  } else {
    return -1;
  }
}

// Fill one TLB entry for VA from a PTE, bypassing lddir/ldpte.
// Masks P(b7) and W(b8) from memory PTE → TLBELO format.
// Uses tlbfill (random slot), works in non-TLBR context.
static void
tlb_fill_from_pte(uint64 va_page, pte_t pte_val)
{
#ifdef ARCH_loongarch
  // Clear P(b7) and W(b8) — reserved in TLBELO
  uint64 hi, lo;
  lo = pte_val;
  asm volatile("srli.d %0, %1, 9" : "=r"(hi) : "r"(lo));
  asm volatile("slli.d %0, %0, 9" : "+r"(hi));
  lo &= 0x7F;                       // keep V,D,PLV,MAT,G
  lo |= hi;                         // combine PPN + flags

  asm volatile("csrwr %0, 0x12" : : "r"(lo));           // TLBELO0
  asm volatile("csrwr %0, 0x13" : : "r"(lo));           // TLBELO1
  asm volatile("csrwr %0, 0x11" : : "r"(va_page));      // TLBEHI
  // TLBIDX.NE=0 → tlbfill writes a valid entry
  asm volatile("csrwr %0, 0x10" : : "r"(0x0C000000));   // PS=12,NE=0
  asm volatile("tlbfill");
#else
  (void)va_page;
  (void)pte_val;
#endif
}

// allocate and map user memory if process is referencing a page
// that was lazily allocated in sys_sbrk().
// returns 0 if va is invalid, non-zero if handled.
uint64
vmfault(pagetable_t pagetable, uint64 va, int read)
{
  uint64 mem;
  struct proc *p = myproc();

  if (va >= MAXVA)
    return 0;

  if (va < 2*PGSIZE)
    return 0;

  if (va >= p->sz)
    return 0;
  va = PGROUNDDOWN(va);

  // Check if page already exists in page table.
  pte_t *pte = walk(pagetable, va, 0);
  if (pte && (*pte & PTE_V)) {
    if((*pte & PTE_U) == 0)
      return 0;
    if(read == 0 && (*pte & PTE_W) == 0)
      return 0;

    // Page exists but TLB entry is stale/invalid.
    // Re-fill TLB directly, bypassing the lddir/ldpte handler.
    tlb_fill_from_pte(va, *pte);
    return 1;  // handled
  }

  // Lazy allocation for genuinely unmapped page.
  mem = (uint64) kalloc();
  if(mem == 0)
    return 0;
  memset((void *) mem, 0, PGSIZE);
  if (mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R) != 0) {
    kfree((void *)mem);
    return 0;
  }
  tlb_fill_from_pte(va, PA2PTE(mem) | PTE_W | PTE_U | PTE_R | PTE_V_CACHE);
  return mem;
}

int
ismapped(pagetable_t pagetable, uint64 va)
{
  pte_t *pte = walk(pagetable, va, 0);
  if (pte == 0) {
    return 0;
  }
  if (*pte & PTE_V){
    return 1;
  }
  return 0;
}
