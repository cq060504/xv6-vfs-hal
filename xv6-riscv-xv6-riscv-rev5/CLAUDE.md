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

### 最新进展（2026-06-03）

LoongArch 版本已经通过 `python3 test-xv6.py -q usertests`。此前阻塞的 `MAXVAplus` 已解决；最终验证是在恢复 `user/usertests.c` 原始 `MAXVA/TRAPFRAME` 检查后完成的。

关键结论：
- LoongArch 硬件可以支持更宽 VA，但本项目当前保持 xv6 原始用户地址契约：`MAXVA = 1 << 38`。
- `TRAPFRAME` 恢复为 `MAXVA - 2*PGSIZE`，并用 `USER_TOP = TRAPFRAME` 统一表达用户堆增长上限。
- LoongArch trap 路径不依赖用户页表映射高地址 `TRAPFRAME`；trapframe 由 `KSave1` 保存的内核地址访问，低 trampoline 通过 PLV0 DMW0 访问。
- `hal/loongarch/hal_tlbrefill.S` 在 `lddir/ldpte` 前拒绝 `VA >= MAXVA` 和 `VA < 2*PGSIZE`，避免高地址在 LoongArch TLB lookup/walk 中别名到低页。
- LoongArch `exec` 保留低 2 页非用户 guard，用户程序从 `0x2000` 链接运行，用于阻断高地址截断到低 VPPN 后命中用户数据。
- `kvminithart()` 写 `RVACFG=8` 作为硬件层辅助限制；xv6 测试语义仍由 `MAXVA`、C 路径边界检查和 TLB refill 检查共同保证。

详细调试记录见 `others/loongarch-usertests-maxva-fix.md`。

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


### 🚀 下一步（第4周）— 搭建 LoongArch 开发环境

#### 环境目标
- 在 WSL2 中用 **Docker** 搭建隔离的 LoongArch 开发环境（与宿主机 RISC-V 环境隔离）
- Docker 镜像基于 Ubuntu 24.04，通过 `apt` 安装 `gcc-loongarch64-linux-gnu` + `qemu-system-loongarch64`
- 通过 volume 挂载源码，宿主机编辑，容器内编译

#### Docker 快速启动
```bash
# 构建镜像
docker build -t xv6-loongarch -f Dockerfile.loongarch .

# 启动容器（挂载项目目录）
docker run -it --rm -v $(pwd):/xv6 xv6-loongarch /bin/bash

# 编译
make ARCH=loongarch
```

#### 第4周交付物
1. Dockerfile 编写并成功构建镜像
2. 交叉编译工具链可用验证
3. QEMU LoongArch virt 机器能启动
4. 最小汇编代码能向 UART 输出字符
5. Makefile `ARCH=loongarch` 分支可用
6. 确认 QEMU virt 机器内存布局和 UART 地址（UART0 = 0x1FE001E0）

---

## LoongArch 移植关键技术要点

### 架构核心差异（HAL 实现前必须理解）

| 维度 | RISC-V (rv64) | LoongArch (LA64) | HAL 影响 |
|---|---|---|---|
| **特权级** | M/S/U 三级 | PLV0(内核)/PLV3(用户) 两级 | 无需 M 态启动流程，start() 极简 |
| **CSR 指令** | `csrr/csrw/csrci/csrsi` | `csrrd/csrwr/csrxchg` | 重写所有 CSR 内联函数 |
| **页表 walk** | 硬件自动（Sv39 3级） | **软件 TLB 重填** | 最大差异！需实现 TLBRENTRY 处理函数 |
| **TLB 刷新** | `sfence.vma` | `invtlb 0, $r0, $r0` | hal_tlb_flush_all() 实现不同 |
| **异常返回** | `sret`/`mret` | `ertn`（统一一条指令） | trampoline.S/kvec.S 返回指令 |
| **中断控制器** | PLIC (MMIO) | EIOINTC (MMIO) | hal_intr.c 完全重写 |
| **定时器** | CLINT+stimecmp CSR | TCFG/TVAL CSR（纯CSR） | hal_timer.h 重新实现 |
| **virtio 磁盘** | MMIO virtio @0x10001000 | PCI virtio（通过 PCIe 总线） | hal_virtio.c 需要 PCI 枚举 |
| **Callee-saved** | ra, sp, s0-s11 (14个) | ra, sp, fp, s0-s8 (12个) | hal_ctx.h 结构体字段数不同 |

### 页表格式差异（重点）

