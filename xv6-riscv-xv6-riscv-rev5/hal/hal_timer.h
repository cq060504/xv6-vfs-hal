// Timer abstraction interface.

#ifndef _HAL_TIMER_H_
#define _HAL_TIMER_H_

#include "types.h"

// ---- Initialise timer interrupts ----
void hal_timer_init(void);

// Acknowledge the current tick and arrange the next platform tick.
void hal_timer_ack(void);

#endif
