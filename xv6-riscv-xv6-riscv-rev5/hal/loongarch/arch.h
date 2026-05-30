// LoongArch64 CPU architecture definitions.
// CSR read/write inline functions, PTE format, page macros.
//
// Key differences from RISC-V:
//  - CSR instructions: csrrd (read), csrwr (write), csrxchg (exchange)
//  - PTE PPN is split across two bit ranges (PPN[47:0]→PTE[59:12], PPN[52:48]→PTE[11:7])
//  - Software TLB refill via TLBRENTRY (not hardware page walk)
//  - Only 2 privilege levels: PLV0 (kernel) and PLV3 (user)
//  - Exception return: ertn (not sret/mret)

#ifndef __ASSEMBLER__

// ============================================================
//  CSR register map
// ============================================================
#define CSR_CRMD    0x0    // Current Mode: PLV[1:0], IE[2], DA[3], PG[4]
#define CSR_PRMD    0x1    // Previous Mode: PPLV[1:0], PIE[2]
#define CSR_ECFG    0x4    // Exception Config: LIE[12:0], VS[18:16]
#define CSR_ESTAT   0x5    // Exception Status: IS[12:0], Ecode[21:16]
#define CSR_ERA     0x6    // Exception Return Address (= sepc)
#define CSR_BADV    0x7    // Bad Virtual Address (= stval)
#define CSR_EENTRY  0xC    // Exception Entry Point (= stvec)
#define CSR_TLBIDX  0x10   // TLB Index
#define CSR_TLBEHI  0x11   // TLB Entry High
#define CSR_TLBELO0 0x12   // TLB Entry Low 0
#define CSR_TLBELO1 0x13   // TLB Entry Low 1
#define CSR_PGDL    0x19   // Page Table Base (low half addr space)
#define CSR_PGDH    0x1A   // Page Table Base (high half addr space)
#define CSR_CPUID   0x20   // Core ID (= mhartid)
#define CSR_TCFG    0x41   // Timer Config: enable + period
#define CSR_TVAL    0x42   // Timer Value: countdown
#define CSR_TICLR   0x44   // Timer Interrupt Clear
#define CSR_LLBCTL  0x60   // Load-Link / Store-Conditional ctl
#define CSR_DMW0    0x180  // Direct Mapping Window 0
#define CSR_DMW1    0x181  // Direct Mapping Window 1

// ============================================================
//  CRMD bit definitions
// ============================================================
#define CRMD_PLV    0x3       // PLV mask (bits 1:0)
#define CRMD_IE     (1L << 2) // global interrupt enable
#define CRMD_DA     (1L << 3) // direct address translation enable
#define CRMD_PG     (1L << 4) // page table mapping enable

// ============================================================
//  PRMD bit definitions
// ============================================================
#define PRMD_PPLV   0x3       // previous PLV mask (bits 1:0)
#define PRMD_PIE    (1L << 2) // previous interrupt enable
#define PRMD_USER   (3L)      // PPLV=3 → returning to user mode

// ============================================================
//  ECFG bit definitions
// ============================================================
#define ECFG_LIE_TIMER (1L << 11)  // local timer interrupt enable
#define ECFG_LIE_HWI   (1L << 12)  // local hardware interrupt enable (EIOINTC)

// ============================================================
//  ESTAT bit definitions
// ============================================================
#define ESTAT_IS_TIMER  (1L << 11)  // timer interrupt pending
#define ESTAT_IS_HWI    (1L << 12)  // external hardware interrupt pending
#define ESTAT_ECODE_SHIFT 16
#define ESTAT_ECODE_MASK  (0x3FULL << ESTAT_ECODE_SHIFT)