```
RISC-V PTE[63:0]: |保留| PPN[53:10] | Flags[9:0] |
                        V(0) R(1) W(2) X(3) U(4) G(5) A(6) D(7)

LoongArch PTE[63:0]:
|NX_H|RPLV_H|RPLV|NX| PPN[59:12] |PPN0[11:7]|G|MAT|PLV|D|V|
 V(0) valid, D(1) dirty, PLV[3:2] 权限, MAT[5:4] 内存属性,
 G(6) global, NX(60) 不可执行, RPLV[61] 读写权限

关键：LoongArch PPN 分两段存储！
  PPN[47:0] → PTE[59:12]
  PPN[52:48] → PTE[11:7]
  PA2PTE/PTE2PA 宏需重写
```

### 软件 TLB 重填机制

LoongArch 的 MMU 在 TLB miss 时**不自动 walk 页表**，而是触发 TLB 重填异常，由内核软件完成 walk 和 TLB 填入。
- 入口 CSR: `TLBRENTRY` (0x88)，独立于 `EENTRY` (0xC)
- 填入指令: `tlbwr`(随机写), `tlbfill`(指定写)
- **简化策略（推荐）**：用 DMW (Direct Mapping Window) 映射内核空间，避免内核 TLB miss

### 异常码对照（trap.c 适配关键）

| 事件 | RISC-V scause | LoongArch ESTAT.Ecode |
|---|---|---|
| 系统调用 | `8` | `EXCCODE_SYS = 11 (0xB)` |
| 缺页/load | `13` | `EXCCODE_TLBL = 1` |
| 缺页/store | `15` | `EXCCODE_TLBS = 2` |
| 缺页/fetch | `12` | `EXCCODE_TLBI = 3` |
| 定时器中断 | `0x8000000000000005` | Ecode=0, IS[11]=1 |
| 外部中断 | `0x8000000000000009` | Ecode=0, IS[12]=1 |

> 推荐在 `hal_read_scause()` 中写 `estat_to_scause()` 转换函数，让 trap.c 无需修改。

### LoongArch 关键 CSR 速查

| CSR 名 | 编号 | 用途 | RISC-V 对应 |
|---|---|---|---|
| CRMD | 0x0 | 当前模式 (PLV+IE+PG+DA) | sstatus |
| PRMD | 0x1 | 异常前模式 (PPLV+PIE) | sstatus.SPP/SPIE |
| ECFG | 0x4 | 异常配置 (LIE[12:0]) | sie |
| ESTAT | 0x5 | 异常状态 (IS+Ecode+EsubCode) | scause+sip |
| ERA | 0x6 | 异常返回地址 | sepc |
| BADV | 0x7 | 异常虚拟地址 | stval |
| EENTRY | 0xC | 异常入口地址 | stvec |
| PGDL | 0x19 | 页表基址(低半地址) | satp |
| CPUID | 0x20 | 核心ID | mhartid |
| TCFG | 0x41 | 定时器配置 | (无—CLINT MMIO) |
| TVAL | 0x42 | 定时器当前值 | (无—CLINT MMIO) |
| TICLR | 0x44 | 定时器中断清除 | stimecmp(写) |
| TLBRENTRY | 0x88 | TLB重填入口 | (无—RISC-V硬件walk) |

### LoongArch HAL 实现文件清单（12个文件）

| # | 文件 | 难度 | 预计行数 | 周次 |
|---|---|---|---|---|
| 1 | `hal/loongarch/arch.h` | ⭐⭐⭐ | ~400 | 第5周 |
| 2 | `hal/loongarch/memlayout.h` | ⭐⭐ | ~60 | 第5周 |
| 3 | `hal/loongarch/hal_entry.S` | ⭐⭐ | ~30 | 第5周 |
| 4 | `hal/loongarch/hal_start.c` | ⭐⭐⭐ | ~80 | 第5周 |
| 5 | `hal/loongarch/hal_uart.c` | ⭐ | ~160 | 第5周 |
| 6 | `hal/loongarch/kernel.ld` | ⭐⭐ | ~50 | 第5周 |
| 7 | `hal/loongarch/hal_tramp.S` | ⭐⭐⭐⭐⭐ | ~200 | 第6周 |
| 8 | `hal/loongarch/hal_kvec.S` | ⭐⭐⭐ | ~70 | 第6周 |
| 9 | `hal/loongarch/hal_swtch.S` | ⭐⭐ | ~50 | 第6周 |
| 10 | `hal/loongarch/hal_intr.c` | ⭐⭐⭐⭐ | ~100 | 第6周 |
| 11 | `hal/loongarch/hal_virtio.c` | ⭐ | ~330 | 第7周 |
| 12 | `hal/loongarch/hal_timer.c`(可选) | ⭐⭐ | ~30 | 第6周 |

### LoongArch 移植分周路线图

