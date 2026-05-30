// LoongArch EIOINTC interrupt controller driver.
// Manages external I/O interrupts (UART, virtio, etc.).
// Timer interrupts (ESTAT.IS[11]) are local to each CPU core and
// do not go through EIOINTC.
//
// QEMU EIOINTC register layout (from hw/intc/loongarch_extioi.c):
//   0x0000  Core ISR (8 bytes per core, 64-bit bitmap of pending IRQs)
//   0x0040  Core Enable (1 byte per core)
//   0x0060  Bounce (32 bytes, edge-triggered IRQ clear)
//   0x0080  IRQ Enable (32 bytes, 8 groups of 32 bits = 256 IRQs total)
//   0x0400  IP Map (256 bytes, one byte per IRQ for core routing)

#include "types.h"
#include "hal/hal.h"

// Register access macros (64-bit access for ISR, 32-bit for others).
#define EIOINTC_ISR(hart)    (*(volatile uint64*)(EIOINTC + 0x0000 + (hart)*8))
#define EIOINTC_CORE_EN(hart) (*(volatile uint32*)(EIOINTC + 0x0040 + (hart)*4))
#define EIOINTC_IRQ_EN(irq)  (*(volatile uint32*)(EIOINTC + 0x0080 + ((irq)/32)*4))
#define EIOINTC_IPMAP(irq)   (*(volatile uint8*)(EIOINTC + 0x0400 + (irq)))

// External interrupt vector numbers.
#define UART_VECTOR   31
#define VIRTIO_VECTOR 32

// Global interrupt controller initialization.
void
hal_irq_init(void)
{
  // Enable specific IRQ lines (UART + virtio).
  EIOINTC_IRQ_EN(UART_VECTOR)   |= (1 << (UART_VECTOR % 32));
  EIOINTC_IRQ_EN(VIRTIO_VECTOR) |= (1 << (VIRTIO_VECTOR % 32));

  // Route UART and virtio interrupts to core 0.
  EIOINTC_IPMAP(UART_VECTOR)   = 0x1;  // core 0
  EIOINTC_IPMAP(VIRTIO_VECTOR) = 0x1;  // core 0
}

// Per-core interrupt controller initialization.
void
hal_irq_hart_init(void)
{
  int hart = hal_get_hartid();

  // Enable interrupt reception for this core.
  EIOINTC_CORE_EN(hart) = 0x1;

  // Enable hardware external interrupt in ECFG (LIE bit 12).
  w_ecfg(r_ecfg() | ECFG_LIE_HWI);
}

// Claim the pending IRQ with the highest priority.
// Returns the IRQ number, or 0 if no interrupt pending.
int
hal_irq_claim(void)
{
  int hart = hal_get_hartid();
  uint64 estat = r_estat();

  // Verify there is an external interrupt pending.
  if ((estat & ESTAT_IS_HWI) == 0)
    return 0;

  // Read the 64-bit ISR bitmap for this core.
  uint64 isr = EIOINTC_ISR(hart);
  if (isr == 0)
    return 0;

  // Find the first set bit (simple bit scan).
  int irq = 0;
  while (irq < 64) {
    if (isr & (1ULL << irq)) {
      // Clear this IRQ in the ISR (write 1 to clear).
      EIOINTC_ISR(hart) = (1ULL << irq);
      return irq;
    }
    irq++;
  }
  return 0;
}

// Complete handling of the given IRQ.
void
hal_irq_complete(int irq)
{
  // The ISR bit was already cleared during claim().
  // EIOINTC doesn't need explicit completion — interrupts are
  // re-enabled by clearing the ISR bit.
  (void)irq;
}