// ============================================================
//  LoongArch exception codes (ESTAT.Ecode)
// ============================================================
#define EXCCODE_INT    0    // interrupt (IS bits indicate source)
#define EXCCODE_PIL    1    // page invalid on load
#define EXCCODE_PIS    2    // page invalid on store
#define EXCCODE_PIF    3    // page invalid on instruction fetch
#define EXCCODE_PME    4    // page modification
#define EXCCODE_PNR    5    // page not readable
#define EXCCODE_PNX    6    // page not executable
#define EXCCODE_PPI    7    // page privilege violation
#define EXCCODE_SYS    11   // system call
#define EXCCODE_BP     12   // breakpoint
#define EXCCODE_INE    13   // instruction not exist
#define EXCCODE_IPE    14   // instruction privilege error
#define EXCCODE_FPDIS  15   // FPU disabled
#define EXCCODE_FPE    18   // floating point exception
// Backward-compat aliases
#define EXCCODE_TLBL EXCCODE_PIL
#define EXCCODE_TLBS EXCCODE_PIS
#define EXCCODE_TLBI EXCCODE_PIF
#define EXCCODE_TLBM EXCCODE_PME

// ============================================================
//  TCFG bit definitions
// ============================================================
#define TCFG_EN        (1L << 0)   // timer enable
#define TCFG_PERIOD    (1L << 1)   // periodic mode
#define TCFG_INITVAL_SHIFT 2       // init value at bits [31:2]

// ============================================================
//  CSR access inline functions (csrrd / csrwr)
// ============================================================

// --- CRMD (0x0): Current Mode Register ---
static inline uint64 r_crmd(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x0" : "=r"(x));
  return x;
}
static inline void w_crmd(uint64 x) {
  asm volatile("csrwr %0, 0x0" : : "r"(x));
}

// --- PRMD (0x1): Previous Mode Register ---
static inline uint64 r_prmd(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x1" : "=r"(x));
  return x;
}
static inline void w_prmd(uint64 x) {
  asm volatile("csrwr %0, 0x1" : : "r"(x));
}

// --- ECFG (0x4): Exception Configuration ---
static inline uint64 r_ecfg(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x4" : "=r"(x));
  return x;
}
static inline void w_ecfg(uint64 x) {
  asm volatile("csrwr %0, 0x4" : : "r"(x));
}

// --- ESTAT (0x5): Exception Status ---
static inline uint64 r_estat(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x5" : "=r"(x));
  return x;
}

// --- ERA (0x6): Exception Return Address (= sepc) ---
static inline uint64 r_era(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x6" : "=r"(x));
  return x;
}
static inline void w_era(uint64 x) {
  asm volatile("csrwr %0, 0x6" : : "r"(x));
}

// --- BADV (0x7): Bad Virtual Address (= stval) ---
static inline uint64 r_badv(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x7" : "=r"(x));
  return x;
}

// --- EENTRY (0xC): Exception Entry Point (= stvec) ---
static inline uint64 r_eentry(void) {
  uint64 x;
  asm volatile("csrrd %0, 0xC" : "=r"(x));
  return x;
}
static inline void w_eentry(uint64 x) {
  asm volatile("csrwr %0, 0xC" : : "r"(x));
}

// --- PGDL (0x19): Page Table Base (low half) ---
static inline uint64 r_pgdl(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x19" : "=r"(x));
  return x;
}
static inline void w_pgdl(uint64 x) {
  asm volatile("csrwr %0, 0x19" : : "r"(x));
}

// --- CPUID (0x20): Core ID ---
static inline uint64 r_cpuid(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x20" : "=r"(x));
  return x;
}

// --- TCFG (0x41): Timer Configuration ---
static inline uint64 r_tcfg(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x41" : "=r"(x));
  return x;
}
static inline void w_tcfg(uint64 x) {
  asm volatile("csrwr %0, 0x41" : : "r"(x));
}

// --- TVAL (0x42): Timer Value (countdown) ---
static inline uint64 r_tval(void) {
  uint64 x;
  asm volatile("csrrd %0, 0x42" : "=r"(x));
  return x;
}
static inline void w_tval(uint64 x) {
  asm volatile("csrwr %0, 0x42" : : "r"(x));
}

