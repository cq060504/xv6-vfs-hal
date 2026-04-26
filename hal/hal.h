#ifndef _HAL_H_
#define _HAL_H_

// ============================================================
// xv6 硬件抽象层 - 统一包含头
// ============================================================
// 使用方式:
//   原 #include "riscv.h"     -> #include "hal/hal.h"
//   原 #include "memlayout.h" -> #include "hal/hal.h"
//   (hal.h 会统一提供所有 HAL 接口符号)
//
// 编译时需通过 -DHAL_PLATFORM=riscv 来指定平台
// 或通过 #include "hal/riscv/hal_impl.h" 统一包含
// ============================================================

#include "types.h"
#include "hal_arch.h"
#include "hal_vm.h"
#include "hal_intr.h"
#include "hal_timer.h"
#include "hal_memlayout.h"
#include "hal_console.h"
#include "hal_ctx.h"

// ---------- 平台相关寄存器定义 ----------
// 移入 hal/riscv/hal_arch.h 等,本文件只包含公共接口

#endif