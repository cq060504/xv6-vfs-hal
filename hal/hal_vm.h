#ifndef _HAL_VM_H_
#define _HAL_VM_H_

#include "types.h"

// ============================================================
// 内存管理抽象接口
// ============================================================

// ---------- TLB 刷写 ----------
void hal_tlb_flush_all(void);

// ---------- 页表等级和标志位 ----------
// 需在平台实现中定义:
//   #define HAL_PTE_V   0x001  // Valid
//   #define HAL_PTE_R   0x002  // Read
//   #define HAL_PTE_W   0x004  // Write
//   #define HAL_PTE_X   0x008  // Execute
//   #define HAL_PTE_U   0x010  // User
//   #define HAL_PTE_G   0x020  // Global
//   #define HAL_PTE_A   0x040  // Accessed
//   #define HAL_PTE_D   0x080  // Dirty
//   #define HAL_MAXVA   (1ULL << 38)  // 页表支持的最大虚拟地址

// ---------- 物理地址和页面操作 ----------
#define HAL_PGSIZE    4096
#define HAL_PGSHIFT   12

// 若在 riscv.h 中使用的 PTE 操作函数,需要抽象如下:
//   typedef uint64 hal_pte_t;
//   uint64 hal_walk(pagetable_t, uint64, int alloc);

// ---------- 用户内存布局相关 ----------
// 由 hal_memlayout.h 提供

#endif