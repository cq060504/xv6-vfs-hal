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

__attribute__ ((aligned (16), section(".bootstack"))) char stack0[4096 * NCPU];
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

  // Output early debug message via UART
  // Note: This happens before UART init in main(), use direct MMIO write
  volatile char *uart_lsr = (volatile char*)0x1FE001E5;  // LSR register
  volatile char *uart_thr = (volatile char*)0x1FE001E0;  // THR register

  if(id == 0){
    const char *msg = "start: begin\n";
    while (*msg) {
      // Wait for THR to be empty
      while ((*uart_lsr & (1 << 5)) == 0) ;
      *uart_thr = *msg++;
    }

    // Copy .data from flash LMA to RAM VMA.
    uint64 *src = (uint64*)_data_lma;
    uint64 *dst = (uint64*)_data_start;
    while(dst < (uint64*)_bss_start) *dst++ = *src++;

    // Zero .bss in RAM. The boot stacks live in .bootstack, not .bss.
    dst = (uint64*)_bss_start;
    while(dst < (uint64*)_bss_end) *dst++ = 0;

    __sync_synchronize();
    boot_done = 0x6c613634646f6e65ULL;

    msg = "start: calling main\n";
    while (*msg) {
      while ((*uart_lsr & (1 << 5)) == 0) ;
      *uart_thr = *msg++;
    }
  } else {
    while(boot_done != 0x6c613634646f6e65ULL)
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

  // Configure timer in periodic mode (~10ms between interrupts).
  // TCFG format: [0]=EN, [1]=PERIODIC, [31:2]=INIT_VAL.
  // At ~1GHz clock, 10000000 counts ≈ 10ms.
  uint64 interval = 10000000;
  w_tcfg((interval << TCFG_INITVAL_SHIFT) | TCFG_PERIOD | TCFG_EN);
}

void hal_timer_init(void) { timerinit(); }