// --- TICLR (0x44): Timer Interrupt Clear ---
static inline void w_ticlr(uint64 x) {
  asm volatile("csrwr %0, 0x44" : : "r"(x));
}

// --- DMW0 (0x180): Direct Mapping Window 0 ---
static inline void w_dmw0(uint64 x) {
  asm volatile("csrwr %0, 0x180" : : "r"(x));
}

// --- DMW1 (0x181): Direct Mapping Window 1 ---
static inline void w_dmw1(uint64 x) {
  asm volatile("csrwr %0, 0x181" : : "r"(x));
}

// ============================================================
//  Interrupt control (via CRMD.IE bit)
// ============================================================
static inline void intr_on(void) {
  uint64 x = r_crmd();
  w_crmd(x | CRMD_IE);
}

static inline void intr_off(void) {
  uint64 x = r_crmd();
  w_crmd(x & ~CRMD_IE);
}

static inline int intr_get(void) {
  return (r_crmd() & CRMD_IE) != 0;
}

// ============================================================
//  TLB flush (invtlb)
// ============================================================
static inline void sfence_vma(void) {
  // invtlb op, rj, rk:  op=0 flush all, rj=rk=$r0 for all ASIDs
  asm volatile("invtlb 0, $r0, $r0");
}

// ============================================================
//  General-purpose register access
// ============================================================
static inline uint64 r_tp(void) {
  uint64 x;
  asm volatile("or %0, $r2, $r0" : "=r"(x));
  return x;
}

static inline void w_tp(uint64 x) {
  asm volatile("or $r2, %0, $r0" : : "r"(x));
}

static inline uint64 r_sp(void) {
  uint64 x;
  asm volatile("or %0, $r3, $r0" : "=r"(x));
  return x;
}

static inline uint64 r_ra(void) {
  uint64 x;
  asm volatile("or %0, $r1, $r0" : "=r"(x));
  return x;
}

// ============================================================
//  ESTAT → RISC-V scause conversion (for trap.c compatibility)
// ============================================================
static inline uint64 estat_to_scause(uint64 estat) {
  uint64 ecode = (estat >> ESTAT_ECODE_SHIFT) & 0x3F;
  uint64 is_bits = estat & 0x1FFF;

  // Interrupts: Ecode=0, IS bits indicate source
  if (ecode == EXCCODE_INT) {
    if (is_bits & ESTAT_IS_TIMER)
      return 0x8000000000000005ULL;  // RISC-V supervisor timer interrupt
    if (is_bits & ESTAT_IS_HWI)
      return 0x8000000000000009ULL;  // RISC-V supervisor external interrupt
    return 0x8000000000000000ULL | ecode;
  }

  // Exceptions — map LoongArch Ecode → RISC-V scause for trap.c compatibility
  switch (ecode) {
    case EXCCODE_SYS:   return 8;    // syscall
    case EXCCODE_PIL:   return 13;   // load page fault
    case EXCCODE_PIS:   return 15;   // store page fault
    case EXCCODE_PIF:   return 12;   // instruction page fault
    case EXCCODE_PME:   return 15;   // page modification → store page fault
    case EXCCODE_PNR:   return 13;   // page not readable → load page fault
    case EXCCODE_PNX:   return 12;   // page not executable → instr page fault
    case EXCCODE_PPI:   return 13;   // page privilege → load page fault
    case EXCCODE_INE:   return 2;    // illegal instruction
    case EXCCODE_IPE:   return 2;    // instruction privilege → illegal instr
    case EXCCODE_BP:    return 3;    // breakpoint
    case EXCCODE_FPDIS: return ecode; // FPU disabled — pass through
    case EXCCODE_FPE:   return ecode; // FP exception — pass through
    default:            return ecode;
  }
}

// ============================================================
//  PTE format and page table macros
// ============================================================
typedef uint64 pte_t;
typedef uint64 *pagetable_t;

