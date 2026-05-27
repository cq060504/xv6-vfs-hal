# xv6-riscv HAL 跨平台移植项目

## 项目概述

本项目的目标是为 xv6-riscv 第5版引入**硬件抽象层（HAL）**，使其能够在不同的处理器架构（RISC-V、LoongArch 等）上运行，而无需修改内核通用代码。

### 核心价值
- 将平台相关代码与内核通用逻辑解耦
- 建立统一的跨平台接口
- 为多架构适配奠定基础

---

## 项目阶段与时间表

### 📅 总体计划：8周

| 周次 | 目标 | 关键交付物 |
|---|---|---|
| **第1周** | 深度理解原版 xv6-riscv | 平台相关代码清单 + 物理地址布局图 |
| **第2周** | 在 RISC-V 上建立 HAL 框架 | RISC-V HAL 骨架，系统仍可运行 |
| **第3周** | 完成 RISC-V HAL 全部接口 | 完整的 RISC-V 实现，全测例通过 |
| **第4周** | 搭建 LoongArch 开发环境 | 工具链、QEMU、最小启动代码 |
| **第5周** | LoongArch HAL（CPU/内存/串口） | 能进入 main() 并完成初始化 |
| **第6周** | LoongArch HAL（中断/定时器/切换） | 完整中断和调度支持 |
| **第7周** | 整合调试，通过测例 | 基本测例通过（fork、exec、pipe 等） |
| **第8周** | 收尾、优化、生成 diff | 代码清理、文档、最终回归测试 |

---

## 平台相关文件清单

### 一、完全平台相关（必须 HAL 化）

| 文件 | 平台相关原因 | HAL 方案 |
|---|---|---|
| `kernel/riscv.h` | RISC-V CSR 寄存器定义、页表 PTE 格式 | → `hal/hal_cpu.h` + `hal/hal_vm.h` |
| `kernel/memlayout.h` | PLIC/CLINT/UART 物理地址、KERNBASE/PHYSTOP 布局 | → `hal/hal_memlayout.h` |
| `kernel/entry.S` | 启动汇编（mhartid、栈初始化、入口地址） | → `hal/riscv/entry.S` |
| `kernel/start.c` | M 态初始化（CSR 操作） | → `hal/riscv/hal_start.c` |
| `kernel/trampoline.S` | 用户态/内核态陷入（sret、异常向量） | → `hal/riscv/trampoline.S` |
| `kernel/kernelvec.S` | 内核态中断向量 | → `hal/riscv/kernelvec.S` |
| `kernel/swtch.S` | 上下文切换（寄存器保存/恢复） | → `hal/riscv/swtch.S` |
| `kernel/plic.c` | RISC-V PLIC 中断控制器 | → `hal/riscv/hal_intr.c` |
| `kernel/vm.c` | Sv39 页表实现（walk、sfence.vma） | → `hal/riscv/hal_vm.c` |
| `kernel/trap.c` | 平台特定部分提取 | → 调用 HAL 接口 |
| `kernel.ld` | 链接脚本（OUTPUT_ARCH、入口、加载地址） | → `hal/riscv/kernel.ld` |

### 二、部分平台相关（需要接口抽象）

| 文件 | 依赖 | 方案 |
|---|---|---|
| `kernel/proc.c` | `r_tp()`、`cpuid()` | → 抽象 `hal_cpuid()` |
| `kernel/uart.c` | 16550a 寄存器、基地址 | → 通过 `hal_memlayout.h` 提供基址 |
| `kernel/console.c` | 调用 uart 函数 | → 通过 HAL 的串口接口 |
| `kernel/main.c` | 调用平台初始化函数 | → 调用 `hal_init()` 等入口 |

### 三、平台无关（无需修改）

`bio.c`, `fs.c`, `file.c`, `log.c`, `pipe.c`, `syscall.c`, `sysproc.c`, `sysfile.c`, `proc.h`（结构体定义）, `spinlock.c`, `types.h`, `param.h`

---

## HAL 接口设计

### 目录结构

