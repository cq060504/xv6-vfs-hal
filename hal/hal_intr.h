#ifndef _HAL_INTR_H_
#define _HAL_INTR_H_

#include "types.h"

// ============================================================
// 中断控制器抽象接口
// ============================================================

void    hal_irq_init(void);          // plicinit
void    hal_irq_hart_init(void);     // plicinithart
int     hal_irq_claim(void);         // plic_claim
void    hal_irq_complete(int);       // plic_complete

#endif