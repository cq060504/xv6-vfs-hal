#ifndef _HAL_CONSOLE_H_
#define _HAL_CONSOLE_H_

#include "types.h"

// ============================================================
// 控制台 I/O 抽象接口
// ============================================================

void hal_console_init(void);
void hal_putchar_sync(int c);   // 同步发送一个字符(用于 consputc)
void hal_putchar(int c);        // 中断驱动发送(或其他方式)
int  hal_getchar(void);         // 读入一个字符(非阻塞,-1 表示无数据)

// 注册 console 中断处理函数
void hal_console_intr(void (*handler)(int));

#endif