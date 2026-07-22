// LoongArch 16550a UART driver.

#include "types.h"
#include "hal/hal.h"
#include "defs.h"

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

#define RHR 0
#define THR 0
#define ISR 2
#define LSR 5
#define LSR_RX_READY (1<<0)

extern volatile int panicking;
extern volatile int panicked;

__attribute__ ((section(".text.entry")))
void
hal_console_init(void)
{
  // On QEMU LoongArch: FIFO reset causes internal assertion crash.
  // Keep UART register config minimal; the emulated UART works with defaults.
}

void
hal_console_write(char buf[], int n)
{
  for(int i = 0; i < n; i++)
    hal_putchar(buf[i]);
}

void
hal_putchar(int c)
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

static int
uart_getc(void)
{
  if(ReadReg(LSR) & LSR_RX_READY)
    return ReadReg(RHR);
  else
    return -1;
}

void
hal_console_intr(void (*handler)(int))
{
  ReadReg(ISR);
  while(1){
    int c = uart_getc();
    if(c == -1)
      break;
    handler(c);
  }
}
