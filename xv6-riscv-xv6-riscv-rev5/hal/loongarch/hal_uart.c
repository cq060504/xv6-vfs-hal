// LoongArch 16550a UART driver.
// Identical logic to RISC-V version; only the UART0 base address differs
// (provided by memlayout.h).

#include "types.h"
#include "param.h"
#include "hal/hal.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

#define RHR 0
#define THR 0
#define IER 1
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1)
#define ISR 2
#define LCR 3
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7)
#define LSR 5
#define LSR_RX_READY (1<<0)
#define LSR_TX_IDLE (1<<5)

static struct spinlock tx_lock;
static int tx_busy;
static int tx_chan;

extern volatile int panicking;
extern volatile int panicked;

__attribute__ ((section(".text.entry")))
void
uartinit(void)
{
  // On QEMU LoongArch: FIFO reset causes internal assertion crash.
  // Keep UART register config minimal — the emulated UART works with defaults.
  // Only init the lock for now. Full config deferred to Week 7.
  initlock(&tx_lock, "uart");
}

void
uartwrite(char buf[], int n)
{
  acquire(&tx_lock);
  int i = 0;
  while(i < n){
    while(tx_busy != 0)
      sleep(&tx_chan, &tx_lock);
    WriteReg(THR, buf[i]);
    i += 1;
    tx_busy = 1;
  }
  release(&tx_lock);
}

void
uartputc_sync(int c)
{
  if(panicking == 0)
    push_off();
  if(panicked){
    for(;;)
      ;
  }
  // QEMU LoongArch UART: LSR polling unreliable. Use delay instead.
  WriteReg(THR, c);
  // Small delay to let QEMU process the byte before next write
  for(volatile int d = 0; d < 50000; d++) asm volatile("");
  if(panicking == 0)
    pop_off();
}

int
uartgetc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY)
    return ReadReg(RHR);
  else
    return -1;
}

void
uartintr(void)
{
  ReadReg(ISR);
  acquire(&tx_lock);
  if(ReadReg(LSR) & LSR_TX_IDLE){
    tx_busy = 0;
    wakeup(&tx_chan);
  }
  release(&tx_lock);
  while(1){
    int c = uartgetc();
    if(c == -1)
      break;
    consoleintr(c);
  }
}

// HAL unified interface wrappers
void hal_console_init(void)                  { uartinit(); }
void hal_putchar_sync(int c)                 { uartputc_sync(c); }
void hal_putchar(int c)                      { uartputc_sync(c); }
int  hal_getchar(void)                       { return uartgetc(); }
void hal_console_intr(void (*handler)(int))  { uartintr(); }