// LoongArch PTE layout (4KB page):
//   [63]   [62]     [61]   [60]  [59:12]          [11:7]    [6] [5:4] [3:2] [1] [0]
//   NX_H  RPLV_H  RPLV    NX    PPN[47:0]        PPN_HI    G   MAT   PLV   D   V
//
// PPN is split: PPN[47:0]→PTE[59:12], PPN[52:48]→PTE[11:7]
#define PTE_V     (1L << 0)              // valid
#define PTE_D     (1L << 1)              // dirty (writable)
#define PTE_PLV0  (0L << 2)             // kernel-only accessible
#define PTE_PLV3  (3L << 2)             // user-mode accessible
#define PTE_MAT   (1L << 4)             // MAT[0]=1: cacheable coherent
#define PTE_G     (1L << 6)             // global
#define PTE_NX    (1L << 60)            // no-execute
#define PTE_RPLV  (1L << 61)            // restrict lower-PLV read

// Map to RISC-V-compatible flag names used by kernel/vm.c.
// LoongArch semantics: default is readable+executable (NX=0, RPLV=0).
// D=1 makes the page writable, PLV[3:2]=11 allows user access.
#define PTE_R  (0)          // readable (default when RPLV=0)
#define PTE_W  PTE_D        // writable (D bit)
#define PTE_X  (0)          // executable (default when NX=0)
#define PTE_U  PTE_PLV3     // user accessible (PLV=3)

// Combined flag for valid+cacheable — used when creating PTEs.
// All leaf PTEs need MAT=01 (coherent) for proper memory access.
#define PTE_V_CACHE  (PTE_V | PTE_MAT)

// Physical address ↔ PTE conversion.
// LoongArch PPN is split: PPN[47:0] at PTE[59:12], PPN[52:48] at PTE[11:7]
// PPN = PA[55:12].  PPN[47:0] = PA[59:12],  PPN[52:48] = PA[55:48].
// The (uint64) cast inside each macro handles pointer types (vm.c passes pagetable_t).
#define PA2PTE(pa)  (((uint64)(pa) & 0x0FFFFFFFFFFF000ULL) | \
                     ((((uint64)(pa) >> 48) & 0x1FUL) << 7))
#define PTE2PA(pte) (((uint64)(pte) & 0x0FFFFFFFFFFF000ULL) | \
                     ((((uint64)(pte) >> 7) & 0x1FUL) << 48))

#define PTE_FLAGS(pte) ((pte) & 0xFFFUL)

// ============================================================
//  Page table walk macros (same algorithm as RISC-V Sv39)
// ============================================================
#define PGSIZE 4096
#define PGSHIFT 12

#define PGROUNDUP(sz)  (((sz)+PGSIZE-1) & ~(PGSIZE-1))
#define PGROUNDDOWN(a) (((a)) & ~(PGSIZE-1))

// 3-level page table: 9-9-9-12 addressing (same as Sv39)
#define PXMASK          0x1FF
#define PXSHIFT(level)  (PGSHIFT+(9*(level)))
#define PX(level, va)   ((((uint64)(va)) >> PXSHIFT(level)) & PXMASK)

// MAXVA: for 3-level page table, 2^39 / 2 = 2^38 - 1 (sign extension)
#define MAXVA (1ULL << (9 + 9 + 9 + 12 - 1))

// PGDL holds the physical page table base (no mode bits like satp)
#define MAKE_SATP(pagetable) ((uint64)(pagetable))