```
xv6-riscv/
├── kernel/                 ← 通用内核代码
├── hal/
│   ├── hal.h              ← 统一 HAL 接口声明
│   ├── hal_arch.h         ← CPU 控制接口
│   ├── hal_intr.h         ← 中断控制器接口
│   ├── hal_timer.h        ← 定时器接口
│   ├── hal_vm.h           ← 页表/MMU 接口
│   ├── hal_memlayout.h    ← 内存布局常量（平台相关值）
│   ├── hal_console.h      ← 控制台 I/O 接口
│   ├── riscv/
│   │   ├── hal_cpu.c
│   │   ├── hal_intr.c     ← PLIC 实现
│   │   ├── hal_timer.c
│   │   ├── hal_vm.c       ← Sv39 实现
│   │   ├── hal_uart.c
│   │   ├── entry.S
│   │   ├── trampoline.S
│   │   ├── kernelvec.S
│   │   ├── swtch.S
│   │   ├── hal_memlayout.h
│   │   └── kernel.ld
│   └── loongarch/         ← LoongArch 实现
│       ├── hal_cpu.c
│       ├── hal_intr.c     ← EIOINTC 实现
│       ├── ...
└── Makefile               ← ARCH=riscv|loongarch 选择
```

### 核心 HAL 接口声明（hal/hal.h）

```c
#ifndef HAL_H
#define HAL_H

#include <stdint.h>

/* ---- CPU 控制 ---- */
void hal_intr_on(void);         // 开中断
void hal_intr_off(void);        // 关中断
int  hal_intr_get(void);        // 获取中断状态
int  hal_cpuid(void);           // 当前 CPU 核心号
void hal_cpu_halt(void);        // 停机

/* ---- 定时器 ---- */
void hal_timer_init(void);
void hal_timer_set_next(void);

/* ---- 中断控制器 ---- */
void hal_plic_init(void);
void hal_plic_init_hart(void);
int  hal_plic_claim(void);
void hal_plic_complete(int irq);

/* ---- 内存/MMU ---- */
typedef uint64_t pte_t;
typedef uint64_t *pagetable_t;

pagetable_t hal_vm_create(void);
void hal_vm_free(pagetable_t pt, int level);
int  hal_vm_map(pagetable_t pt, uint64_t va, uint64_t pa, int perm);
void hal_vm_switch(pagetable_t pt);
void hal_tlb_flush(void);

/* ---- 串口 ---- */
void hal_uart_init(void);
void hal_uart_putc(char c);
int  hal_uart_getc(void);

/* ---- 启动 ---- */
void hal_start(void);

#endif
```

---

## 关键技术对照

### RISC-V vs LoongArch

| 功能 | RISC-V | LoongArch |
|---|---|---|
| **关中断** | `csrci sstatus, SIE` | `csrxchg r0, t0, CRMD` |
| **获取核心ID** | `csrr a0, mhartid` | `cpucfg rd, 0x5` 或 `CSR.CPUID` |
| **设置异常向量** | `csrw stvec, t0` | `csrwr t0, EENTRY` |
| **页表基址** | `csrw satp, t0` | `csrwr t0, PGDL` |
| **TLB 刷新** | `sfence.vma` | `invtlb 0, r0, r0` |
| **定时器** | CLINT（MMIO） | `CSR.TCFG` / `CSR.TVAL` |
| **中断控制器** | PLIC（MMIO） | EIOINTC / LIOINTC |
| **特权级** | M/S/U 三级 | PLV0/PLV3 两级 |
| **页表格式** | Sv39（3级，9-9-9-12） | 类似但 PTE 格式不同 |

---

## 开发原则

### 🎯 核心守则

1. **第2周只搬家不改逻辑**  
   将平台代码移入 HAL，保持系统可运行，不重构逻辑。

2. **渐进式开发**  
   每周完成一个清晰的可验证的目标，避免大爆炸式改动。

3. **持续验证**  
   每个阶段用 `make qemu` 验证，确保功能不丧失。

4. **接口优先**  
   先定义 HAL 接口，再分别实现 RISC-V 和 LoongArch。

5. **代码注释**  
   这是教学 OS，注释要充分（尤其是 HAL 层汇编）。

---

## 依赖关系与参考资源

### 环境需求

- **RISC-V**：已有 QEMU + riscv64-unknown-elf-gcc
- **LoongArch**：需安装 QEMU 7.1+ 和 loongarch64-linux-gnu-gcc

### 参考文档

