// Timer abstraction interface.

#ifndef _HAL_TIMER_H_
#define _HAL_TIMER_H_

#include "types.h"

// ---- Read current time ----
uint64 hal_get_time(void);

// ---- Set next timer interrupt ----
void hal_set_timer(uint64 next);

// ---- Initialise timer interrupts ----
void hal_timer_init(void);

#endif
