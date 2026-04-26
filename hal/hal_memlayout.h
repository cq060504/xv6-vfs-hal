#ifndef _HAL_MEMLAYOUT_H_
#define _HAL_MEMLAYOUT_H_

#include "types.h"

// ============================================================
// 物理内存布局抽象接口
// 各平台提供自己的 memlayout 宏定义,并通过此头文件暴露
// ============================================================

// ---------- 必须由平台定义的宏 ----------
// 各平台在 hal/{riscv,loongarch}/memlayout.h 中定义:
//   HAL_KERNBASE    // 内核起始虚拟地址
//   HAL_PHYSTOP     // 物理内存结束地址
//   HAL_UART0       // UART 基址
//   HAL_VIRTIO0     // VIRTIO MMIO 基址
//   HAL_CLINT       // CLINT 基址
//   HAL_PLIC        // PLIC 基址
//   HAL_TRAMPOLINE  // trampoline 虚拟地址
//   HAL_TRAPFRAME   // trapframe 虚拟地址
//   HAL_KSTACK(p)   // 内核栈地址

// RISC-V 平台的实现直接包含原 memlayout.h
// 但我们按新架构重新组织

// ---------- 用户/内核地址空间边界 ----------
extern char HAL_ETEXT[];   // 内核代码结束(对应 etext)
extern char HAL_END[];     // 内核末尾地址

#endif