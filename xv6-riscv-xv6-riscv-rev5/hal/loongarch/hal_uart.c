// LoongArch 16550a UART driver.
//
// Interrupts: QEMU 8.2.2 does NOT deliver UART interrupts to the CPU.
// The 16550→PCH-PIC→EIOINTC chain is broken (verified empirically:
// 16550 IIR shows pending, PCH-PIC is unmasked, but EIOINTC COREISR
// never latches and ESTAT.IS12 never fires).  Therefore both TX and RX
// use only LSR polling — no interrupt-driven I/O is possible at guest
// level on this QEMU version.
//
// TX: hal_putchar() spin-waits on LSR_TX_IDLE before writing THR.
//     hal_console_write() loops over hal_putchar().
// RX: hal_console_intr() polls LSR_RX_READY in a drain loop.
//     clockintr() calls hal_console_poll() each timer tick, which
//     delegates to hal_console_intr() to drain any buffered input.

#include "types.h"
#include "hal/hal.h"
#include "defs.h"

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

#define RHR 0          // Receive Holding Register
#define THR 0          // Transmit Holding Register
#define IER 1          // Interrupt Enable Register
#define FCR 2          // FIFO Control Register
#define ISR 2          // Interrupt Status Register
#define LCR 3          // Line Control Register
#define LSR 5          // Line Status Register

#define LSR_RX_READY (1<<0)
#define LSR_TX_IDLE  (1<<5)

// Interrupt Enable Register bits
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)

// FIFO Control Register bits
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR  (3<<1)  // clear RX + TX FIFO

// Line Control Register bits
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7)   // DLAB

// QEMU's 16550 baud base is 115.2K; divisor 3 selects 38.4K.
#define BAUD_DIV 3

extern volatile int panicking;
extern volatile int panicked;

void
hal_console_init(void)
{
  // Disable interrupts during init.
  WriteReg(IER, 0x00);

  // Set baud rate to 38.4K.
  WriteReg(LCR, LCR_BAUD_LATCH);
  WriteReg(0, BAUD_DIV & 0xff);          // DLL (divisor low)
  WriteReg(1, (BAUD_DIV >> 8) & 0xff);   // DLM (divisor high)

  // 8 data bits, 1 stop bit, no parity; clear DLAB.
  WriteReg(LCR, LCR_EIGHT_BITS);

  // Enable and reset FIFOs.
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  // Enable RX interrupt in the 16550 IER so IIR reflects pending state.
  // Interrupt will NOT reach the CPU on QEMU 8.2.2; actual RX is via
  // hal_console_poll() → hal_console_intr() → LSR polling on each tick.
  WriteReg(IER, IER_RX_ENABLE);
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
  // Wait until the transmit holding register can accept another byte.
  while((ReadReg(LSR) & LSR_TX_IDLE) == 0)
    ;
  WriteReg(THR, c);
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

void
hal_console_poll(void (*handler)(int))
{
  hal_console_intr(handler);
}
