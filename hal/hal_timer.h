#ifndef _HAL_TIMER_H_
#define _HAL_TIMER_H_

#include "types.h"

// ============================================================
// 定时器抽象接口
// ============================================================

// ---------- 读当前时间(mtime / cycle 等) ----------
uint64 hal_get_time(void);

// ---------- 设置下次定时器中断触发时间 ----------
void hal_set_timer(uint64 next);

// ---------- 定时器中断使能 ----------
void hal_timer_init(void);     // 在 start.c 中调用

#endif