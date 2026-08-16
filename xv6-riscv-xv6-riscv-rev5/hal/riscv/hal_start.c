// RISC-V machine-mode startup: sets up privilege mode, delegates traps,
// initialises timer, and jumps to main() in supervisor mode.

#include "types.h"
#include "param.h"
#include "hal/hal.h"
#include "defs.h"

void main();
void timerinit();

// QEMU virt exposes a 10 MHz timebase. One xv6 tick is 100 ms (10 Hz).
#define TIMER_TICK_CYCLES 1000000UL

// entry.S needs one stack per CPU.
__attribute__ ((aligned (16))) char stack0[4096 * NCPU];

// entry.S jumps here in machine mode on stack0.
void
start()
{
  // set M Previous Privilege mode to Supervisor, for mret.
  unsigned long x = r_mstatus();
  x &= ~MSTATUS_MPP_MASK;
  x |= MSTATUS_MPP_S;
  w_mstatus(x);

  // set M Exception Program Counter to main, for mret.
  // requires gcc -mcmodel=medany
  w_mepc((uint64)main);

  // disable paging for now.
  w_satp(0);

  // delegate all interrupts and exceptions to supervisor mode.
  w_medeleg(0xffff);
  w_mideleg(0xffff);
  w_sie(r_sie() | SIE_SEIE | SIE_STIE);

  // configure Physical Memory Protection to give supervisor mode
  // access to all of physical memory.
  w_pmpaddr0(0x3fffffffffffffull);
  w_pmpcfg0(0xf);

  // ask for clock interrupts.
  timerinit();

  // keep each CPU's hartid in its tp register, for cpuid().
  int id = r_mhartid();
  w_tp(id);

  // switch to supervisor mode and jump to main().
  asm volatile("mret");
}

// ask each hart to generate timer interrupts.
void
timerinit()
{
  // enable supervisor-mode timer interrupts.
  w_mie(r_mie() | MIE_STIE);

  // enable the sstc extension (i.e. stimecmp).
  w_menvcfg(r_menvcfg() | (1L << 63));

  // allow supervisor to use stimecmp and time.
  w_mcounteren(r_mcounteren() | 2);

  // ask for the very first timer interrupt.
  w_stimecmp(r_time() + TIMER_TICK_CYCLES);
}

// ---- HAL unified interface wrapper ----
// timerinit() (called from start() in M-mode) configures M-mode CSR
// delegation so S-mode can access stimecmp/time.  hal_timer_init()
// (called from main() in S-mode) only programs the next one-shot
// deadline.  The two must remain separate because M-mode CSR writes
// (menvcfg/mcounteren) are illegal in S-mode.
void
hal_timer_init(void)
{
  w_stimecmp(r_time() + TIMER_TICK_CYCLES);
}

void
hal_timer_ack(void)
{
  w_stimecmp(r_time() + TIMER_TICK_CYCLES);
}
