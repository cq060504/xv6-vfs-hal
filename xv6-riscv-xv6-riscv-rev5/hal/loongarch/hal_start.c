// LoongArch platform initialization.
// Code runs from flash (VMA=LMA=0x1c000000). .data/.bss in RAM at 0x00400000.
// Copies .data from flash LMA to RAM VMA, clears .bss, then enters main().

#include "types.h"
#include "param.h"
#include "hal/hal.h"
#include "defs.h"

void main();
void timerinit();
void kernelvec();

// QEMU's LoongArch constant timer runs at 100 MHz. xv6 uses 10 Hz ticks.
#define TIMER_TICK_CYCLES 10000000UL

// Per-CPU boot/scheduler stacks. While a process is running, its CPU's
// scheduler context is suspended here, so the same page can be reused by the
// non-returning stack-overflow panic path (see hal_kvec.S).
__attribute__ ((aligned (16), section(".bootstack"))) char stack0[4096 * NCPU];

// Hart 0 sets this after data/bss init; secondary harts spin until it
// matches.  "la64done" as a little-endian 8-byte constant.
#define BOOT_DONE_MAGIC 0x6c613634646f6e65ULL
static volatile uint64 boot_done;

// Linker symbols: _data_lma = flash address of .data initial values
//                 _data_start = RAM VMA of .data
//                 _bss_end = RAM VMA of end of .bss
extern char _data_lma[], _data_start[], _bss_start[], _bss_end[];

__attribute__ ((section(".text.entry")))
void
start()
{
  int id = r_cpuid();
  w_tp(id);

  if(id == 0){
    // Copy .data from flash LMA to RAM VMA.
    uint64 *src = (uint64*)_data_lma;
    uint64 *dst = (uint64*)_data_start;
    while(dst < (uint64*)_bss_start) *dst++ = *src++;

    // Zero .bss in RAM. The boot stacks live in .bootstack, not .bss.
    dst = (uint64*)_bss_start;
    while(dst < (uint64*)_bss_end) *dst++ = 0;

    __sync_synchronize();
    boot_done = BOOT_DONE_MAGIC;
  } else {
    while(boot_done != BOOT_DONE_MAGIC)
      ;
    __sync_synchronize();
  }

  // NOTE: Timer interrupt NOT enabled here - will be enabled in main()
  // after trapinithart() is called. This prevents early timer interrupts
  // before kernel is fully initialized.

  w_eentry((uint64)kernelvec);

  main();
}

void timerinit()
{
  // Enable timer interrupt in ECFG
  w_ecfg(r_ecfg() | ECFG_LIE_TIMER);

  // Clear any pending timer interrupt
  w_ticlr(1);

  // Configure the per-core timer in periodic 100 ms mode (10 Hz).
  // TCFG format: [0]=EN, [1]=PERIODIC, [31:2]=INIT_VAL.
  w_tcfg((TIMER_TICK_CYCLES << TCFG_INITVAL_SHIFT) |
         TCFG_PERIOD | TCFG_EN);
}

void hal_timer_init(void) { timerinit(); }

void hal_timer_ack(void) { w_ticlr(1); }

// Called from kernelvec (hal_kvec.S) when a kernel stack overflow is detected.
// sp  = value of sp register at the time of the guard fault.
// badv = the virtual address that faulted (inside the guard page).
// Does not return — calls panic() after printing diagnostics.
void hal_stack_overflow_panic(uint64 sp, uint64 badv) {
  extern volatile int panicking;

  // The fault may have happened while printf's lock was held. Panic output
  // must therefore use the existing lock-free panic path.
  panicking = 1;
  printf("\nKERNEL STACK OVERFLOW\n");
  printf("  original sp  = 0x%lx\n", sp);
  printf("  fault addr   = 0x%lx\n", badv);
  panic("stack overflow");
}
