// Console I/O abstraction interface.

#ifndef _HAL_CONSOLE_H_
#define _HAL_CONSOLE_H_

#include "types.h"

void hal_console_init(void);
void hal_putchar_sync(int c);
void hal_putchar(int c);
int  hal_getchar(void);
void hal_console_intr(void (*handler)(int));

#endif