// ============================================================
//  RISC-V compatibility aliases (for kernel code not yet using hal_ prefix)
// ============================================================
#define w_satp(x)    w_pgdl(x)     // RISC-V satp → LoongArch PGDL
#define r_satp()     r_pgdl()
#define w_stvec(x)   w_eentry(x)   // RISC-V stvec → LoongArch EENTRY
#define r_stvec()    r_eentry()
// Week 6: Convert PRMD to RISC-V sstatus format for trap.c compatibility.
// RISC-V SSTATUS_SPP[8]=1 means "from kernel", SPP=0 means "from user".
// LoongArch PRMD.PPLV=3 means "from user", PPLV=0 means "from kernel".
// This inline converts PRMD bits to RISC-V sstatus bits.
static inline uint64 r_sstatus_compat(void) {
  uint64 prmd = r_prmd();
  uint64 sstatus = 0;
  // SPP: kernel (PPLV!=3) → SPP=1, user (PPLV=3) → SPP=0
  if ((prmd & 0x3) != 3)  sstatus |= (1L << 8);  // SSTATUS_SPP
  // SPIE: PRMD.PIE → RISC-V SPIE
  if (prmd & PRMD_PIE)     sstatus |= (1L << 5);  // SSTATUS_SPIE
  return sstatus;
}
// w_sstatus writes PRMD for ertn return path.
static inline void w_sstatus_compat(uint64 x) {
  uint64 prmd = 0;
  // SPP=0 → PPLV=3 (user), SPP=1 → PPLV=0 (kernel)
  if ((x & (1L << 8)) == 0) prmd |= 3;   // user PLV3
  // SPIE=1 → PIE=1
  if (x & (1L << 5))        prmd |= PRMD_PIE;
  w_prmd(prmd);
}

#define r_sstatus()  r_sstatus_compat()
#define w_sstatus(x) w_sstatus_compat(x)

// These now work with the converted values.
#define SSTATUS_SPP   (1L << 8)   // RISC-V-compatible bit 8
#define SSTATUS_SPIE  (1L << 5)   // RISC-V-compatible bit 5

// cpu_idle — platform-specific idle instruction.
static inline void hal_cpu_idle(void) { asm volatile("idle 0"); }

// ============================================================
//  HAL unified interface wrappers (inline)
// ============================================================

// --- Core ID ---
static inline uint64 hal_get_hartid(void)       { return r_tp(); }

// --- Exception state ---
// Use PRMD-based sstatus for trap compatibility (checks previous mode)
static inline uint64 hal_read_sstatus(void)     { return r_sstatus(); }
static inline void   hal_write_sstatus(uint64 x) { w_sstatus(x); }
static inline uint64 hal_read_sepc(void)        { return r_era(); }
static inline void   hal_write_sepc(uint64 x)    { w_era(x); }
static inline uint64 hal_read_scause(void)      { return estat_to_scause(r_estat()); }
static inline uint64 hal_read_stval(void)       { return r_badv(); }

// --- Trap vector ---
static inline uint64 hal_read_stvec(void)       { return r_eentry(); }
static inline void   hal_write_stvec(uint64 x)   { w_eentry(x); }

// --- Page table base ---
static inline uint64 hal_read_satp(void)        { return r_pgdl(); }
static inline void   hal_write_satp(uint64 x)    { w_pgdl(x); }

// --- SP and RA ---
static inline uint64 hal_read_sp(void)          { return r_sp(); }
static inline uint64 hal_read_ra(void)          { return r_ra(); }

// --- Timer ---
// LoongArch Stable Counter (rdtime.d) provides a monotonically increasing
// 64-bit counter.  TVAL/TICLR handle the periodic timer interrupt.
static inline uint64 r_stable_counter(void) {
  uint64 x;
  asm volatile("rdtime.d %0, $r0" : "=r"(x));
  return x;
}
static inline uint64 hal_get_time(void)         { return r_stable_counter(); }
static inline void   hal_set_timer(uint64 next)  { (void)next; w_ticlr(1); }

// --- TLB ---
static inline void   hal_tlb_flush_all(void)    { sfence_vma(); }

// --- Interrupt control ---
static inline void   hal_intr_on(void)          { intr_on(); }
static inline void   hal_intr_off(void)         { intr_off(); }
static inline int    hal_intr_get(void)         { return intr_get(); }

#endif // __ASSEMBLER__
