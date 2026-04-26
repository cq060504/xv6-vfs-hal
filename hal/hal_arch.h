#ifndef _HAL_ARCH_H_
#define _HAL_ARCH_H_

#include "types.h"

// ============================================================
// CPU 架构抽象接口
// 各平台需实现:hal/$(ARCH)/hal_arch.c
// ============================================================

// ---------- 处理器核心 ID ----------
uint64 hal_get_hartid(void);

// ---------- 全局中断控制 ----------
void intr_on(void);
void intr_off(void);
int  intr_get(void);

// ---------- 异常/中断状态 ----------
uint64 hal_read_sstatus(void);
void   hal_write_sstatus(uint64);
uint64 hal_read_sepc(void);
void   hal_write_sepc(uint64);
uint64 hal_read_scause(void);
uint64 hal_read_stval(void);

// ---------- 陷阱向量 ----------
uint64 hal_read_stvec(void);
void   hal_write_stvec(uint64);

// ---------- 页表基址 ----------
uint64 hal_read_satp(void);
void   hal_write_satp(uint64);

#endif