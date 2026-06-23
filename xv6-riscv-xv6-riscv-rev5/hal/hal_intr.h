// Interrupt controller abstraction interface for out device .different from cpu interrupts, which are in hal_cpu.h

#ifndef _HAL_INTR_H_
#define _HAL_INTR_H_

#include "types.h"

// ---- Platform-level interrupt controller ----
void hal_irq_init(void);          // initialise controller
void hal_irq_hart_init(void);     // per-hart initialisation
int  hal_irq_claim(void);         // claim pending IRQ
void hal_irq_complete(int);       // mark IRQ complete

#endif