- [LoongArch 参考手册](https://github.com/loongson/LoongArch-Documentation)
- [Linux kernel arch/loongarch](https://git.kernel.org/pub/scm/linux/kernel/git/torvalds/linux.git/tree/arch/loongarch)（很好的参考实现）
- [RISC-V 特权架构规范](https://riscv.org/specifications/privileged-isa/)
- xv6-riscv第5版源码：https://github.com/mit-pdos/xv6-riscv/releases/tag/xv6-riscv-rev5
- 参考项目1：2025年操作系统全国赛内核赛道"静春山"团队的xv6项目：https://gitlab.eduxiji.net/educg-group-36002-2710490/T202510558995330-264
- 参考项目2：2025年操作系统全国赛内核赛道"RuOK"团队的xv6项目：https://gitlab.eduxiji.net/educg-group-36002-2710490/T202510486995232-2402
- xv6-riscv-book：https://github.com/mit-pdos/xv6-riscv-book
- The RISC-V Instruction Set Manual：https://riscv.org/technical/specifications

---

## 当前状态与下一步

### ✅ 已完成（第3-4周过渡：HAL 接口统一）

#### 最终 RISC-V HAL 代码规模（共 19 个文件，1561 行）

**HAL 接口头文件（hal/，8 个文件，179 行）**

| 文件 | 行数 | 说明 |
|---|---|---|
| `hal/hal.h` | 30 | 统一入口，条件编译选择平台 + 聚合子系统头文件 |
| `hal/hal_arch.h` | 38 | CPU 控制接口（CSR 读写、核心 ID、中断使能） |
| `hal/hal_intr.h` | 14 | 中断控制器接口（init/hart_init/claim/complete） |
| `hal/hal_timer.h` | 17 | 定时器接口（get_time/set_timer/timer_init） |
| `hal/hal_vm.h` | 18 | 虚拟内存接口（TLB 刷新 + PTE 常量引用） |
| `hal/hal_memlayout.h` | 17 | 内存布局抽象（内核镜像边界符号） |
| `hal/hal_console.h` | 14 | 控制台 I/O 接口（同步/异步输出、输入、中断回调） |
| `hal/hal_ctx.h` | 31 | 上下文切换（hal_context 结构体 + hal_switch 声明） |

**RISC-V 平台实现（hal/riscv/，11 个文件，1382 行）**

| 文件 | 行数 | 说明 |
|---|---|---|
| `arch.h` | 388 | CSR 内联函数（r_/w_前缀）+ PTE 宏 + 19 个 hal_ 内联包装 |
| `memlayout.h` | 59 | RISC-V virt 机器物理地址布局（UART0/PLIC/KERNBASE 等） |
| `hal_entry.S` | 21 | 启动入口（.text.entry 段，设栈 → 跳 start） |
| `hal_start.c` | 71 | M 态启动（start）+ timerinit + hal_timer_init 包装 |
| `hal_plic.c` | 54 | PLIC 中断控制器 + 4 个 hal_irq_* 包装 |
| `hal_uart.c` | 165 | 16550a UART 驱动 + 5 个 hal_console_* 包装 |
| `hal_virtio.c` | 326 | Virtio 磁盘驱动 |
| `hal_swtch.S` | 42 | 上下文切换汇编 + hal_switch 别名 |
| `hal_tramp.S` | 149 | 用户态陷入/返回（trampoline） |
| `hal_kvec.S` | 62 | 内核态中断向量 |
| `kernel.ld` | 45 | RISC-V 链接脚本（入口 0x80000000） |

**内核层修改（kernel/，7 个文件）**

| 文件 | 变更说明 |
|---|---|
| `main.c` | `plicinit/plicinithart` → `hal_irq_init/hal_irq_hart_init` |
| `trap.c` | 全部 CSR/PLIC/UART/定时器调用改用 hal_ 前缀（~30 处） |
| `console.c` | `uartputc_sync/uartinit` → `hal_putchar_sync/hal_console_init` |
| `proc.c` | `swtch` → `hal_switch`，`r_tp` → `hal_get_hartid`，`intr_*` → `hal_intr_*` |
| `proc.h` | `struct context` → `#include "hal/hal_ctx.h"` + `struct hal_context` |
| `defs.h` | 前向声明和 swtch 签名改用 `struct hal_context` |
| `hal_ctx.h` | 修正 `hal_switch` 第一个参数从双指针改为单指针 |

#### Bug 修复记录

**#1 hal_get_hartid 特权级错误**
- **症状**：内核静默崩溃，无任何输出
- **根因**：`hal_get_hartid()` 调用 `r_mhartid()` → `csrr mhartid`。`mhartid` 是 M 态 CSR，`start()` 执行 `mret` 切换到 S 态后无法访问，触发非法指令异常
- **修复**：改为调用 `r_tp()`（读 `tp` 寄存器）。`start()` 在 M 态时将 hartid 写入 `tp`，S 态通过 `tp` 获取
- **教训**：HAL 包装函数必须注意 CSR 的特权级访问权限。`tp` 寄存器跨特权级可用，是传递 hartid 的正确方式

#### 当前 HAL 接口覆盖清单

| HAL 声明 | RISC-V 实现 | 类型 | 内核已迁移 |
|---|---|---|---|
| `hal_get_hartid()` | `r_tp()` | 内联 | ✅ |
| `hal_read_scause()` 等 13 个 CSR 函数 | `r_*()/w_*()` | 内联 | ✅ trap.c |
| `hal_intr_on/off/get()` | `intr_on/off/get()` | 内联 | ✅ proc.c, trap.c |
| `hal_irq_init/hart_init/claim/complete()` | `plic*()` | C 函数 | ✅ main.c, trap.c |
| `hal_get_time/set_timer()` | `r_time/w_stimecmp()` | 内联 | ✅ trap.c |
| `hal_timer_init()` | `timerinit()` | C 函数 | 就绪，start() 用原名 |
| `hal_tlb_flush_all()` | `sfence_vma()` | 内联 | 就绪，vm.c 用原名 |
| `hal_console_init/putchar_sync/putchar/getchar/console_intr()` | `uart*()` | C 函数 | ✅ console.c, trap.c |
| `hal_switch()` | `swtch`（汇编） | ASM | ✅ proc.c |

> LoongArch 移植时只需实现以上 hal_ 前缀函数，内核通用代码无需任何修改。


### 🚀 下一步（第4周）
1. 搭建 LoongArch 开发环境
2. 安装 loongarch64 交叉编译工具链和 QEMU
3. 编写最小 LoongArch 启动代码

---

## 笔记与问题追踪

### 📋 设计决策

- **PTE 格式**：HAL 层定义 `typedef uint64_t pte_t`，RISC-V/LoongArch 各自定义常量宏（如 `PTE_V`、`PTE_R` 等）
- **CSR 访问**：通过 C 函数封装，避免内联汇编分散在内核代码中
- **多核启动**：RISC-V 用 `mhartid`，LoongArch 用 `CPUID` CSR，统一通过 `hal_cpuid()` 访问

### 🐛 已知难点

1. **LoongArch 文档缺乏**：官方文档相对稀缺，需参考 Linux 内核实现
2. **中断控制器差异大**：PLIC vs EIOINTC 架构完全不同，设计接口时需谨慎
3. **页表格式细节**：两架构 PTE 的保留位定义不同，容易踩坑

---

## 成功标准

### 第3周（RISC-V）
- ✅ `make ARCH=riscv qemu` 能启动并通过 usertests

### 第7周（LoongArch）
- ✅ `make ARCH=loongarch qemu` 能启动
- ✅ 通过基本测例（fork、exec、pipe、read/write）

### 第8周（最终）
- ✅ 两平台全部测例通过
- ✅ 代码行数合理（新增 HAL 代码尽量精简）
- ✅ 生成可用的 `.diff` 文件
- ✅ README 清晰说明编译和运行步骤

---

---
## Claude 工作规范
1.  **上下文优先**：所有代码修改必须以本文件和 `plan.md` 中的项目计划为准。
2.  **代码修改要求**：
    - 修改 xv6 内核代码前，必须先说明修改的原理和目的。
    - 所有修改必须附带可执行的编译/测试命令。
    - 优先保持和 xv6 原代码风格一致，添加必要的注释。
3.  **跨平台适配要求**：
    - 同时考虑 RISC-V 和 LoongArch 两个架构的差异。
    - 涉及架构相关的代码，必须明确标注平台兼容性。
4.  **输出格式**：
    - 代码块必须标明语言（如 `c` `makefile`）。
    - 重要的命令或配置，用 `>` 单独列出，方便复制执行。
*最后更新：2026-05-27 | 项目阶段：第3周收尾，HAL 接口命名统一完成（含 bug 修复），19 个 HAL 文件 1561 行，usertests 全通过。第4周：搭建 LoongArch 开发环境*
