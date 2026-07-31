// LoongArch EIOINTC interrupt controller driver.
// Manages external I/O interrupts. Loader-backed RAM disks need no IRQ.
// Timer interrupts (ESTAT.IS[11]) are local to each CPU core and
// do not go through EIOINTC.
//
// Interrupt chain: device → PCH-PIC → EIOINTC → CPU (ESTAT.IS12).
//
// QEMU EIOINTC register layout (from v8.2.2 include/hw/intc/loongarch_extioi.h,
// offsets = hardware offset - APIC_OFFSET 0x400):
//   0x0A0  NODETYPE (16 bytes)
//   0x0C0  IPMAP (8 bytes, one byte per 32-IRQ group, value = pin bitmap)
//   0x200  ENABLE (32 bytes, 8 groups of 32 bits = 256 IRQs total)
//   0x280  BOUNCE (stored, not emulated)
//   0x300  ISR (32 bytes, raw pending bitmap)
//   0x400  COREISR (per-CPU per-group pending, write-1-to-clear)
//   0x800  COREMAP (256 bytes, one byte per IRQ: target CPU)
//
// QEMU PCH-PIC (TYPE_LOONGARCH_PCH_PIC) is mapped at VIRT_IOAPIC_REG_BASE
// = VIRT_PCH_REG_BASE = 0x10000000 (hw/loongarch/virt.c, ls7a.h).  Its
// int_mask resets to all-ones (everything masked), so interrupts never
// reach EIOINTC until the mask is cleared.  htmsi_vector[] resets to all
// zeros, so every input is routed to PCH-PIC output 0 = EIOINTC line 0.

#include "types.h"
#include "hal/hal.h"

// EIOINTC register access macros.
#define EIOINTC_EN(irq)    (*(volatile uint32*)(EIOINTC + 0x0200 + ((irq)/32)*4))
#define EIOINTC_IPMAP(irq) (*(volatile uint8*)(EIOINTC + 0x00C0 + (irq)/32))
#define EIOINTC_COREMAP(irq) (*(volatile uint8*)(EIOINTC + 0x0800 + (irq)))
#define EIOINTC_COREISR_GROUP0(hart) (*(volatile uint32*)(EIOINTC + 0x0400 + (hart)*4))
#define EIOINTC_COREISR_GROUP1(hart) (*(volatile uint32*)(EIOINTC + 0x0400 + (hart)*4 + 4))

// PCH-PIC register offsets (QEMU loongarch_pch_pic model, verified by
// register sweep at 0x10000000: 0x00=ID, 0x20/0x24=INT_MASK).
#define PCH_PIC_INT_MASK   0x20  // 1 = masked
#define PCH_PIC_INT_EDGE   0x28  // 1 = edge-triggered, 0 = level

// External interrupt vector number (EIOINTC line, see memlayout.h).
#define UART_VECTOR 0
// PCH-PIC input line carrying the UART irq (serial_mm_init irq = 2).
#define UART_PCH_PIC_INPUT 2

// Global interrupt controller initialization.
void
hal_irq_init(void)
{
  // PCH-PIC resets with int_mask = all-ones (all masked).  Unmask the
  // UART input (line 2) and keep it level-triggered (int_edge = 0).
  *(volatile uint32*)(0x10000000L + PCH_PIC_INT_MASK) &= ~(1U << UART_PCH_PIC_INPUT);

  // EIOINTC: enable UART line 0, route to IP pin 0 on core 0.
  EIOINTC_EN(UART_VECTOR) |= (1U << (UART_VECTOR % 32));
  EIOINTC_IPMAP(UART_VECTOR) = 0x1;      // pin 0
  EIOINTC_COREMAP(UART_VECTOR) = 0x0;    // core 0
}

// Per-core interrupt controller initialization.
void
hal_irq_hart_init(void)
{
  // Enable hardware external interrupt in ECFG (LIE bit 12).
  w_ecfg(r_ecfg() | ECFG_LIE_HWI);
}

// Claim the pending IRQ with the highest priority.
// Returns the IRQ number, or -1 if no interrupt pending.
// (-1, not 0: the UART sits on EIOINTC line 0, so 0 is a valid IRQ.)
int
hal_irq_claim(void)
{
  int hart = hal_get_hartid();
  uint64 estat = r_estat();

  // Verify there is an external interrupt pending.
  if ((estat & ESTAT_IS_HWI) == 0)
    return -1;

  // Read this core's COREISR bitmap (group 0 covers IRQs 0-31).
  uint64 isr = (uint64)EIOINTC_COREISR_GROUP0(hart);
  if (isr == 0)
    return -1;

  // Find the first set bit (simple bit scan).
  int irq = 0;
  while (irq < 32) {
    if (isr & (1ULL << irq)) {
      // Clear this IRQ in the COREISR (write 1 to clear).
      EIOINTC_COREISR_GROUP0(hart) = (1ULL << irq);
      return irq;
    }
    irq++;
  }
  return -1;
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
