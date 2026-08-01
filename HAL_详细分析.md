# xv6-riscv HAL 层详细架构分析

## 目录

1. [HAL 设计目标与架构](#一hal-设计目标与架构)
2. [目录结构与 Include 链](#二目录结构与-include-链)
3. [公共 HAL 接口头文件](#三公共-hal-接口头文件)
4. [RISC-V 平台实现](#四risc-v-平台实现)
5. [LoongArch 平台实现](#五loongarch-平台实现)
6. [关键架构差异对照表](#六关键架构差异对照表)
7. [系统启动流程与 HAL 调用链](#七系统启动流程与-hal-调用链)
8. [各子系统 HAL 调用关系图](#八各子系统-hal-调用关系图)
9. [内核通用代码如何使用 HAL](#九内核通用代码如何使用-hal)
   - 9.1 trap.c — 陷阱处理（HAL 最密集）
   - 9.2 vm.c — 虚拟内存
   - 9.3 proc.c — 进程管理
   - 9.4 main.c — 启动入口
   - 9.5 console.c — 控制台 I/O
   - 9.6 exec.c — 程序加载
   - 9.7 sysproc.c/sysfile.c
   - 9.8 kalloc.c — 物理内存
   - 9.9 不使用 HAL 的内核文件
10. [HAL 在内核中的使用密度热力图](#十hal-在内核中的使用密度热力图)
11. [HAL 接口覆盖度验证](#十一hal-接口覆盖度验证)
12. [从内核视角看 HAL：完整调用链图](#十二从内核视角看-hal完整调用链图)
13. [附录：文件统计](#附录文件统计)

---

## 一、HAL 设计目标与架构

### 1.1 核心价值

xv6 原本只支持 RISC-V 单架构。引入 HAL（Hardware Abstraction Layer）后，**内核通用代码（kernel/）与平台相关代码完全解耦**，使同一份 `vm.c`、`proc.c`、`trap.c` 可以同时编译运行在 RISC-V 和 LoongArch 两个完全不同的 CPU 架构上。

### 1.2 隔离层次

```
┌─────────────────────────────────────────────────────┐
│  内核通用代码 (kernel/*.c)                           │
│  vm.c, proc.c, trap.c, main.c, console.c ...        │
│  只调用 hal_*() 前缀函数，不直接操作 CSR 或 MMIO      │
└──────────────────────┬──────────────────────────────┘
                       │ hal_*() 接口
┌──────────────────────┴──────────────────────────────┐
│  公共 HAL 头文件 (hal/hal_*.h)                       │
│  声明接口，不包含平台相关实现                          │
└──────────┬───────────────────────┬──────────────────┘
           │ ARCH=riscv             │ ARCH=loongarch
┌──────────┴──────────┐  ┌──────────┴──────────────────┐
│ hal/riscv/          │  │ hal/loongarch/              │
│  arch.h (CSR/PTE)   │  │  arch.h (CSR/PTE/refill)   │
│  memlayout.h        │  │  memlayout.h (DMW/KSTACK)   │
│  hal_entry.S        │  │  hal_entry.S                │
│  hal_start.c        │  │  hal_start.c                │
│  hal_swtch.S        │  │  hal_swtch.S                │
│  hal_tramp.S        │  │  hal_tramp.S                │
│  hal_kvec.S         │  │  hal_kvec.S (含栈溢出检测)   │
│  hal_plic.c         │  │  hal_tlbrefill.S (软件refill)│
│  hal_uart.c         │  │  hal_intr.c (EIOINTC)       │
│  hal_virtio.c       │  │  hal_uart.c                 │
│  kernel.ld          │  │  hal_virtio.c (RAM-disk)    │
│                     │  │  kernel.ld                  │
└─────────────────────┘  └─────────────────────────────┘
```

---

## 二、目录结构与 Include 链

### 2.1 目录结构

```
hal/
├── hal.h                  ← 顶层统一入口，根据 ARCH 条件选择平台
├── hal_arch.h             ← CPU 控制接口声明
├── hal_console.h          ← 控制台 I/O 接口声明
├── hal_ctx.h              ← 上下文切换结构体 + 函数声明
├── hal_intr.h             ← 中断控制器接口声明
├── hal_memlayout.h        ← 内存布局（文档性质，实际宏来自平台头文件）
├── hal_timer.h            ← 定时器接口声明
├── hal_vm.h               ← 虚拟内存/MMU 接口声明
│
├── riscv/                 ← RISC-V 平台实现（11 个文件）
│   ├── arch.h             ← CSR 内联函数 + PTE 宏 + HAL wrappers
│   ├── memlayout.h        ← QEMU virt 物理内存布局
│   ├── hal_entry.S        ← 启动入口
│   ├── hal_start.c        ← M 态启动 + 定时器初始化
│   ├── hal_swtch.S        ← 上下文切换
│   ├── hal_tramp.S        ← 用户↔内核陷入跳板
│   ├── hal_kvec.S         ← 内核态异常向量
│   ├── hal_plic.c         ← PLIC 中断控制器驱动
│   ├── hal_uart.c         ← 16550a 串口驱动
│   ├── hal_virtio.c       ← Virtio MMIO 磁盘驱动
│   └── kernel.ld          ← RISC-V 链接脚本
│
└── loongarch/             ← LoongArch 平台实现（12 个文件）
    ├── arch.h             ← CSR 内联函数 + PTE 宏 + HAL wrappers
    ├── memlayout.h        ← QEMU virt (LS7A) 物理内存布局
    ├── hal_entry.S        ← 启动入口
    ├── hal_start.c        ← 平台初始化 + 定时器 + 栈溢出 panic
    ├── hal_swtch.S        ← 上下文切换
    ├── hal_tramp.S        ← 用户↔内核陷入跳板
    ├── hal_kvec.S         ← 内核态异常向量（含栈溢出检测）
    ├── hal_tlbrefill.S    ← 软件 TLB 重填（★ LA 独有）
    ├── hal_intr.c         ← EIOINTC 中断控制器驱动
    ├── hal_uart.c         ← 16550a 串口驱动
    ├── hal_virtio.c       ← RAM-disk 替代实现
    └── kernel.ld          ← LoongArch 链接脚本
```

### 2.2 Include 链

任何内核 .c 文件只需：

```c
#include "hal/hal.h"
```

`hal.h` 的内部展开顺序：

```
hal.h
  │
  ├─ #include "types.h"                ← 基本类型 (uint64 等)
  │
  ├─ [条件编译选择平台]
  │   ├─ #ifdef ARCH_riscv
  │   │     #include "arch.h"          ← hal/riscv/arch.h
  │   │     #include "memlayout.h"     ← hal/riscv/memlayout.h
  │   ├─ #elif ARCH_loongarch
  │   │     #include "arch.h"          ← hal/loongarch/arch.h
  │   │     #include "memlayout.h"     ← hal/loongarch/memlayout.h
  │
  ├─ #include "hal_arch.h"             ← 声明 hal_read_sstatus() 等
  ├─ #include "hal_vm.h"               ← 声明 hal_tlb_flush_all()
  ├─ #include "hal_intr.h"             ← 声明 hal_irq_init() 等
  ├─ #include "hal_timer.h"            ← 声明 hal_get_time() 等
  ├─ #include "hal_memlayout.h"        ← 声明 HAL_ETEXT, HAL_END
  ├─ #include "hal_console.h"          ← 声明 hal_console_write() 等
  └─ #include "hal_ctx.h"             ← struct hal_context + hal_switch()
```

**注意：** `#ifndef __ASSEMBLER__` 保护 C 代码不被汇编器解析。汇编文件（.S）直接 include `arch.h` 和 `memlayout.h`，C 内联函数自动跳过。这使得**同一个头文件既可以被 C 编译器解析，也可以被汇编器解析**。

---

## 三、公共 HAL 接口头文件

### 3.1 `hal/hal.h` — 统一入口

```c
// hal/hal.h (精简版)
#ifndef HAL_H
#define HAL_H

#include "types.h"

// 平台选择
#ifdef ARCH_riscv
  #include "arch.h"        // → hal/riscv/arch.h
  #include "memlayout.h"   // → hal/riscv/memlayout.h
#elif defined(ARCH_loongarch)
  #include "arch.h"        // → hal/loongarch/arch.h
  #include "memlayout.h"   // → hal/loongarch/memlayout.h
#endif

// 子系统接口声明
#include "hal_arch.h"
#include "hal_vm.h"
#include "hal_intr.h"
#include "hal_timer.h"
#include "hal_memlayout.h"
#include "hal_console.h"
#include "hal_ctx.h"

#endif
```

**功能：** 纯编排作用，无任何函数定义。内核代码只需 `#include "hal/hal.h"`，所有平台相关的 CSR 访问、PTE 格式、内存布局常量都会被正确引入。

### 3.2 `hal/hal_arch.h` — CPU 架构接口

声明 18 个 CPU 控制函数。所有实现在各平台的 `arch.h` 中作为 `static inline` 提供：

| 函数 | 功能 | RISC-V 实现 | LoongArch 实现 |
|------|------|-------------|----------------|
| `hal_get_hartid()` | 获取当前 CPU 核心号 | `r_tp()` | `r_tp()` |
| `hal_intr_on()` | 开启中断 | `intr_on()` (set SIE) | `intr_on()` (set CRMD.IE) |
| `hal_intr_off()` | 关��中断 | `intr_off()` (clear SIE) | `intr_off()` (clear CRMD.IE) |
| `hal_intr_get()` | 获取中断状态 | `intr_get()` | `intr_get()` |
| `hal_read_sstatus()` | 读状态寄存器 | `r_sstatus()` | `r_sstatus_compat()` (PRMD→sstatus) |
| `hal_write_sstatus(x)` | 写状态寄存器 | `w_sstatus(x)` | `w_sstatus_compat(x)` (sstatus→PRMD) |
| `hal_read_sepc()` | 读异常返回地址 | `r_sepc()` | `r_era()` |
| `hal_write_sepc(x)` | 写异常返回地址 | `w_sepc(x)` | `w_era(x)` |
| `hal_read_scause()` | 读异常原因 | `r_scause()` | `estat_to_scause(r_estat())` ★ |
| `hal_read_stval()` | 读异常地址 | `r_stval()` | `r_badv()` |
| `hal_read_stvec()` | 读异常向量基址 | `r_stvec()` | `r_eentry()` |
| `hal_write_stvec(x)` | 写异常向量基址 | `w_stvec(x)` | `w_eentry(x)` |
| `hal_read_satp()` | 读页表基址 | `r_satp()` | `r_pgdl()` |
| `hal_write_satp(x)` | 写页表基址 | `w_satp(x)` | `w_pgdl(x)` |
| `hal_read_sp()` | 读栈指针 | `r_sp()` | `r_sp()` |
| `hal_read_ra()` | 读返回地址 | `r_ra()` | `r_ra()` |
| `hal_cpu_idle()` | CPU 空闲等待 | `wfi` | `idle 0` |

**★ 关键适配点：** `hal_read_scause()` 在 LoongArch 上不是简单的 CSR 读取，而是通过 `estat_to_scause()` 函数将 LoongArch 的异常码（ESTAT.Ecode）转换为 RISC-V 的 scause 值。这是让 `trap.c` 无需修改的关键适配。

### 3.3 `hal/hal_console.h` — 控制台 I/O 接口

| 函数 | 功能 | RISC-V | LoongArch |
|------|------|--------|-----------|
| `hal_console_init()` | 初始化 UART 硬件 | 设波特率/FIFO/中断 | 空函数（QEMU 默认配置） |
| `hal_console_write(buf,n)` | 阻塞写入 n 字节 | 逐字节+锁+sleep | 循环 hal_putchar |
| `hal_putchar(c)` | 单字符轮询输出 (供 printf) | 等 LSR 空闲→写 THR | 等 LSR 空闲→写 THR |
| `hal_console_intr(h)` | 中断处理，读入字符 | ISR 确认+读 RBR | ISR 确认+读 RBR |

**差异原因：** HAL 接口抽象保证两架构使用相同的 API。RISC-V 版本使用 TX 锁+sleep 实现阻塞输出，LoongArch 版本使用更简单的轮询方式。两架构均通过 LSR 硬件状态寄存器精确判断 UART 发送就绪。

### 3.4 `hal/hal_ctx.h` — 上下文切换

```c
// RISC-V: 14 个 callee-saved 寄存器
struct hal_context {
  uint64 ra;     // offset 0
  uint64 sp;     // offset 8
  uint64 s0;     // offset 16
  uint64 s1;     // offset 24
  uint64 s2;     // offset 32
  uint64 s3;     // offset 40
  uint64 s4;     // offset 48
  uint64 s5;     // offset 56
  uint64 s6;     // offset 64
  uint64 s7;     // offset 72
  uint64 s8;     // offset 80
  uint64 s9;     // offset 88
  uint64 s10;    // offset 96
  uint64 s11;    // offset 104
};

// LoongArch: 12 个 callee-saved 寄存器
struct hal_context {
  uint64 ra;     // $r1,  offset 0
  uint64 sp;     // $r3,  offset 8
  uint64 fp;     // $r22, offset 16 (s9)
  uint64 s0;     // $r23, offset 24
  uint64 s1;     // $r24, offset 32
  uint64 s2;     // $r25, offset 40
  uint64 s3;     // $r26, offset 48
  uint64 s4;     // $r27, offset 56
  uint64 s5;     // $r28, offset 64
  uint64 s6;     // $r29, offset 72
  uint64 s7;     // $r30, offset 80
  uint64 s8;     // $r31, offset 88
};

void hal_switch(struct hal_context *old, struct hal_context *new);
```

**差异原因：** LoongArch 整数寄存器 r0-r31 中，callee-saved 为 r1(ra), r3(sp), r22(fp/s9), r23-r31(s0-s8) = 12 个。RISC-V 的 callee-saved 多了 s9/s10/s11 = 14 个。两者的 hal_swtch.S 保存/恢复的寄存器列表和 offset 布局必须一致。编译器在调用 hal_switch 前已将 caller-saved 寄存器保存到栈上。

### 3.5 `hal/hal_intr.h` — 中断控制器接口

| 函数 | 功能 | RISC-V | LoongArch |
|------|------|--------|-----------|
| `hal_irq_init()` | 初始化中断控制器 | PLIC 设优先级 | EIOINTC 使能+路由 IRQ |
| `hal_irq_hart_init()` | Per-hart 中断初始化 | PLIC 使能+阈值 | EIOINTC 核心使能+ECFG |
| `hal_irq_claim()` | 获取待处理 IRQ | 读 PLIC_SCLAIM | 扫描 EIOINTC ISR 位图 |
| `hal_irq_complete(irq)` | 完成 IRQ 处理 | 写 PLIC_SCLAIM | 空函数（ISR 已清零） |

### 3.6 `hal/hal_timer.h` — 定时器接口

| 函数 | 功能 | RISC-V | LoongArch |
|------|------|--------|-----------|
| `hal_get_time()` | 读硬件计数器 | `r_time()` (rdtime) | `rdtime.d` |
| `hal_set_timer(next)` | 设下次中断时间 | `w_stimecmp(next)` | `w_ticlr(1)` |
| `hal_timer_init()` | 初始化定时器 | 设 stimecmp | 设 TCFG 周期模式 |

**差异原因：** RISC-V 使用 Sstc 扩展的 one-shot 模式（`stimecmp`），每次中断后重新设下一次时间。LoongArch 使用 CSR 定时器周期模式（`TCFG`），设置一次后自动周期性产生中断，只需清除中断标志（`TICLR`）。

### 3.7 `hal/hal_vm.h` — 虚拟内存接口

```c
void hal_tlb_flush_all(void);  // RISC-V: sfence.vma; LA: invtlb 0, $r0, $r0
```

页大小、PTE 格式、TRAMPOLINE、TRAPFRAME、KSTACK、MAXVA 等常量由各平台的 `arch.h` 和 `memlayout.h` 提供。

### 3.8 `hal/hal_memlayout.h` — 文档性质

```c
extern char HAL_ETEXT[];   // 内核代码结束
extern char HAL_END[];     // 内核镜像结束
```

实际的内存布局宏（`UART0`, `PLIC`, `KERNBASE`, `PHYSTOP` 等）由各平台的 `memlayout.h` 定义。此文件只声明跨平台共享的内核边界符号。

---

## 四、RISC-V 平台实现

### 4.1 `hal/riscv/arch.h` — 核心架构定义（395 行）

**CSR 内联函数（30+ 个）：**

RISC-V 需要 m-mode 和 s-mode 两套 CSR。所有访问函数使用 `static inline` + 内联汇编。

```
关键 CSR 函数组：
  M-mode: r/w_mstatus, w_mepc, r/w_medeleg, r/w_mideleg, r_mhartid
          w_pmpcfg0/addr0, r/w_mie, r/w_menvcfg, r/w_mcounteren
  S-mode: r/w_sstatus, r/w_sie, r/w_sepc, r/w_stvec, r/w_satp
          r_scause, r_stval, r/w_stimecmp, r_time
```

**中断控制（内联）：**

```c
static inline void intr_on()  { w_sstatus(r_sstatus() | SSTATUS_SIE); }
static inline void intr_off() { w_sstatus(r_sstatus() & ~SSTATUS_SIE); }
static inline int  intr_get() { return (r_sstatus() & SSTATUS_SIE) != 0; }
```

通过 SSTATUS_SIE 位控制/查询中断状态。

**Sv39 页表常量：**

```c
#define SATP_SV39  (8L << 60)
#define MAKE_SATP(pagetable) (SATP_SV39 | (((uint64)pagetable) >> 12))
// Sv39 分页: 9-9-9-12
#define PT_LEVELS  3
#define MAXVA      (1L << (9+9+9+12-1))  // 2^38 = 0x4000000000

// PTE 格式: PPN 连续存于 [53:10]
#define PA2PTE(pa)  ((((uint64)pa) >> 12) << 10)
#define PTE2PA(pte) (((pte) >> 10) << 12)
#define PTE_FLAGS(pte) ((pte) & 0x3FF)
#define hal_pte_encode_perm(perm) (perm)    // 恒等映射
```

**HAL 统一接口 wrapper：**

所有 `hal_*()` 函数（18 个）直接在 arch.h 底部实现，一对一映射到 CSR 内联函数：

```c
static inline uint64 hal_get_hartid(void)     { return r_tp(); }
static inline uint64 hal_read_sstatus(void)   { return r_sstatus(); }
static inline void   hal_write_sstatus(uint64 x) { w_sstatus(x); }
// ... 等
static inline void   hal_tlb_flush_all(void)  { sfence_vma(); }
static inline void   hal_cpu_idle(void)       { asm volatile("wfi"); }
```

### 4.2 `hal/riscv/memlayout.h` — 物理内存布局

```
QEMU -machine virt:

0x00001000  Boot ROM
0x02000000  CLINT（核心本地中断器）
0x0C000000  PLIC（平台级中断控制器）
0x10000000  UART0 (IRQ 10)
0x10001000  VIRTIO0 MMIO (IRQ 1)
0x10002000  VIRTIO1 MMIO (IRQ 2) [可选]
0x10003000  VIRTIO2 MMIO (IRQ 3) [可选]
0x80000000  KERNBASE (内核加载点)
            PHYSTOP = KERNBASE + 128MB

虚拟地址布局（用户空间顶部）:
  TRAMPOLINE = MAXVA - PGSIZE        ≈ 0x3FFFFFF000
  TRAPFRAME  = TRAMPOLINE - PGSIZE   ≈ 0x3FFFFFE000
  KSTACK(p)  = TRAMPOLINE - (p+1)*3*PGSIZE  ← 每进程 3 页（2映射+1guard）
  USER_TOP   = TRAPFRAME
```

**PLIC 子寄存器宏：** `PLIC_PRIORITY`, `PLIC_PENDING`, `PLIC_SENABLE(hart)`, `PLIC_SPRIORITY(hart)`, `PLIC_SCLAIM(hart)` — 这些宏使 `hal_plic.c` 和 RISC-V 原版 `plic.c` 保持兼容。

**地址验证：** `hal_pagetable_va_valid(va)` 只接受 `va < MAXVA`。

### 4.3 `hal/riscv/hal_entry.S` — 启动入口

```
_entry:  (链接到 0x80000000)
  ① sp = stack0 + ((mhartid + 1) * 4096)    ← 每 CPU 独立启动栈
  ② call start()                             ← 进入 M 态 C 代码
  ③ j spin                                    ← start() 不应返回
```

### 4.4 `hal/riscv/hal_start.c` — M 态启动

**`start()` 流程：**
1. 设 `mstatus.MPP = Supervisor`（下次 mret 进入 S 态）
2. 设 `mepc = main`（mret 后跳转到 main()）
3. `satp = 0`（禁用分页）
4. `medeleg = 0xffff`, `mideleg = 0xffff`（所有异常/中断委托给 S 态）
5. PMP 放开全部物理内存给 S 态
6. `timerinit()`（使能定时器中断）
7. `tp = mhartid`（保存 hart ID）
8. `mret`（切到 S 态，PC=main()）

**`timerinit()` 流程：**
1. `mie |= STIE`（使能 S 态定时器中断）
2. `menvcfg[63] = 1`（使能 Sstc 扩展）
3. `mcounteren |= 2`（允许 S 态访问 stimecmp/time）
4. `stimecmp = time + 1000000`（第一次中断 ~1ms 后）

**`hal_timer_init()`**: 仅设 `stimecmp = time + 1000000`（S 态安全，不碰 M 态 CSR）。

### 4.5 `hal/riscv/hal_swtch.S` — 上下文切换

```
hal_switch(a0=old, a1=new):
  保存 14 个寄存器 → *old:  ra(0), sp(8), s0(16), s1(24), ..., s11(104)
  恢复 14 个寄存器 ← *new:  同 offset
  ret                          ← PC = ra (跳到新上下文)
```

### 4.6 `hal/riscv/hal_tramp.S` — 用户↔内核跳板

**`uservec`（用户态 → 内核态）：**
1. `csrrw a0, sscratch, a0` — 保存用户 a0，获取 trapframe 地址
2. 保存全部 32 个用户寄存器到 trapframe (offset 40~280)
3. 恢复用户 a0 从 sscratch，保存到 trapframe
4. 加载：sp=kernel_sp, tp=kernel_hartid, t0=usertrap, t1=kernel_satp
5. `csrw satp, t1; sfence.vma` — 切换到内核页表
6. `jr t0` — 跳到 usertrap()

**`userret`（内核态 → 用户态）：**
1. `csrw satp, a0; sfence.vma` — 切换到用户页表
2. 从 trapframe 恢复全部 32 个寄存器
3. `sret` — 返回用户态

**关键：** TRAMPOLINE 必须同时映射到用户和内核页表的同一 VA。切换 satp 的指令位于 trampoline 页中——切换前后 PC 未变，但翻译 PC 的页表变了，所以新页表也必须有这个 VA 的映射。

### 4.7 `hal/riscv/hal_kvec.S` — 内核异常向量

```
kernelvec:
  ① sp -= 256 (开栈帧)
  ② 保存 caller-saved: ra, gp, tp, t0-t2, a0-a7, t3-t6
  ③ call kerneltrap()          ← C 函数
  ④ 恢复 caller-saved (跳过 tp)
  ⑤ sp += 256
  ⑥ sret
```

### 4.8 `hal/riscv/hal_plic.c` — PLIC 驱动

```
hal_irq_init():          设 UART+VIRTIO0/1/2 的优先级为 1
hal_irq_hart_init():     使能 UART+VIRTIO 中断，设阈值 0
hal_irq_claim():         return PLIC_SCLAIM(hart)
hal_irq_complete(irq):   PLIC_SCLAIM(hart) = irq
```

### 4.9 `hal/riscv/hal_uart.c` — 16550a UART 驱动

完整实现，包括：
- **TX 锁机制：** `tx_lock` spinlock + `tx_busy` 标志 + `sleep(&tx_chan)/wakeup` 实现阻塞输出
- **中断驱动接收：** ISR 确认 + 读取所有可用字符 → 调用 `handler(c)`
- **`hal_putchar()`:** 轮询输出（等待 LSR TX 空闲 → 写 THR），用于 printf
- **初始化：** 关中断 → 设波特率 38400 → 8N1 → FIFO → 开中断

### 4.10 `hal/riscv/hal_virtio.c` — Virtio MMIO 磁盘驱动

**架构：** MMIO 寄存器映射到 `VIRTIO0/1/2` 物理地址，使用标准 virtio 协议。

**核心数据结构：** `struct virtio_disk` — descriptor ring (16 个 entry), available ring, used ring, 空闲链表, 进行中的 I/O 跟踪。

**关键函数：**
- `virtio_disk_init()`: 重置设备 → 协商特性 → 配置队列 → 分配并注册 DMA 内存 → 设置状态 DRIVER_OK
- `virtio_disk_rw(buf, write)`: 构建 3 个 descriptor (header + data + status) → 提交到 available ring → 通知设备 → 等待完成
- `virtio_disk_intr()`: 确认中断 → 处理 used ring 中所有已完成项 → 唤醒等待进程

---

## 五、LoongArch 平台实现

### 5.1 `hal/loongarch/arch.h` — 核心架构定义（484 行）

**CSR 寄存器映射（宏定义 + 内联函数 50+ 个）：**

| CSR | 地址 | 名称 | 用途 |
|-----|------|------|------|
| CRMD | 0x0 | Current Mode | PLV[1:0], IE[2], DA[3], PG[4] |
| PRMD | 0x1 | Previous Mode | PPLV[1:0], PIE[2] |
| ECFG | 0x4 | Exception Config | LIE[12:0] |
| ESTAT | 0x5 | Exception Status | IS[12:0], Ecode[21:16] |
| ERA | 0x6 | Exception Return Addr | = sepc |
| BADV | 0x7 | Bad Virtual Addr | = stval |
| EENTRY | 0xC | Exception Entry | = stvec |
| PGDL | 0x19 | Page Table (low half) | = satp (低半区) |
| PGDH | 0x1A | Page Table (high half) | (高半区，LA 独有) |
| PGD | 0x1B | Combined PGD | 只读，自动选 PGDL/PGDH |
| CPUID | 0x20 | Core ID | = mhartid |
| TCFG | 0x41 | Timer Config | 周期模式配置 |
| TVAL | 0x42 | Timer Value | 倒计时值 |
| TICLR | 0x44 | Timer Intr Clear | 清定时器中断 |
| DMW0/1 | 0x180/1 | Direct Mapping Window | 恒等映射窗口 |
| TLBRENTRY | 0x88 | TLB Refill Entry | 软件 TLB refill 入口 |

**异常码转换（关键适配）：**

```c
// estato_scause() 将 LoongArch 异常码转为 RISC-V scause 值
// 使 trap.c 无需修改
static inline uint64 estat_to_scause(uint64 estat) {
    uint64 ecode = (estat >> 16) & 0x3F;
    switch(ecode) {
        case EXCCODE_SYS: return 8;    // syscall
        case EXCCODE_PIL: return 13;   // load page fault
        case EXCCODE_PIS: return 15;   // store page fault
        case EXCCODE_PIF: return 12;   // instr page fault
        // ... 10 种异常码映射
    }
}
```

**sstatus 兼容层：**

```c
// PRMD.PPLV → SSTATUS_SPP, PRMD.PIE → SSTATUS_SPIE
// 让 trap.c 中的 (r_sstatus() & SSTATUS_SPP) 判断继续工作
static inline uint64 r_sstatus_compat(void) { ... }
static inline void   w_sstatus_compat(uint64 x) { ... }
```

**中断控制（通过 CRMD.IE 位）：**

```c
static inline void intr_on()  { w_crmd(r_crmd() | CRMD_IE); }
static inline void intr_off() { w_crmd(r_crmd() & ~CRMD_IE); }
```

**LoongArch 页表格式（与 RISC-V 完全不同）：**

```
PTE 位图 (64-bit):
 63      62    61      60...52  51...12    11...7   6...0
 RPLV    NX    NR      保留     PPN[47:0]  PPN[52:48]+SW  attr(V,D,PLV,MAT,G,P,W)

权限模型：默认可读+可执行（NR=0,NX=0,RPLV=0）
"可写"需要同时设 D(bit1) + HW_W(bit8)
"不可读"需要设 NR(bit61)
"不可执行"需要设 NX(bit62)

兼容层：
  #define PTE_R  (0)                        // 默认可读
  #define PTE_W  (PTE_D | PTE_HW_W)        // = 0x102
  #define PTE_X  (0)                        // NX=0 即可执行
  #define PTE_U  PTE_PLV3                   // PLV=3
  #define PTE_V_CACHE (PTE_V | PTE_MAT | PTE_P)  // = 0x91
```

**权限编码/解码函数（LA 独有）：**

```c
// hal_pte_encode_perm: 通用 PTE_R/W/X/U → LA 原生 PTE
// 通过 negate 方式：可读 → 不设 NR, 不可读 → 设 NR
static inline uint64 hal_pte_encode_perm(int perm) { ... }

// hal_pte_decode_perm: LA PTE → 恢复通用权限位 (供 uvmcopy)
static inline int hal_pte_decode_perm(pte_t pte) { ... }
```

**PA2PTE/PTE2PA（简单掩码）：**

```c
#define PA2PTE(pa)  ((uint64)(pa) & 0x0FFFFFFFFFFF000ULL)
#define PTE2PA(pte) ((uint64)(pte) & 0x0FFFFFFFFFFF000ULL)
// PPN 保持在原位 [59:12]，低 12 位清零
// 依赖 xv6 物理内存 < 256MB → PPN[52:48]=0
```

**4 级页表：**

```c
#define PT_LEVELS  4                    // 9-9-9-9-12
#define MAXVA      (1ULL << 38)         // 与 RISC-V Sv39 一致
#define MAKE_SATP(pagetable) ((uint64)(pagetable))  // 纯 PA，无模式位
```

### 5.2 `hal/loongarch/memlayout.h` — 物理内存布局

```
QEMU -machine virt (LS7A 芯片组):

0x00000000  低端 RAM (256 MB)
0x00400000  内核 .data/.bss（RAM 中）
0x07C00000  PHYSTOP (0x00400000 + 124 MB)
0x09000000  RAMDISK #1 (fs.img, 2 MB)
0x0A000000  RAMDISK #2 (ext2.img, 8 MB)
0x0B000000  RAMDISK #3 (fat32.img, 10 MB)
0x0FE00000  EIOINTC (中断控制器)
0x10000000  VIRTIO0 PCI（通过 PCI 枚举访问）
0x1C000000  KERNBASE (flash, 内核代码)
0x1C009000  TRAMPOLINE (低地址，DMW0 可达)
0x1FE001E0  UART0 (IRQ 31)

内核栈 (高地址，避开 DMW0):
  KSTACK_TOP             = 0xFFFFFFFFFFFFF000      (VA[63]=1)
  KSTACK(p)              = KSTACK_TOP - (p*3+2)*PGSIZE
  KSTACK_REGION_BOTTOM   = KSTACK_TOP - 192*PGSIZE
  → 每进程 3 页（2映射 + 1guard），64 进程共 192 页

地址验证:
  hal_pagetable_va_valid(va) = 
    (va < MAXVA) || (va >= KSTACK_REGION_BOTTOM && va < KSTACK_TOP)
```

**关键差异：** TRAMPOLINE 是低物理地址（0x1C009000），通过 DMW0 直接访问。TRAPFRAME 保持高 VA 值但不实际用于访问（通过 KSave1 CSR 访问 trapframe）。内核栈放在高地址（VA[63]=1），DMW0 不覆盖，实现了真正的 guard page 保护。

### 5.3 `hal/loongarch/hal_entry.S` — 启动入口

```
_entry: (链接到 0x1C000000)
  ① sp = stack0 + ((cpuid + 1) * 4096)  ← la.abs + csrrd CPUID
  ② bl start                             ← 跳到 start()
  ③ b spin                                ← 自旋（不应返回）
```

**对比 RISC-V：** LoongArch 无 M 态，`_entry` 直接运行在 PLV0（内核态）。使用 `csrrd $t1, 0x20`（CPUID）替代 RISC-V 的 `csrr a1, mhartid`。

### 5.4 `hal/loongarch/hal_start.c` — 平台初始化

**`start()` 流程：**
- Hart 0:
  1. `tp = r_cpuid()`（保存 hart ID）
  2. 直接 MMIO 写 UART 输出 "start: begin"
  3. 复制 .data 从 flash LMA → RAM VMA (0x00400000)
  4. 清零 .bss
  5. 写 `boot_done` 魔数（通知 secondary harts）
  6. 输出 "start: calling main"
- Secondary harts: 自旋等 `boot_done`
- 所有 harts: `w_eentry((uint64)kernelvec)`, 调用 `main()`

**`timerinit()`：**
1. `ECFG |= ECFG_LIE_TIMER`（使能定时器中断）
2. `w_ticlr(1)`（清除 pending 中断）
3. `TCFG = (10M << 2) | PERIOD | EN`（~10ms 周期模式）

**对比 RISC-V：** LoongArch 不需要 mret 特权级切换，不需要 PMP 配置，但需要手动复制 .data 段（因为代码在 flash 中，数据段需复制到 RAM）。

### 5.5 `hal/loongarch/hal_swtch.S` — 上下文切换

```
hal_switch(a0=old, a1=new):
  保存 12 个寄存器 → *old:  ra(0), sp(8), fp(16), s0(24), ..., s8(88)
  恢复 12 个寄存器 ← *new:  同 offset
  jirl $r0, $ra, 0            ← PC = ra（不保存返回地址）
```

**对比 RISC-V：** 保存 12 个寄存器（vs 14 个）。`jirl $r0, $ra, 0` 等价于 RISC-V 的 `ret`。

### 5.6 `hal/loongarch/hal_tramp.S` — 用户↔内核跳板

**关键差异（对比 RISC-V trampoline）：**

| 方面 | RISC-V | LoongArch |
|------|--------|-----------|
| trapframe 获取方式 | sscratch CSR | KSave1 CSR (0x31) |
| 用户 a0 保存 | sscratch | KSave0 CSR (0x30) |
| 用户 PC 来源 | sepc CSR | ERA CSR (0x6) |
| 页表切换 | csrw satp, t1 + sfence.vma | csrwr PGDL, t1（无 fence，内核走 DMW0） |
| 全局指针 | gp 寄存器已保存 | gp 不存在，填 $r0 |
| 浮点寄存器 | 不保存 | 不保存 |
| 返回用户态 | sret | ertn |
| 是否需要用户页表映射 | 是（TRAMPOLINE+TRAPFRAME） | 否（DMW0+KSave1 覆盖） |

**uservec 流程：**
1. `csrwr $a0, 0x30` — 保存用户 a0 → KSave0
2. `csrrd $a0, 0x31` — 获取 trapframe KVA ← KSave1
3. 保存全部用户寄存器到 trapframe (通过 DMW0 直访)
4. 恢复用户 a0 → 保存到 trapframe
5. 保存 ERA → trapframe->epc
6. 加载 kernel_sp, kernel_trap, kernel_satp → sp, t0, t1
7. `csrwr $t1, 0x19` — 切换到内核页表 (PGDL)
8. 跳转到 usertrap()

**userret 流程：**
1. `csrwr $a0, 0x19` — 切换到用户页表 (PGDL)
2. `invtlb 0, $r0, $r0` — 刷新全部 TLB
3. 从 trapframe 恢复用户寄存器 (通过 DMW0 直访)
4. `ertn` — 返回用户态

### 5.7 `hal/loongarch/hal_kvec.S` — 内核异常向量（含栈溢出检测）

**独有特性：内核栈溢出检测。**

分 3 个阶段：

**Phase 0 — 保存检测寄存器：**
```
csrwr $t0, 0x33    → KSave3 = t0
csrwr $t1, 0x34    → KSave4 = t1
```

**Phase 1 — ESTAT 检测：**
```
csrrd $t0, ESTAT → 提取 Ecode → 与 PIL(1)/PIS(2) 比较
→ 非 PIL/PIS → 跳到正常路径
```

**Phase 2 — BADV 检测：**
```
csrrd $t0, BADV → 检查 [KSTACK_REGION_BOTTOM, KSTACK_TOP)
→ 命中 → 确认栈溢出！
  → 保存坏 sp → KSave4
  → sp = stack0 + (cpuid+1)*4096   ← 紧急栈
  → a0 = 坏 sp, a1 = BADV
  → 调用 hal_stack_overflow_panic() ← 不返回
→ 未命中 → 跳到正常路径
```

**正常路径：**
1. 恢复 t0, t1 从 KSave3/4
2. sp -= 160（开栈帧）
3. 保存 17 个 caller-saved 寄存器（ra, tp, a0-a7, t0-t8）
4. `la.abs $t0, kerneltrap; jirl $ra, $t0, 0` — 调用 C 函数
5. 恢复 17 个寄存器（跳过 tp）
6. sp += 160
7. `ertn`

### 5.8 `hal/loongarch/hal_tlbrefill.S` — 软件 TLB 重填（★ LA 独有）

**RISC-V 无此文件——RISC-V 硬件自动 walk Sv39 页表。**

**入口：** .tlbrefill 段，地址 0x1C008000，由 TLBRENTRY CSR 指向。

**完整流程：**

1. **保存 t0:** `csrwr $t0, TLBRSAVE`
2. **设页大小:** `TLBREHI.PS = 12` (4KB)
3. **VA 合法性检查:**
   - 用户态地址: `2*PGSIZE ≤ VA < MAXVA`
   - 内核态高地址: `KSTACK_REGION_BOTTOM ≤ VA < KSTACK_TOP`
   - 否则 → `.Ltlbr_invalid`
4. **选页表根:**
   ```
   读 TLBRBADV → 检查 bit 63
   bit63=0 → csrrd 0x19 (PGDL)
   bit63=1 → csrrd 0x1A (PGDH)
   保存到 KSave5 (0x35)
   ```
5. **四级 lddir walk:**
   ```
   Level 3: lddir $t0, KSave5, 3 → read PTE[VA[47:39]]
            检查 V → 重新读根表 → lddir 3 → addi -1 (clear V → Level-2 PA)
   Level 2: lddir $t0, $t0, 2 → read PTE[VA[38:30]]
            同上模式
   Level 1: lddir $t0, $t0, 1 → read PTE[VA[29:21]]
            同上模式
   Level 0: ldpte $t0, 0 → load even PTE → TLBRELO0
            ldpte $t0, 1 → load odd PTE  → TLBRELO1
   ```
6. **tlbfill:** 写入 TLB
7. **恢复 t0, ertn:** 重新执行引发 TLB miss 的指令

**无效处理：** 填 V=0 的 TLB 项 → ertn → 硬件发现 V=0 → PIL/PIS → kernelvec/usertrap。

**lddir 指令：** LoongArch 专用硬件指令，自动从 TLBREHI.VPPN 计算索引偏移，并发起内存读取。软件只需提供当前级页表的物理地址和 level 号。

### 5.9 `hal/loongarch/hal_intr.c` — EIOINTC 驱动

**EIOINTC 寄存器布局（0x0FE00000+:）**

| 偏移 | 寄存器 | 功能 |
|------|--------|------|
| 0x0000 | ISR[hart] | 64-bit pending 位图（per core） |
| 0x0400 | CORE_EN[hart] | Per-core 使能 |
| 0x1000 | IRQ_EN[irq/32] | 全局 IRQ 使能（分组） |
| 0x1400 | IPMAP[irq] | IRQ 路由到指定核心（单字节） |

**函数实现：**

- `hal_irq_init()`: 使能 UART IRQ 31 在 IRQ_EN，路由到核心 0
- `hal_irq_hart_init()`: 使能本核心在 CORE_EN，使能外部中断在 ECFG_LIE_HWI
- `hal_irq_claim()`: 确认 ESTAT_IS_HWI pending → 读 64-bit ISR → 扫描找第一个 set bit → 写 1 清除 → 返回 IRQ 号
- `hal_irq_complete(irq)`: 空函数（已在 claim 时清除）

**对比 RISC-V PLIC：** EIOINTC 用位图代替单个 claim 寄存器，每个核心有独立 ISR。清除方式为写 1 到对应位。不需要 completion。

### 5.10 `hal/loongarch/hal_uart.c` — 16550a 串口驱动

- `hal_console_init()`: 关中断 → 设波特率 38400 (DLL/DLM) → 8N1 → 开 FIFO → 开 RX 中断
- `hal_console_write(buf, n)`: 循环调用 `hal_putchar()`（无锁/无 sleep）
- `hal_putchar(c)`: LSR 轮询（等 TX 空闲 → 写 THR）
- `hal_console_intr(handler)`: 读 ISR → 循环读 RBR → handler(c)

**对比 RISC-V UART：** LoongArch 版本无 TX 锁和阻塞 sleep 语义，其余逻辑一致（LSR 轮询、FIFO 初始化、中断配置）。

### 5.11 `hal/loongarch/hal_virtio.c` — RAM-disk 替代

**原因：** LoongArch QEMU 的 virtio 是 PCI 设备，直接 PCI 枚举超出教学 OS 范围。替代方案是利用 QEMU 的 `-device loader` 将文件系统镜像提前加载到预留的低端 RAM 窗口。

**三个 RAM 磁盘（每个 16MB）：**

| 磁盘 | 物理地址 | 镜像 |
|------|---------|------|
| dev=1 | 0x09000000 | fs.img (xv6fs) |
| dev=2 | 0x0A000000 | ext2.img |
| dev=3 | 0x0B000000 | fat32.img |

**关键函数：**

- `virtio_disk_rw(b, write)`: 持 per-disk spinlock → `memmove` 在 `b->data` 和 RAM 窗口之间 → 立即设 `b->disk = 0`（同步完成，无 DMA、无中断）
- `virtio_disk_intr()`: 空函数（无中断驱动）

**对比 RISC-V virtio：** RAM-disk 是纯同步 memcpy，无 virtio 协议、无 descriptor ring、无 DMA、无中断。I/O 立即完成，代价是每次读写期间占满 CPU。

---

## 六、关键架构差异对照表

| 维度 | RISC-V | LoongArch |
|------|--------|-----------|
| **特权级** | M/S/U 三级 | PLV0(内核)/PLV3(用户) 两级 |
| **启动方式** | M 态 → start() → mret → S 态 main() | 直接 PLV0 → start() → main() |
| **异常返回** | sret/mret | ertn（统一指令） |
| **异常 PC** | sepc CSR | ERA CSR |
| **异常原因** | scause CSR (固定编码) | ESTAT CSR (需转换为 RISC-V scause) |
| **异常地址** | stval CSR | BADV CSR |
| **异常向量** | stvec CSR | EENTRY CSR |
| **页表基址** | satp (含 MODE+ASID+PPN) | PGDL(低)+PGDH(高)，纯 PA |
| **页表 walk** | 硬件自动 Sv39 (3 级) | 软件 lddir/ldpte + tlbfill (4 级) |
| **TLB flush** | sfence.vma | invtlb 0, $r0, $r0 |
| **直接映射** | 无 | DMW0 (VA[63:60]=0 → PA=VA, PLV0 专用) |
| **有效 VA** | 39 位 (Sv39 固定) | 48 位 (VALEN=48, RVACFG=8→40 位) |
| **PTE V 位** | bit0 | bit0 |
| **PTE 权限** | R/W/X/U 正向授权 (bits1-4) | NR/NX 负向禁止 + D/HW_W 组合写 |
| **PTE MAT/P 位** | 无 | MAT[4:5], P[7] (缓存+物理存在) |
| **PA2PTE** | `(pa>>12)<<10` (PPN 移位) | `pa & ~0xFFF` (PPN 原位) |
| **kalloc 可分配** | 0x80000000+ ~128MB | 0x00420000~0x07C00000 (~120MB) |
| **KERNBASE** | 0x80000000 (RAM) | 0x1C000000 (flash) |
| **UART 地址** | 0x10000000 (IRQ 10) | 0x1FE001E0 (IRQ 31) |
| **中断控制器** | PLIC (0x0C000000) | EIOINTC (0x0FE00000) |
| **定时器** | stimecmp (Sstc, one-shot) | TCFG/TVAL/TICLR (periodic) |
| **磁盘** | Virtio MMIO (0x10001/2/3000) | RAM-disk (0x0900/0A00/0B00000) |
| **TRAMPOLINE VA** | MAXVA-PGSIZE (~0x3FFFFF000) | 0x1C009000 (低地址,DMW0) |
| **TRAPFRAME 访问** | 用户 VA TRAPFRAME (需映射) | KSave1 CSR (内核VA,DMW0) |
| **用户页表映射** | 需要 TRAMPOLINE+TRAPFRAME | 不需要（DMW0+KSave1 替代） |
| **内核栈 VA** | TRAMPOLINE 下方 (~MAXVA 附近) | 高地址 0xFFFF... (DMW0 不覆盖) |
| **栈 guard** | 传统 guard page | ★ 硬件级 PIL/PIS（真正保护） |
| **context 寄存器** | 14 个 | 12 个 |
| **CPU 空闲** | wfi | idle 0 |
| **启动栈复制** | 不需要 | 需要（flash→RAM） |
| **软件 TLB refill** | 无 | hal_tlbrefill.S |
| **栈溢出检测** | 无 | hal_kvec.S 内建 |

---

## 七、系统启动流程与 HAL 调用链

### 7.1 启动流程（hart 0 路径）

```
硬件上电
  │
  ▼
hal_entry.S: _entry
  │ sp = stack0 + (hartid+1)*4096
  ▼
hal_start.c: start()
  │ RISC-V: M 态初始化（MPP, PMP, 定时器）→ mret
  │ LA:     复制 .data (flash→RAM), 清零 .bss, 等 boot_done
  ▼
main.c: main()
  │
  ├─ consoleinit()       → hal_console_init()     ← HAL: UART 初始化
  ├─ printfinit()
  ├─ kinit()             → kalloc/kfree (物理内存池)
  │
  ├─ kvminit()           → kvmmake()
  │   └─ proc_mapstacks() → kvmmap → mappages → walk()
  │                         └─ hal_pagetable_va_valid() ← HAL: LA 接受高地址栈
  │
  ├─ kvminithart()       ★ HAL: 开启 MMU
  │   ├─ w_satp(kernel_pagetable)     → PGDL
  │   ├─ w_pgdh(kernel_pagetable)     → PGDH (仅 LA)
  │   ├─ sfence_vma / invtlb          → hal_tlb_flush_all()
  │   ├─ w_dmw0(0x11)                 → DMW0 配置 (仅 LA)
  │   ├─ PWCL/PWCH 配置                → lddir 参数 (仅 LA)
  │   ├─ w_rvacfg(8)                  → 40 位有效 VA (仅 LA)
  │   ├─ TLBRENTRY = tlb_refill_entry  → TLB refill 入口 (仅 LA)
  │   └─ CRMD.PG=1                    → 启动页面映射 (仅 LA)
  │
  ├─ procinit()
  ├─ trapinit() / trapinithart()
  │   └─ hal_write_stvec(kernelvec)   ← HAL: 设异常向量
  │
  ├─ hal_irq_init()                   ← HAL: 中断控制器初始化
  ├─ hal_irq_hart_init()              ← HAL: per-hart 中断使能
  │
  ├─ binit() / iinit() / vfs_init() / fileinit()
  ├─ virtio_disk_init()               ← HAL: 磁盘驱动初始化
  ├─ hal_timer_init()                 ← HAL: 启动定时器中断
  │
  ├─ userinit() → allocproc()         → 创建 pid=1, context.ra=forkret
  │
  └─ scheduler()                      → 永不返回
       └─ hal_switch(&c->context, &p->context) ← HAL: 首次切入进程
```

### 7.2 启动流程（secondary hart 路径）

```
硬件上电 → _entry → start()

hart 1/2/...:
  │ 等 boot_done 魔数
  ▼
main() → else 分支:
  ├─ kvminithart()       ★ HAL: per-core MMU 启动
  ├─ trapinithart()      ★ HAL: per-core 异常向量
  ├─ hal_irq_hart_init() ★ HAL: per-core 中断使能
  └─ scheduler()         → 进入调度循环
```

### 7.3 运行时陷阱路径

```
用户代码 → 异常
  │
  ├─ RISC-V: stvec → TRAMPOLINE (用户页表必须有映射)
  └─ LA: 硬件自动 PLV0 → EENTRY → TRAMPOLINE (DMW0 直接访问)
  │
  ▼
hal_tramp.S: uservec
  ├─ 保存用户寄存器 → trapframe (RISC-V: 页表访问; LA: KSave1+DMW0)
  ├─ 加载 kernel_sp, kernel_satp
  ├─ 切换页表 (RISC-V: csrw satp; LA: csrwr PGDL)
  └─ 跳转 trap.c: usertrap()
  │
  ├─ 系统调用 → syscall()
  ├─ 设备中断 → devintr()
  │   ├─ UART → hal_console_intr(consoleintr)
  │   └─ 磁盘 → virtio_disk_intr()
  ├─ 定时器 → yield() → sched() → hal_switch() → scheduler
  └─ 缺页 → vmfault() → kalloc + mappages
  │
  ▼
prepare_return() → 写 trapframe kernel_sp/satp/trap
  │
  ▼
hal_tramp.S: userret
  ├─ 切换用户页表 (RISC-V: csrw satp; LA: csrwr PGDL + invtlb)
  ├─ 恢复用户寄存器 ← trapframe
  └─ sret / ertn → 用户态
```

### 7.4 内核态异常路径

```
内核代码执行中 → 异常
  │
  ▼
hal_kvec.S: kernelvec
  ├─ LA: Phase 0-2 栈溢出检测
  │   ├─ 读取 ESTAT/BADV → 判断是否为 guard page
  │   └─ 是 → 切换紧急栈 → hal_stack_overflow_panic()
  └─ 正常路径:
      ├─ 保存 caller-saved → 栈
      ├─ 调用 kerneltrap() (trap.c)
      └─ 恢复 → ertn/sret
```

---

## 八、各子系统 HAL 调用关系图

### 8.1 CPU 控制子系统

```
内核代码 (trap.c, proc.c, vm.c)
  │
  ├─ hal_get_hartid()        → r_tp() (两架构相同)
  ├─ hal_intr_on/off/get()   → RISC-V: SSTATUS_SIE
  │                          → LA: CRMD.IE
  ├─ hal_read_scause()       → RISC-V: r_scause()
  │                          → LA: estat_to_scause(r_estat()) ★
  ├─ hal_read_stval()        → RISC-V: r_stval()
  │                          → LA: r_badv()
  ├─ hal_read/write_sepc()   → RISC-V: r/w_sepc()
  │                          → LA: r/w_era()
  ├─ hal_read/write_stvec()  → RISC-V: r/w_stvec()
  │                          → LA: r/w_eentry()
  ├─ hal_read/write_satp()   → RISC-V: r/w_satp()
  │                          → LA: r/w_pgdl()
  └─ hal_read_sstatus()      → RISC-V: r_sstatus()
                              → LA: r_sstatus_compat() (PRMD→sstatus) ★
```

### 8.2 内存管理子系统

```
vm.c:
  walk() → hal_pagetable_va_valid() ★
    ├─ RISC-V: va < MAXVA
    └─ LA: va < MAXVA || va ∈ [KSTACK_REGION_BOTTOM, KSTACK_TOP)

  mappages() → hal_pte_encode_perm() ★
    ├─ RISC-V: 恒等映射
    └─ LA: R/W/X/U → NR/NX/D/HW_W 编码转换

  uvmcopy() → PTE_FLAGS() → hal_pte_decode_perm() ★
    └─ LA: PTE 恢复为通用权限位

  kvminithart() → hal_tlb_flush_all() ★
    ├─ RISC-V: sfence.vma
    └─ LA: invtlb 0, $r0, $r0

  hal_tlb_flush_all() ☆☆
    └─ 用户态返回时由 userret 调用
```

### 8.3 中断子系统

```
trap.c: devintr()
  │
  ├─ hal_irq_claim()         → RISC-V: PLIC_SCLAIM(hart)
  │                          → LA: EIOINTC ISR 位图扫描
  │
  ├─ UART IRQ:
  │   └─ hal_console_intr(consoleintr)
  │       ├─ RISC-V: ISR 确认 + TX done 检查 + 读所有输入
  │       └─ LA: ISR 确认 + 读所有输入
  │
  ├─ VIRTIO IRQ:
  │   └─ virtio_disk_intr()
  │       ├─ RISC-V: used ring 处理 + wakeup
  │       └─ LA: 空函数 (RAM-disk 无中断)
  │
  └─ hal_irq_complete(irq)   → RISC-V: PLIC_SCLAIM(hart) = irq
                              → LA: 空函数 (已在 claim 清除)
```

### 8.4 定时器子系统

```
trap.c: usertrap() / kerneltrap()
  │
  ├─ hal_get_time()           → RISC-V: r_time()
  │                           → LA: rdtime.d
  │
  └─ hal_set_timer(next)      → RISC-V: w_stimecmp(next)
                               → LA: w_ticlr(1) (周期模式，只清除)

启动路径:
  hal_timer_init()            → RISC-V: w_stimecmp(time + 1M)
                              → LA: timerinit() (TCFG 周期模式)
```

### 8.5 控制台子系统

```
printf() → hal_putchar(c)     → RISC-V: 等 LSR → 写 THR
                               → LA: 等 LSR → 写 THR

console.c: consolewrite()
  → hal_console_write(buf,n)  → RISC-V: tx_lock + sleep + 写 THR
                               → LA: 循环 hal_putchar

console.c: consoleintr(c)     ← 由 hal_console_intr 回调
```

### 8.6 上下文切换子系统

```
proc.c: scheduler()
  └─ hal_switch(&c->context, &p->context)  ☆☆ 调度器→进程

proc.c: sched()
  └─ hal_switch(&p->context, &c->context)  ☆☆ 进程→调度器

hal_swtch.S: hal_switch()
  ├─ 保存 12/14 个 callee-saved 寄存器 → *old
  └─ 恢复 12/14 个 callee-saved 寄存器 ← *new
      └─ ret / jirl → PC = new_ra
```

### 8.7 LoongArch 独有的 HAL 组件

```
☆ TLB Refill (hal_tlbrefill.S):
  内核栈高地址访问 → TLB miss → TLBRENTRY → 软件 lddir walk
  → ldpte → tlbfill → ertn

☆ 栈溢出检测 (hal_kvec.S):
  内核栈写 guard 页 → PIL/PIS → kernelvec → Phase 0-2 检测
  → 紧急栈 → hal_stack_overflow_panic()

☆ DMW0 恒等映射 (kvminithart + arch.h):
  内核低地址访问 → VA[63:60]=0,PLV=0 → VA=PA → 无需 TLB

☆ PGDH 高地址页表 (kvminithart + arch.h):
  内核高地址栈访问 → VA[63]=1 → PGDH → lddir walk → TLB

☆ RAM-disk (hal_virtio.c):
  memmove 同步访问预加载镜像 → 无需 virtio 协议
```

---

## 九、内核通用代码如何使用 HAL

本章逐一分析 `kernel/` 下每个文件如何使用 HAL 定义的接口、宏和类型。HAL 的使用密度不同：`trap.c` 和 `vm.c` 是重度用户（27+ 处），`proc.c` 中等（20+ 处），`bio.c`、`fs.c`、`spinlock.c` 等完全不用 HAL。

### 9.1 `kernel/trap.c` — 陷阱处理（HAL 最密集，35+ 处）

`trap.c` 是整个内核中 HAL 调用最密集的文件——它需要读取异常原因、设置异常向量、控制中断、操作定时器、处理设备中断，几乎涵盖了 HAL 的所有子系统。

#### 9.1.1 异常/中断 CSR 访问

```c
// usertrap() — 用户态异常入口

// ① 读异常返回地址（RISC-V: sepc, LA: ERA）
p->trapframe->epc = hal_read_sepc();

// ② 读异常原因（RISC-V: scause, LA: estat→scause 转换）
if(hal_read_scause() == 8) {   // 系统调用
    hal_intr_on();              // 开中断
    syscall();
}

// ③ 缺页异常 — 读故障地址 + 原因
else if((hal_read_scause() == 15 || hal_read_scause() == 13 || hal_read_scause() == 12) &&
          vmfault(p->pagetable, hal_read_stval(), ...) != 0) {
    // 懒分配成功处理
}

// ④ 错误诊断 — 打印异常信息
printf("usertrap(): unexpected scause 0x%lx pid=%d\n", hal_read_scause(), p->pid);
printf("            sepc=0x%lx stval=0x%lx\n", hal_read_sepc(), hal_read_stval());
```

```
★ 关键适配：hal_read_scause() 在 LoongArch 上不是简单的 CSR 读取
  RISC-V: 直接 return r_scause()
  LA:     return estat_to_scause(r_estat())
          ↑ 内部转换表: EXCCODE_SYS(11)→8, EXCCODE_PIL(1)→13...
          trap.c 完全不知道底层是 scause 还是 ESTAT
```

#### 9.1.2 陷阱向量切换

```c
// prepare_return() — 返回用户态前

hal_intr_off();                                    // 关中断
hal_write_stvec(TRAMPOLINE + (uservec - trampoline)); // 设用户态入口
p->trapframe->kernel_satp = hal_read_satp();        // 保存内核页表
p->trapframe->kernel_sp = p->kstack + 2 * PGSIZE;   // 保存内核栈顶
p->trapframe->kernel_hartid = hal_get_hartid();     // 保存 hart ID
```

```
调用链:
  hal_write_stvec() → RISC-V: w_stvec(), LA: w_eentry()
  hal_read_satp()   → RISC-V: r_satp(),  LA: r_pgdl()
  hal_get_hartid()  → RISC-V: r_tp(),   LA: r_tp()
```

#### 9.1.3 sstatus 兼容（LA 关键适配）

```c
// usertrapret() — 保存/恢复 sstatus
uint64 sstatus = hal_read_sstatus();   // ← LA: PRMD → sstatus 格式转��
sstatus &= ~SSTATUS_SPP;
sstatus |= SSTATUS_SPIE;
hal_write_sstatus(sstatus);            // ← LA: sstatus 格式 → PRMD
```

```
★ LA 的 hal_read_sstatus() 不是读真实 CSR，而是读 PRMD 再转换：
  PRMD.PPLV=3(user) → SSTATUS_SPP=0
  PRMD.PPLV=0(kernel) → SSTATUS_SPP=1
  PRMD.PIE → SSTATUS_SPIE

  trap.c 继续使用 RISC-V 的 SSTATUS_SPP/SPIE 宏检查"异常前是否在用户态"—
  这个逻辑在 LA 上靠兼容层继续工作。
```

#### 9.1.4 设备中断和定时器

```c
// devintr() — 设备中断分发
int irq = hal_irq_claim();       // ← RISC-V: PLIC_SCLAIM, LA: EIOINTC ISR 扫描
if(irq == UART0_IRQ) {
    hal_console_intr(consoleintr); // 串口输入回调
} else if(irq == VIRTIO0_IRQ) {
    virtio_disk_intr();           // 磁盘中断
}
hal_irq_complete(irq);           // ← RISC-V: PLIC_SCLAIM=irq, LA: 空函数

// kerneltrap() — 定时器中断
hal_set_timer(hal_get_time() + 1000000);  // 设下一次时间中断
```

#### 9.1.5 HAL 符号使用总表

| HAL 符号 | 使用次数 | 使用位置 |
|----------|---------|---------|
| `hal_read_scause()` | 6 | usertrap, kerneltrap, devintr |
| `hal_read_sepc()` | 3 | usertrap, kerneltrap |
| `hal_read_stval()` | 3 | usertrap, kerneltrap |
| `hal_read_sstatus()` / `hal_write_sstatus()` | 4 | usertrapret |
| `hal_intr_on()` / `hal_intr_off()` / `hal_intr_get()` | 4 | usertrap, usertrapret, kerneltrap |
| `hal_write_stvec()` | 2 | usertrapret, trapinithart |
| `hal_read_satp()` | 1 | usertrapret |
| `hal_get_hartid()` | 1 | usertrapret |
| `hal_get_time()` / `hal_set_timer()` | 2 | kerneltrap |
| `hal_irq_claim()` / `hal_irq_complete()` | 2 | devintr |
| `hal_console_intr()` | 2 | devintr |
| `PGSIZE` | 1 | 计算 kernel_sp |
| `MAKE_SATP` | 1 | usertrapret 构造返回值 |
| `TRAMPOLINE` | 1 | 计算 uservec 地址 |

### 9.2 `kernel/vm.c` — 虚拟内存（HAL 密集，50+ 处）

`vm.c` 是页表操作的核心，大量使用 HAL 定义的 PTE 宏、页表级数和地址布局常量。**walk() 是唯一直接操作 PTE 结构的函数，其余所有函数都通过 walk() 间接访问页表。**

#### 9.2.1 内核页表构建

```c
// kvmmake() — 映射内核地址空间
kvmmap(kpgtbl, PGROUNDDOWN(UART0), PGROUNDDOWN(UART0), PGSIZE, PTE_R | PTE_W);
kvmmap(kpgtbl, PLIC, PLIC, 0x4000000, PTE_R | PTE_W);
kvmmap(kpgtbl, KERNBASE, KERNBASE, ..., PTE_R | PTE_X);
kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline, PGSIZE, PTE_R | PTE_X);
proc_mapstacks(kpgtbl);  // → kvmmap → mappages → walk → HAL 宏链
```

```
使用的 HAL 地址宏: UART0, PLIC, VIRTIO0/1/2, KERNBASE, PHYSTOP, TRAMPOLINE
使用的 HAL PTE 宏:  PTE_R, PTE_W, PTE_X, PTE_V_CACHE, PGSIZE, PGROUNDUP/DOWN
```

#### 9.2.2 walk() — 核心页表遍历（HAL 交汇点）

```c
// walk() 是 HAL 宏交汇最密集的函数
if(!hal_pagetable_va_valid(va))       // ★ HAL: LA 接受高地址栈，RV 只接受 <MAXVA
    panic("walk");

for(int level = PT_LEVELS - 1; ...) { // ★ HAL: RV=2, LA=3
    pte_t *pte = &pagetable[PX(level, va)]; // ★ HAL: PX 宏
    if(*pte & PTE_V) {                // ★ HAL: bit0 两架构恰好相同
        pagetable = PTE2PA(*pte);     // ★ HAL: RV 移位, LA 掩码
    } else {
        *pte = PA2PTE(pagetable) | PTE_V; // ★ HAL: RV 移位, LA 掩码
    }
}
return &pagetable[PX(0, va)];         // ★ HAL: PX(0, va)
```

#### 9.2.3 mappages() — 写入叶子 PTE

```c
*pte = PA2PTE(pa) | hal_pte_encode_perm(perm) | PTE_V_CACHE;
//    ↑ HAL          ↑ HAL (★ LA 权限编码)       ↑ HAL
```

#### 9.2.4 kvminithart() — MMU 启动（架构分叉点）

```c
// 两架构共享部分
w_satp(MAKE_SATP(kernel_pagetable));    // ★ HAL: RV=含MODE位, LA=纯PA

// LoongArch 独占部分（50+ 行）
w_pgdh(MAKE_SATP(kernel_pagetable));    // PGDH (LA 独有)
w_dmw0(0x0000000000000011ULL);         // DMW0 配置 (LA 独有)
w_rvacfg(8);                            // 40 位有效 VA (LA 独有)
// PWCL/PWCH 配置                         // lddir 参数 (LA 独有)
// TLBRENTRY 设置                         // TLB refill 入口 (LA 独有)
// CRMD.DA=0, CRMD.PG=1                  // 启动 MMU (LA 独有)
```

#### 9.2.5 walkaddr() — 用户 VA→PA（含独立防线）

```c
if(va >= MAXVA)  return 0;         // ★ HAL: 独立 MAXVA 防线
if((*pte & PTE_V) == 0) return 0;
if((*pte & PTE_U) == 0) return 0;  // U 位在 RV=0x10, LA=0xC
return PTE2PA(*pte);
```

#### 9.2.6 freewalk() — 叶子/中间 PTE 区分（架构差异）

```c
#ifdef ARCH_loongarch
    if(pte & PTE_MAT)  panic("freewalk: leaf");  // LA: MAT=1 → 叶子
#else
    if(pte & (PTE_R|PTE_W|PTE_X)) panic("freewalk: leaf"); // RV: 有权限位 → 叶子
#endif
```

#### 9.2.7 vm.c HAL 使用总表

| HAL 符号 | 使用次数 | 主要位置 |
|----------|---------|---------|
| `PGSIZE` | 15+ | 全文件范围 |
| `PGROUNDUP/DOWN` | 10+ | kvmmake, uvmalloc, uvmdealloc |
| `PTE_V` | 8 | walk, walkaddr, freewalk, uvmcopy |
| `MAXVA` | 6 | walkaddr, copyout, copyin, copyinstr, vmfault |
| `PTE2PA` | 6 | walk, uvmunmap, freewalk, uvmcopy |
| `PA2PTE` | 4 | walk, mappages |
| `PTE_R/W/X/U` | 12+ | kvmmake, uvmalloc, vmfault, uvmclear |
| `PT_LEVELS` | 1 | walk |
| `PX` | 2 | walk |
| `PTE_V_CACHE` | 1 | mappages |
| `PTE_FLAGS` | 1 | uvmcopy |
| `hal_pte_encode_perm()` | 1 | mappages |
| `hal_pagetable_va_valid()` | 1 | walk |
| `MAKE_SATP` | 3 | kvminithart |

### 9.3 `kernel/proc.c` — 进程管理（20+ 处）

#### 9.3.1 内核栈映射

```c
// proc_mapstacks()
uint64 va = KSTACK((int)(p - proc));     // ★ HAL: LA=高地址 0xFFFF..., RV=近MAXVA
kvmmap(kpgtbl, va, pa, PGSIZE, PTE_R | PTE_W);
kvmmap(kpgtbl, va + PGSIZE, pa2, PGSIZE, PTE_R | PTE_W);
```

```c
// procinit()
p->kstack = KSTACK((int)(p - proc));     // ★ HAL: 记录栈起始 VA
```

```c
// allocproc()
p->context.sp = p->kstack + 2*PGSIZE;    // ★ HAL: 初始 sp = 栈顶
```

#### 9.3.2 用户页表管理

```c
// proc_pagetable() — 架构分叉
// RISC-V: 映射 TRAMPOLINE + TRAPFRAME 到用户页表
// LA: 不映射（DMW0 + KSave1 替代）
mappages(pagetable, TRAMPOLINE, PGSIZE, trampoline, PTE_R | PTE_X);
mappages(pagetable, TRAPFRAME,  PGSIZE, p->trapframe, PTE_R | PTE_W);
```

#### 9.3.3 调度器

```c
// scheduler()
hal_intr_on();                             // 开中断
hal_switch(&c->context, &p->context);     // ★ HAL: 切换到进程
hal_cpu_idle();                            // ★ HAL: RV=wfi, LA=idle 0

// sched()
if(hal_intr_get()) panic("sched interruptible"); // ★ HAL: 中断状态检查
hal_switch(&p->context, &c->context);     // ★ HAL: 切回调度器

// forkret()
uint64 satp = MAKE_SATP(p->pagetable);
trampoline_userret = TRAMPOLINE + (userret - trampoline);
```

#### 9.3.4 proc.c HAL 使用总表

| HAL 符号 | 使用位置 |
|----------|---------|
| `KSTACK` | proc_mapstacks, procinit |
| `PGSIZE`, `PTE_R`, `PTE_W` | proc_mapstacks, allocproc |
| `TRAMPOLINE`, `TRAPFRAME` | proc_pagetable, proc_freepagetable |
| `hal_switch()` | scheduler (×2), sched (×1) |
| `hal_intr_on/off/get` | scheduler, sched |
| `hal_get_hartid()` | cpuid |
| `hal_cpu_idle()` | scheduler |
| `MAKE_SATP` | forkret |
| `USER_TOP` | growproc |

### 9.4 `kernel/main.c` — 启动入口（4 处 HAL）

```c
hal_irq_init();           // 初始化 PLIC/EIOINTC
hal_irq_hart_init();      // per-hart 中断使能
hal_timer_init();         // 启动定时器
// secondary harts:
hal_irq_hart_init();      // 重做 per-hart 中断使能
```

`main.c` 不直接碰 CSR——所有平台相关工作委托给 HAL。

### 9.5 `kernel/console.c` — 控制台 I/O（4 处 HAL）

```c
hal_console_init();         // 初始化 UART 硬件
hal_putchar(c);             // 单字符输出（printf 路径）
hal_putchar('\b');          // 退格处理（3 次调用）
hal_console_write(buf, n);  // 批量输出
```

`console.c` 不调用任何 UART MMIO 地址——完全通过 HAL console 接口访问。

### 9.6 `kernel/exec.c` — 程序加载（10+ 处 HAL）

```c
// ELF 加载 — 使用 HAL PTE 权限宏
uvmalloc(pagetable, ..., PTE_X);       // 代码段：可执行
uvmalloc(pagetable, ..., PTE_W);       // 数据段：可写

// 用户栈 guard
mappages(pagetable, sp-PGSIZE, PGSIZE, ..., PTE_R); // guard 页：无 U 位
uvmclear(pagetable, sp-PGSIZE);                       // 显式清 U 位

// ELF 段加载 — 使用 PGSIZE
for(i = 0; i < filesz; i += PGSIZE) { ... }
if(offs + PGSIZE > filesz) { ... }
```

### 9.7 `kernel/sysproc.c` 和 `kernel/sysfile.c`（各 1 处 HAL）

```c
// sysproc.c: sbrk()
if(addr + n > USER_TOP)   // ★ HAL: 用户空间上限

// sysfile.c
if(n >= PGSIZE)           // ★ HAL: 页大小限制
```

### 9.8 `kernel/kalloc.c` — 物理内存分配（3 处 HAL）

```c
// kinit() / kalloc() / kfree()
if((uint64)pa % PGSIZE != 0 || (char*)pa < end || (uint64)pa >= PHYSTOP)
    panic("kfree");
```

### 9.9 不使用 HAL 的内核文件

以下文件完全不引用 HAL 宏/函数——它们是"纯内核逻辑"，与硬件无关：

| 文件 | 原因 |
|------|------|
| `bio.c` | 磁盘块缓存，操作内存中的链表，不访问硬件 |
| `fs.c` | 文件系统逻辑层，通过 bio 层访问磁盘 |
| `syscall.c` | 系统调用路由，只调用其他函数 |
| `spinlock.c` | 自旋锁，使用 `__sync_*` 内置原子操作 |
| `sleeplock.c` | 睡眠锁，在自旋锁上构建 |
| `printf.c` | 格式化输出，通过 console HAL 输出字符 |
| `string.c` | 纯数据操作 |

---

## 十、HAL 在内核中的使用密度热力图

```
文件            HAL 引用次数    主要 HAL 子系统
─────────────────────────────────────────────────────
trap.c         35+ inlines     CPU+中断+定时器+控制台+IRQ+内存
vm.c           50+ macros      PTE格式+页表+内存布局+MMU
proc.c         20+ mixed       上下文切换+栈布局+中断+定时器
main.c         4               中断+定时器
console.c      4               控制台
exec.c         10+ macros      PTE权限
kalloc.c       3               PGSIZE,PHYSTOP
sysproc.c      1               USER_TOP
sysfile.c      1               PGSIZE
bio.c          0               —
fs.c           0               —
syscall.c      0               —
spinlock.c     0               —
sleeplock.c    0               —
printf.c       0               (通过 console HAL 输出)
string.c       0               —
```

---

## 十一、HAL 接口覆盖度验证

以下逐项验证 HAL 声明→平台实现→内核调用的完整链路：

### 11.1 CPU 控制接口

```
声明 (hal_arch.h)          RISC-V 实现 (arch.h)     LA 实现 (arch.h)          内核调用 (trap.c/proc.c)
────────────────────────────────────────────────────────────────────────────────────────
hal_get_hartid()           r_tp()                   r_tp()                    trapinithart, cpuid
hal_intr_on()              set SIE                  set CRMD.IE               syscall, scheduler
hal_intr_off()             clear SIE                clear CRMD.IE             usertrapret
hal_intr_get()             read SIE                 read CRMD.IE              sched
hal_read_scause()          r_scause()               estat_to_scause()         6 处 in trap.c
hal_read_stval()           r_stval()                r_badv()                  vmfault, 诊断
hal_read/write_sepc()      r/w_sepc()               r/w_era()                 usertrap, kerneltrap
hal_read/write_sstatus()   r/w_sstatus()            PRMD ↔ sstatus 转换        usertrapret
hal_read/write_stvec()     r/w_stvec()              r/w_eentry()              trapinithart, usertrapret
hal_read/write_satp()      r/w_satp()               r/w_pgdl()                kvminithart, usertrapret
hal_cpu_idle()             wfi                      idle 0                    scheduler
```

### 11.2 定时器接口

```
声明 (hal_timer.h)         RISC-V 实现              LA 实现                    内核调用
─────────────────────────────────────────────────────────────────────────────────────
hal_get_time()             r_time()                 rdtime.d                   kerneltrap
hal_set_timer()            w_stimecmp(next)         w_ticlr(1)                kerneltrap
hal_timer_init()           w_stimecmp(time+1M)      timerinit()               main.c
```

### 11.3 中断控制器接口

```
声明 (hal_intr.h)          RISC-V 实现 (plic.c)     LA 实现 (intr.c)           内核调用
─────────────────────────────────────────────────────────────────────────────────────
hal_irq_init()             PLIC 设优先级             EIOINTC 使能+路由           main.c
hal_irq_hart_init()        PLIC 使能+阈值            EIOINTC 核心使能+ECFG       main.c(×2)
hal_irq_claim()            PLIC_SCLAIM              EIOINTC ISR 位图扫描        devintr
hal_irq_complete()         PLIC_SCLAIM=irq          空函数                      devintr
```

### 11.4 内存管理接口

```
宏/函数                     RISC-V                    LA                          内核调用
─────────────────────────────────────────────────────────────────────────────────────
PA2PTE(pa)                 (pa>>12)<<10             pa & 0x0FFFFFFF...000        walk, mappages
PTE2PA(pte)                (pte>>10)<<12            pte & 0x0FFFFFFF...000       walk, walkaddr, uvmcopy
PTE_V/CACHE                相同 (bit0)               相同 + MAT/P 位              walk, mappages, vmfault
PTE_R/W/X/U                bits 1-4                 R=0, W=D+HW_W, X=0, U=PLV3  kvmmake, uvmalloc, vmfault
PT_LEVELS                  3                        4                           walk
PX(level,va)               相同公式，不同 level 范围                               walk
MAXVA                      1L<<(27+12-1)            1ULL<<38                     walkaddr, copyout, vmfault
KSTACK(p)                  TRAMPOLINE-(p+1)*3*4096  KSTACK_TOP-(p*3+2)*4096     proc_mapstacks, procinit
TRAMPOLINE                 MAXVA-PGSIZE              0x1C009000                  proc_pagetable, kvmmake
TRAPFRAME                  TRAMPOLINE-PGSIZE         MAXVA-2*PGSIZE              proc_pagetable
KSTACK_REGION_BOTTOM       无定义                    0xFFFFFFFFFFFF3000          hal_pagetable_va_valid
hal_pagetable_va_valid()   va < MAXVA                va<MAXVA ∥ va∈[BOTTOM,TOP) walk
hal_pte_encode_perm()      恒等                      通用→NR/NX/D/HW_W编码        mappages
hal_pte_decode_perm()      恒等                      NR/NX/D/HW_W→通用            uvmcopy
```

### 11.5 控制台接口

```
声明 (hal_console.h)       RISC-V (uart.c)           LA (uart.c)                 内核调用
─────────────────────────────────────────────────────────────────────────────────────
hal_console_init()         FIFO+波特率+中断          空函数                       consoleinit
hal_console_write()        tx_lock+sleep+写THR      循环 hal_putchar             consolewrite
hal_putchar()              等 LSR→写THR              等 LSR→写THR              printf, consolewrite
hal_console_intr()         ISR确认+读RBR+handler     ISR确认+读RBR+handler       devintr
```

---

## 十二、从内核视角看 HAL：完整调用链图

```
┌─────────────────────────────────────────────────────────────────────┐
│                         kernel/ 通用代码                             │
│                                                                     │
│  main.c        trap.c            vm.c             proc.c            │
│  ───────       ───────           ─────            ───────           │
│  启动调度       异常处理           页表管理          进程调度           │
│                                                                     │
│  console.c     exec.c           kalloc.c         sysproc.c          │
│  ────────      ──────           ────────         ─────────          │
│  串口I/O       ELF加载           物理内存          sbrk边界           │
│                                                                     │
└──────────┬──────────┬──────────┬──────────┬────────────────────────┘
           │          │          │          │
           ▼          ▼          ▼          ▼
┌──────────────────────────────────────────────────────────────────────┐
│                    HAL 公共接口层 (hal/hal_*.h)                        │
│                                                                      │
│  hal_arch.h  hal_intr.h  hal_timer.h  hal_console.h  hal_vm.h       │
│  18个CPU函数  4个IRQ函数  3个定时器函数  4个UART函数    1个TLB函数     │
│                                                                      │
│  hal_ctx.h                    hal_memlayout.h                        │
│  struct hal_context           HAL_ETEXT, HAL_END                     │
│  hal_switch()                                                        │
└──────────┬──────────────────────────────┬────────────────────────────┘
           │ ARCH=riscv                    │ ARCH=loongarch
           ▼                               ▼
┌──────────────────────┐      ┌──────────────────────────────────────┐
│  hal/riscv/          │      │  hal/loongarch/                      │
│  ─────────           │      │  ─────────────                       │
│  arch.h (CSR+PTE)    │      │  arch.h (CSR+PTE+兼容层)             │
│  memlayout.h (RV布局) │      │  memlayout.h (LA布局+DMW0+KSTACK)    │
│  hal_entry.S         │      │  hal_entry.S                         │
│  hal_start.c (M态)   │      │  hal_start.c (PLV0+.data复制)        │
│  hal_swtch.S (14reg) │      │  hal_swtch.S (12reg)                │
│  hal_tramp.S (双映射) │      │  hal_tramp.S (KSave1+DMW0)          │
│  hal_kvec.S          │      │  hal_kvec.S (+栈溢出检测3阶段)       │
│  hal_plic.c (PLIC)   │      │  ─ 无 ─ (无硬件walk)                 │
│  ─ 无 ─               │      │  hal_tlbrefill.S (软件4级lddir)     │
│                       │      │  hal_intr.c (EIOINTC)               │
│  hal_uart.c (TX锁)   │      │  hal_uart.c                         │
│  hal_virtio.c (MMIO) │      │  hal_virtio.c (RAM-disk)             │
│  kernel.ld (RV布局)  │      │  kernel.ld (flash布局)               │
└──────────────────────┘      └──────────────────────────────────────┘
```

---

## 附录：文件统计

| 分类 | 文件数 (RISC-V) | 文件数 (LoongArch) | 总行数（估） |
|------|----------------|-------------------|-------------|
| 公共 HAL 头文件 | 8 | 8 | ~180 |
| 平台实现 | 11 | 12 | ~2800 |
| 合计 | 19 | 20 | ~2980 |

**注：** LoongArch 多出的 1 个文件是 `hal_tlbrefill.S`（软件 TLB refill），这是 LA 架构独有的需求。RAM-disk 替代 virtio 使 LoongArch 的 `hal_virtio.c` 远比 RISC-V 版本精简。

---

*最后更新：2026-07-28*
