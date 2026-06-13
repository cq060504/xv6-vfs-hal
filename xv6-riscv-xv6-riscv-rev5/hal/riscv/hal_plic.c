// RISC-V Platform Level Interrupt Controller (PLIC) driver.

#include "types.h"
#include "param.h"
#include "hal/hal.h"
#include "defs.h"

//
// the riscv Platform Level Interrupt Controller (PLIC).
//

void
plicinit(void)
{
  // set desired IRQ priorities non-zero (otherwise disabled).
  *(uint32*)(PLIC + UART0_IRQ*4) = 1;
  *(uint32*)(PLIC + VIRTIO0_IRQ*4) = 1;
#if defined(VIRTIO1_IRQ) && VIRTIO_NDISK > 1
  *(uint32*)(PLIC + VIRTIO1_IRQ*4) = 1;
#endif
}

void
plicinithart(void)
{
  int hart = cpuid();

  // set enable bits for this hart's S-mode
  // for the uart and virtio disk.
  uint32 mask = (1 << UART0_IRQ) | (1 << VIRTIO0_IRQ);
#if defined(VIRTIO1_IRQ) && VIRTIO_NDISK > 1
  mask |= (1 << VIRTIO1_IRQ);
#endif
  *(uint32*)PLIC_SENABLE(hart) = mask;

  // set this hart's S-mode priority threshold to 0.
  *(uint32*)PLIC_SPRIORITY(hart) = 0;
}

// ask the PLIC what interrupt we should serve.
int
plic_claim(void)
{
  int hart = cpuid();
  int irq = *(uint32*)PLIC_SCLAIM(hart);
  return irq;
}

// tell the PLIC we've served this IRQ.
void
plic_complete(int irq)
{
  int hart = cpuid();
  *(uint32*)PLIC_SCLAIM(hart) = irq;
}

// ---- HAL unified interface wrappers ----
void hal_irq_init(void)           { plicinit(); }
void hal_irq_hart_init(void)      { plicinithart(); }
int  hal_irq_claim(void)          { return plic_claim(); }
void hal_irq_complete(int irq)    { plic_complete(irq); }
