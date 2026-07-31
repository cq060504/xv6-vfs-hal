// Console I/O abstraction interface.

#ifndef _HAL_CONSOLE_H_
#define _HAL_CONSOLE_H_

#include "types.h"

void hal_console_init(void);
void hal_console_write(char buf[], int n);
void hal_putchar(int c);
void hal_console_intr(void (*handler)(int));
void hal_console_poll(void (*handler)(int));

#endif