| 周次 | 目标 | 验证标准 |
|---|---|---|
| **第4周** | 环境搭建 + 最小启动实验 | Docker 可用、QEMU 能启动、UART 能输出字符 |
| **第5周** | CPU+内存+串口 | `main()` 能输出、kinit 完成、进入 shell |
| **第6周** | 中断+定时器+调度 | 时钟中断触发、双进程切换、键盘输入响应 |
| **第7周** | 磁盘+文件系统+测例 | fork/exec/pipe 通过、usertests 基本通过 |

---

## 笔记与问题追踪

### 📋 设计决策

- **PTE 格式**：HAL 层定义 `typedef uint64_t pte_t`，RISC-V/LoongArch 各自定义常量宏（如 `PTE_V`、`PTE_R` 等）。LoongArch PPN 分两段存储，PA2PTE/PTE2PA 需重写。
- **CSR 访问**：通过 C 函数封装，避免内联汇编分散在内核代码中
- **多核启动**：RISC-V 用 `mhartid`，LoongArch 用 `CPUID` CSR，统一通过 `hal_get_hartid()` 访问
- **scause 兼容**：LoongArch 用 ESTAT 而非 scause。在 `hal_read_scause()` 中做 `estat_to_scause()` 转换，确保 trap.c 无需修改
- **上下文结构体**：RISC-V 14 字段 vs LoongArch 12 字段，需 `#ifdef ARCH_xxx` 条件编译 hal_ctx.h
- **TLB 策略**：先用 DMW 映射内核空间避免内核 TLB miss，第6周再实现完整软件 TLB refill handler
- **磁盘策略**：第5-6周用 ramdisk，第7周再实现 PCI virtio 磁盘驱动（避免 PCI 枚举复杂性阻塞早期开发）
- **Docker 隔离**：RISC-V 环境在宿主机，LoongArch 环境在 Docker 容器，通过 `ARCH=xxx` 切换

### 🐛 已知难点

1. **软件 TLB 重填**：LoongArch 最大差异。RISC-V 硬件自动 walk，LoongArch 需软件 `tlb_refill_handler`。先绕过——内核用 DMW，用户态第6周实现。
2. **中断控制器差异大**：PLIC vs EIOINTC 架构完全不同。EIOINTC 寄存器布局需参考 Linux 内核 `drivers/irqchip/irq-loongarch-eiointc.c` 和 QEMU `hw/intc/loongarch_extioi.c`。
3. **页表 PPN 分两段**：PA2PTE/PTE2PA 宏与 RISC-V 完全不同的移位逻辑，容易出错。
4. **PCI virtio 替代 MMIO virtio**：LoongArch QEMU virt 的磁盘是 PCI 设备而非 MMIO，需要 PCI 枚举或先用 ramdisk。
5. **QEMU -kernel 可能不工作**：LoongArch virt 可能期望 UEFI firmware 加载内核。可能需要 `-device loader` 或直接用 BIOS 模式。
6. **-mcmodel 差异**：RISC-V 用 `-mcmodel=medany`，LoongArch 裸机程序可能需要 `-mcmodel=normal`。
7. **CSR 指令 `csrxchg` 语义微妙**：推荐用读-改-写方式操作 CRMD，避免 `csrxchg` 的掩码陷阱。

---

## 成功标准

### 第3周（RISC-V）
- ✅ `make ARCH=riscv qemu` 能启动并通过 usertests

### 第7周（LoongArch）
- ✅ `make ARCH=loongarch qemu` 能启动
- ✅ 通过基本测例（fork、exec、pipe、read/write）
- ✅ `python3 test-xv6.py -q usertests` 通过

### 第8周（最终）
- ✅ 两平台 `usertests` 通过
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
*最后更新：2026-06-03 | 项目阶段：LoongArch `usertests -q` 已通过，`MAXVAplus` 阻塞已解决，详细记录见 `others/loongarch-usertests-maxva-fix.md`*

## vfs+hal测试
使用方式
   make ARCH=loongarch qemu（在 xv6-la 容器中）或 make qemu（在 xv6 容器中）
  启动后在 shell 中输入：

  ┌─────────────────────────┬────────────────────────────────────┐
  │          目的           │                命令                │
  ├─────────────────────────┼────────────────────────────────────┤
  │ xv6 自带测试            │ usertests -q                       │
  ├─────────────────────────┼────────────────────────────────────┤
  │ ext2 测试（顺序不能乱） │ ext2test2 回车，等完成后 ext2test1 │
  ├─────────────────────────┼────────────────────────────────────┤
  │ ext2 L4 测试            │ ext2test3（已知可能不过）          │
  └─────────────────────────┴────────────────────────────────────┘

  核心原则：init.c 只做环境准备，测试命令由用户在 shell 中自由选择执行。这样切换测试对象时完全不需要修改代码。
