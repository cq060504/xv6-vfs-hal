// Low-level driver for the LoongArch virt machine's 16550a UART.

#include "types.h"
#include "param.h"
#include "hal/hal.h"
#include "spinlock.h"
#include "proc.h"
#include "defs.h"

#define Reg(reg) ((volatile unsigned char *)(UART0 + (reg)))

#define ReadReg(reg) (*(Reg(reg)))
#define WriteReg(reg, v) (*(Reg(reg)) = (v))

#define RHR 0                 // receive holding register
#define THR 0                 // transmit holding register
#define IER 1                 // interrupt enable register
#define IER_RX_ENABLE (1<<0)
#define IER_TX_ENABLE (1<<1)
#define FCR 2                 // FIFO control register
#define FCR_FIFO_ENABLE (1<<0)
#define FCR_FIFO_CLEAR (3<<1)
#define ISR 2                 // interrupt status register
#define LCR 3                 // line control register
#define LCR_EIGHT_BITS (3<<0)
#define LCR_BAUD_LATCH (1<<7)
#define LSR 5                 // line status register
#define LSR_RX_READY (1<<0)
#define LSR_TX_IDLE (1<<5)

#define BAUD_DIV 3

static struct spinlock tx_lock;
static int tx_busy;
static int tx_chan;

extern volatile int panicking;
extern volatile int panicked;

void
hal_console_init(void)
{
  WriteReg(IER, 0x00);

  WriteReg(LCR, LCR_BAUD_LATCH);
  WriteReg(0, BAUD_DIV & 0xff);
  WriteReg(1, (BAUD_DIV >> 8) & 0xff);

  WriteReg(LCR, LCR_EIGHT_BITS);
  WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR);

  initlock(&tx_lock, "uart");
  WriteReg(IER, IER_TX_ENABLE | IER_RX_ENABLE);
}

// Transmit from process context. The TX-empty interrupt wakes a writer
// after the UART accepts each byte.
void
hal_console_write(char buf[], int n)
{
  acquire(&tx_lock);

  int i = 0;
  while(i < n){
    while(tx_busy != 0)
      sleep(&tx_chan, &tx_lock);

    WriteReg(THR, buf[i]);
    i++;
    tx_busy = 1;
  }

  release(&tx_lock);
}

// Synchronous output for printf(), panic(), and console echo.
void
hal_putchar(int c)
{
  if(panicking == 0)
    push_off();

  if(panicked){
    for(;;)
      ;
  }

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
  return -1;
}

void
hal_console_intr(void (*handler)(int))
{
  ReadReg(ISR);

  acquire(&tx_lock);
  if(ReadReg(LSR) & LSR_TX_IDLE){
    tx_busy = 0;
    wakeup(&tx_chan);
  }
  release(&tx_lock);

  for(;;){
    int c = uart_getc();
    if(c == -1)
      break;
    handler(c);
  }
}
