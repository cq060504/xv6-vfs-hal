好的，我来为你系统地规划这个项目。内容较长，我会分几个部分详细讲解。

---

# xv6-riscv HAL 开发完整指南与八周计划

## 一、什么是硬件抽象层（HAL）？

**HAL（Hardware Abstraction Layer）** 是操作系统内核与具体硬件之间的一层"翻译官"。

在没有 HAL 的 xv6-riscv 里，内核代码直接写死了 RISC-V 的汇编指令、寄存器名称、中断控制器（PLIC）、定时器（CLINT）等硬件细节。这意味着你想在 LoongArch 上跑，就得把所有这些地方全部手动改一遍，极易出错。

引入 HAL 后，内核的通用逻辑（进程调度、内存管理、系统调用）只调用 HAL 提供的**统一接口**，比如 `hal_enable_interrupt()`、`hal_timer_init()`、`hal_tlb_flush()`，而每个平台各自实现这些接口。结构如下：

```
┌─────────────────────────────────────┐
│         内核通用代码（kernel/）       │  ← 不感知平台差异
├─────────────────────────────────────┤
│      HAL 接口层（hal/hal.h）         │  ← 统一的函数/宏定义
├──────────────────┬──────────────────┤
│  RISC-V 实现      │  LoongArch 实现  │  ← 平台相关代码
│ (hal/riscv/)     │ (hal/loongarch/) │
└──────────────────┴──────────────────┘
```

HAL 通常需要抽象的内容包括：

| 类别 | 具体内容 |
|---|---|
| CPU 控制 | 中断使能/关闭、特权级切换、核心ID获取 |
| 内存管理 | 页表格式、TLB 刷新、物理地址布局 |
| 中断控制器 | RISC-V 的 PLIC ↔ LoongArch 的 EIOINTC |
| 定时器 | RISC-V 的 CLINT ↔ LoongArch 的 CSR 定时器 |
| 串口/控制台 | UART 初始化和读写 |
| 启动入口 | `_entry` 汇编、内核栈设置、hart/core 启动 |
| 上下文切换 | 寄存器保存/恢复的汇编 |

---

## 二、xv6-riscv 第5版中需要 HAL 化的文件清单
```
● 我已经系统阅读了 xv6-riscv 的完整源代码。以下是详细的 HAL 化文件清单，比 plan.md 更全面：

  ---
  需要 HAL 化的完整文件清单

  一、完全平台相关（必须 HAL 化）

  ┌────────────────────┬───────────────────────────────────────────────────┬──────────────────────┐
  │        文件        │                    HAL 化原因                     │         方案         │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │                    │ 大量 RISC-V CSR                                   │ → hal/hal_cpu.h 和   │
  │ kernel/riscv.h     │ 寄存器定义（mstatus/sstatus/satp/stvec等）、页表  │ hal/hal_vm.h         │
  │                    │ PTE 格式、内联汇编读写函数                        │                      │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/memlayout.h │ PLIC/CLINT/UART                                   │ →                    │
  │                    │ 物理地址、KERNBASE/PHYSTOP/TRAMPOLINE 布局        │ hal/hal_memlayout.h  │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/entry.S     │ csrr mhartid 读核心ID，la sp, stack0，入口地址    │ → hal/riscv/hal_entr │
  │                    │ 0x80000000                                        │ y.S                  │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/start.c     │ mstatus/mepc/satp/medeleg/mideleg/sie/pmp/定时器  │ → hal/riscv/hal_star │
  │                    │ 全部是 CSR 操作                                   │ t.c                  │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/trampoline. │ csrw sscratch、csrw sepc、sret 等用户态陷入/返回  │ → hal/riscv/hal_tram │
  │ S                  │                                                   │ p.S                  │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/kernelvec.S │ 内核态中断向量，sret 返回                         │ →                    │
  │                    │                                                   │ hal/riscv/hal_kvec.S │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/swtch.S     │ RISC-V 寄存器保存/恢复（ra/sp/s0-s11）            │ → hal/riscv/hal_swtc │
  │                    │                                                   │ h.S                  │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/plic.c      │ RISC-V PLIC 中断控制器寄存器操作                  │ →                    │
  │                    │                                                   │ hal/riscv/hal_intr.c │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/trap.c      │ r_sepc()/r_scause()/r_sstatus()/r_satp()/w_stvec( │ → 拆分平台代码到     │
  │                    │ )                                                 │ HAL，保留通用逻辑    │
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/vm.c        │ walk() 的 Sv39                                    │ → hal/riscv/hal_vm.c │
  │                    │ 三级页表、sfence_vma()、w_satp()/MAKE_SATP()      │                      │    
  ├────────────────────┼───────────────────────────────────────────────────┼──────────────────────┤
  │ kernel/kalloc.c    │ PHYSTOP 依赖内存布局                              │ →                    │    
  │                    │                                                   │ 低度依赖，通过宏即可 │    
  └────────────────────┴───────────────────────────────────────────────────┴──────────────────────┘
                                                                                                       
  二、部分平台相关（需要接口抽象）                                                                     
  
  ┌──────────────────────┬─────────────────────────────────────────┬───────────────────────────────┐   
  │         文件         │               平台相关点                │           HAL 方案            │
  ├──────────────────────┼─────────────────────────────────────────┼───────────────────────────────┤   
  │ kernel/proc.c        │ r_tp() 获取核心ID，cpuid()              │ → 抽象 hal_get_cpuid()        │
  │                      │ 函数，KSTACK() 布局                     │                               │   
  ├──────────────────────┼─────────────────────────────────────────┼───────────────────────────────┤   
  │ kernel/console.c     │ 调用 uart*() 函数                       │ → 抽象                        │   
  │                      │                                         │ hal_putchar()/hal_getchar()   │   
  ├──────────────────────┼─────────────────────────────────────────┼───────────────────────────────┤   
  │ kernel/uart.c        │ UART0 基地址、16550a 寄存器操作（MMIO   │ → 通过 hal_memlayout.h 提供   │
  │                      │ 方式平台通用，地址平台相关）            │ UART 基址                     │   
  ├──────────────────────┼─────────────────────────────────────────┼───────────────────────────────┤
  │ kernel/virtio_disk.c │ VIRTIO0 基地址                          │ → 通过 hal_memlayout.h 提供   │   
  │                      │                                         │ MMIO 基址                     │   
  ├──────────────────────┼─────────────────────────────────────────┼───────────────────────────────┤
  │ kernel/main.c        │ 调用 plicinit/plicinithart 等平台初始化 │ → 调用 HAL 初始化接口         │   
  ├──────────────────────┼─────────────────────────────────────────┼───────────────────────────────┤   
  │ kernel/printf.c      │ 调用 consputc() 间接依赖 uart           │ → 无需 HAL，已通过 console    │
  │                      │                                         │ 抽象                          │   
  ├──────────────────────┼─────────────────────────────────────────┼───────────────────────────────┤
  │ kernel/kernel.ld     │ OUTPUT_ARCH("riscv")，入口              │ → 需要平台相关链接脚本        │   
  │                      │ _entry，加载地址 0x80000000             │                               │   
  └──────────────────────┴─────────────────────────────────────────┴───────────────────────────────┘
                                                                                                       
  三、平台无关（不需要 HAL 化）                                                                        
  
  ┌───────────────────────────────┬────────────────────────────────────────────────────────────────┐   
  │             文件              │                              原因                              │
  ├───────────────────────────────┼────────────────────────────────────────────────────────────────┤   
  │ kernel/bio.c, kernel/fs.c,    │ 纯文件系统逻辑                                                 │
  │ kernel/file.c, kernel/log.c   │                                                                │   
  ├───────────────────────────────┼────────────────────────────────────────────────────────────────┤   
  │ kernel/pipe.c,                │                                                                │   
  │ kernel/syscall.c,             │ 系统调用分发/参数处理                                          │   
  │ kernel/sysproc.c,             │                                                                │   
  │ kernel/sysfile.c              │                                                                │
  ├───────────────────────────────┼────────────────────────────────────────────────────────────────┤   
  │                               │ struct context                                                 │
  │ kernel/proc.h                 │ 定义虽含寄存器名，但结构体布局可跨平台——平台差异体现在 swtch.S │
  │                               │  的汇编实现中                                                  │
  ├───────────────────────────────┼────────────────────────────────────────────────────────────────┤
  │ kernel/spinlock.c,            │                                                                │
  │ kernel/sleeplock.c,           │ 纯 C 逻辑                                                      │   
  │ kernel/string.c               │                                                                │
  ├───────────────────────────────┼────────────────────────────────────────────────────────────────┤   
  │ kernel/types.h,               │ 类型定义和常量                                                 │
  │ kernel/param.h, kernel/stat.h │                                                                │   
  └───────────────────────────────┴────────────────────────────────────────────────────────────────┘
                                                                                                       
  四、与 plan.md 的差异说明                                                                            
  
  plan.md 列出的文件基本正确，但有几点需要补充和修正：                                                 
                  
  1. 缺少 kernelvec.S — 内核态中断向量也是完全 RISC-V 汇编（sret），必须 HAL 化                        
  2. 缺少 kernel.ld — 链接脚本的 OUTPUT_ARCH 和加载地址 0x80000000 是平台相关的
  3. virtio_disk.c — 基本平台无关，只是 VIRTIO0 地址通过 memlayout.h 宏提供即可                        
  4. proc.c — 平台相关程度较低，只需抽象 cpuid()（r_tp() 调用）                                        
  5. console.c/printf.c — 不需要 HAL，它们通过 uart*() 接口间接调用硬件                                
                                                                                                       
  五、建议的 HAL 接口分层                                                                              
                                                                                                       
  hal/hal.h                    ← 统一包含头                                                            
  hal/hal_arch.h               ← CPU 控制接口声明                                                      
  hal/hal_intr.h               ← 中断控制器接口声明                                                    
  hal/hal_timer.h              ← 定时器接口声明                                                        
  hal/hal_vm.h                 ← 页表/内存接口声明                                                     
  hal/hal_memlayout.h          ← 内存布局常量（各平台提供不同值）                                      
  hal/hal_console.h            ← 控制台 I/O 接口声明                                                   
                                                                                                       
  hal/riscv/                   ← RISC-V 实现                                                           
    hal_arch.c                 ← r_mstatus/w_mstatus/r_mhartid 等                                      
    hal_intr.c                 ← PLIC 实现（原 plic.c）                                                
    hal_timer.c                ← CLINT/sstc 实现                                                       
    hal_vm.c                   ← Sv39 页表、sfence_vma                                                 
    hal_entry.S                ← 启动入口                                                              
    hal_tramp.S                ← 用户态陷入/返回                                                       
    hal_kvec.S                 ← 内核态中断向量                                                        
    hal_swtch.S                ← 上下文切换                                                            
    hal_lds.ld                 ← 链接脚本                                                              
                                                                                                       
  hal/loongarch/               ← LoongArch 实现（未来）                                                
    hal_arch.c                                                                                         
    hal_intr.c                                                                                         
    ...                                                                                                
                                                                                                       
  这样改造后，kernel/ 目录下的代码不再直接包含 riscv.h，而是通过 hal/hal.h                             
  调用统一接口，实现平台无关性。                                                                       
                                                                                                       
✻ Sautéed for 51s                                                                                      
  
● Saved 1 memory · 1 team memory                                                                       
  ⎿  user.md     
  ⎿  hal-project.md   
```
现在我知道了这些文件需要hal化，下一步是不是该逐个文件阅读

## 三、HAL 的设计方案

### 推荐目录结构

```
xv6-riscv/
├── kernel/           ← 通用内核代码（尽量不动）
├── hal/
│   ├── hal.h         ← HAL 统一接口声明
│   ├── riscv/
│   │   ├── hal_cpu.c      ← CPU控制实现
│   │   ├── hal_intr.c     ← 中断控制器（PLIC）
│   │   ├── hal_timer.c    ← 定时器（CLINT）
│   │   ├── hal_vm.c       ← 页表/MMU
│   │   ├── hal_uart.c     ← 串口
│   │   ├── entry.S        ← 启动汇编
│   │   ├── trampoline.S   ← 陷入向量
│   │   ├── swtch.S        ← 上下文切换
│   │   └── memlayout.h    ← RISC-V 地址布局
│   └── loongarch/
│       ├── hal_cpu.c
│       ├── hal_intr.c
│       ├── hal_timer.c
│       ├── hal_vm.c
│       ├── hal_uart.c
│       ├── entry.S
│       ├── trampoline.S
│       ├── swtch.S
│       └── memlayout.h
└── Makefile          ← 根据 ARCH 变量选择编译哪套 hal/
```

### hal.h 接口示例

```c
// hal/hal.h
#ifndef HAL_H
#define HAL_H

#include <stdint.h>

/* ---- CPU 控制 ---- */
void hal_intr_on(void);         // 开中断
void hal_intr_off(void);        // 关中断
int  hal_intr_get(void);        // 获取当前中断状态
int  hal_cpuid(void);           // 当前 CPU 核心号
void hal_cpu_halt(void);        // 停机（panic 用）

/* ---- 定时器 ---- */
void hal_timer_init(void);
void hal_timer_set_next(void);  // 设置下一次时钟中断

/* ---- 中断控制器 ---- */
void hal_plic_init(void);
void hal_plic_init_hart(void);
int  hal_plic_claim(void);       // 获取待处理中断号
void hal_plic_complete(int irq); // 完成中断处理

/* ---- 内存/MMU ---- */
typedef uint64_t pte_t;
typedef uint64_t *pagetable_t;

pagetable_t hal_vm_create(void);
void hal_vm_free(pagetable_t pt, int level);
int  hal_vm_map(pagetable_t pt, uint64_t va, uint64_t pa, int perm);
void hal_vm_switch(pagetable_t pt);  // 切换页表（写 satp/CSR）
void hal_tlb_flush(void);

/* ---- 串口 ---- */
void hal_uart_init(void);
void hal_uart_putc(char c);
int  hal_uart_getc(void);

/* ---- 启动 ---- */
void hal_start(void);   // 平台初始化入口（main() 之前调用）

#endif
```

---

## 四、LoongArch 环境搭建思路

LoongArch 的 QEMU 和工具链需要单独安装。关键组件：

- **交叉编译器**：`loongarch64-linux-gnu-gcc`（或 `loongarch64-unknown-elf-gcc`）
- **QEMU**：需要 `qemu-system-loongarch64`，QEMU 7.1+ 开始支持
- **参考机器**：QEMU 的 `virt` 机器（LoongArch）

WSL2 下安装 LoongArch 工具链的大致步骤（第三、四周详细操作）：
```bash
# 方案一：从 Debian/Ubuntu 源（如果版本够新）
sudo apt install gcc-loongarch64-linux-gnu qemu-system-misc

# 方案二：从龙芯官方下载预编译工具链
# https://github.com/loongson/build-tools/releases
```

---

## 五、八周详细计划

### 🗓️ 第1周：深度理解原版 xv6-riscv（重要，打基础）

**目标**：在动手改代码之前，彻底搞清楚哪些文件是平台相关的。

**每日任务**：
- Day 1-2：阅读 `kernel/riscv.h`，理解所有 CSR 寄存器宏；阅读 `kernel/memlayout.h`，画出 RISC-V 的物理地址布局图
- Day 3：阅读 `kernel/entry.S` 和 `kernel/start.c`，搞懂启动流程（M 态 → S 态）
- Day 4：阅读 `kernel/trampoline.S` 和 `kernel/trap.c`，理解用户态/内核态切换全过程
- Day 5：阅读 `kernel/vm.c`，理解 sv39 三级页表的建立和使用
- Day 6：阅读 `kernel/plic.c` 和 `kernel/swtch.S`
- Day 7：整理**平台相关代码清单**，为每个文件/函数打上标签（"需要 HAL 化" / "通用"）

**交付物**：一份平台相关代码清单 + 物理地址布局图

---

### 🗓️ 第2周：在 RISC-V 上建立 HAL 框架（不破坏功能）

**目标**：把平台相关代码搬到 `hal/riscv/` 下，内核通过 `hal.h` 调用，系统仍能正常启动和跑测例。

**步骤**：
1. 创建 `hal/` 目录结构
2. 编写 `hal/hal.h`（先把接口定义好，哪怕还没实现）
3. 将 `kernel/riscv.h` 的宏和函数移入 `hal/riscv/riscv_regs.h`
4. 创建 `hal/riscv/hal_cpu.c`：实现 `hal_intr_on/off/get`、`hal_cpuid`
5. 创建 `hal/riscv/hal_uart.c`：封装 UART 操作
6. 修改 `Makefile`：引入 `ARCH ?= riscv` 变量，根据 ARCH 编译不同的 hal/ 子目录

**验证**：`make qemu` 能正常进入 xv6 Shell，`usertests` 通过。

**关键原则**：**这一步只是搬家，不改逻辑**。先让系统继续工作，再往下走。

---

### 🗓️ 第3周：完成 RISC-V HAL 的全部接口

**目标**：把所有平台相关部分全部 HAL 化。

**任务**：
- `hal/riscv/hal_intr.c`：封装 PLIC 的 `plic_init`、`plic_claim`、`plic_complete`
- `hal/riscv/hal_timer.c`：封装 CLINT 定时器
- `hal/riscv/hal_vm.c`：封装页表创建、映射、TLB 刷新（`sfence.vma`）、切换（写 `satp`）
- 将 `entry.S`、`trampoline.S`、`swtch.S` 移入 `hal/riscv/`
- `kernel/trap.c` 中的平台特定部分提取到 `hal/riscv/hal_trap.c`，通过回调或函数指针接入

**验证**：再次运行 `make ARCH=riscv qemu`，全部测例通过。此时你有了一个**干净的、RISC-V 实现已完整的 HAL**。

---

### 🗓️ 第4周：搭建 LoongArch 开发环境

**目标**：能编译出 LoongArch 的内核并在 QEMU 上启动（哪怕只是打印一行字）。

**任务**：
- 安装 `qemu-system-loongarch64` 和 `loongarch64-linux-gnu-gcc`（或裸机 elf 工具链）
- 了解 LoongArch 的：
  - **特权架构**：CSR 寄存器（CRMD、PRMD、ECFG、ESTAT 等），对应 RISC-V 的 sstatus/stvec
  - **地址空间**：直接映射窗口（DMW）机制，与 RISC-V sv39 的区别
  - **中断控制器**：EIOINTC 或 LIOINTC
  - **启动流程**：从 QEMU `virt` 机器的复位向量开始
- 编写最小的 LoongArch 启动代码（`hal/loongarch/entry.S`），能从 UART 输出 "Hello LoongArch"
- 编写最小 Makefile 支持 `make ARCH=loongarch`

**参考资料**：
- 龙芯架构参考手册（LoongArch Reference Manual）
- Linux 内核中 `arch/loongarch/` 的实现（很好的参考）

---

### 🗓️ 第5周：实现 LoongArch HAL——CPU、内存、串口

**目标**：实现 LoongArch 版本的基础 HAL 接口。

**任务**：
- `hal/loongarch/hal_cpu.c`：用 LoongArch CSR 指令实现中断开关、核心ID
- `hal/loongarch/hal_uart.c`：QEMU virt 机型上的 UART（16550 兼容，地址不同）
- `hal/loongarch/hal_vm.c`：实现 LoongArch 的三级页表（与 sv39 类似但格式不同），封装 TLB 刷新（`invtlb` 指令）和页表切换（写 `PGDL` CSR）
- `hal/loongarch/entry.S`：实现多核启动、内核栈设置

**验证**：能进入 `main()` 并完成内存初始化。

---

### 🗓️ 第6周：实现 LoongArch HAL——中断、定时器、上下文切换

**目标**：实现完整的中断和调度支持。

**任务**：
- `hal/loongarch/hal_intr.c`：实现 LoongArch 的外部中断控制（QEMU virt 上可用 PCIE INTC 或简化版）
- `hal/loongarch/hal_timer.c`：用 LoongArch 的 `TCFG`/`TVAL` CSR 实现定时器中断
- `hal/loongarch/trampoline.S`：实现用户态到内核态的陷入向量（`tlbrefill`、`exception` 向量）
- `hal/loongarch/swtch.S`：实现通用寄存器的保存/恢复（LoongArch 有 32 个通用寄存器）
- `hal/loongarch/hal_trap.c`：实现异常分发逻辑

---

### 🗓️ 第7周：整合调试，通过测例

**目标**：在 LoongArch 上跑通 xv6 的核心测例。

**任务**：
- 整合 VFS 层（与队友协调接口），确保 HAL 层不影响文件系统
- 移植用户态程序的编译（`user/` 目录下的程序需要用 loongarch 工具链编译）
- 调试常见问题：
  - 页表映射错误（最常见）
  - 中断向量未对齐
  - 定时器中断未触发导致无法调度
- 目标：通过 `usertests`（或其等价版本）的基本测例：`fork`、`exec`、`pipe`、`read/write`

**工具**：
```bash
# QEMU 调试技巧
qemu-system-loongarch64 -S -gdb tcp::1234 &
loongarch64-linux-gnu-gdb vmlinux -ex "target remote :1234"
```

---

### 🗓️ 第8周：收尾、优化、生成 diff

**目标**：系统稳定，整理代码，提交。

**任务**：
- 清理所有 TODO 和调试代码
- 统计新增代码行数（目标：HAL 代码增量尽量小）
- 编写 `README.md`：记录如何编译和运行两个平台
- 生成 diff 文件：
  ```bash
  git diff xv6-riscv-rev5 HEAD > my-hal.diff
  ```
- 补充注释，确保代码可读性（这是教学操作系统，注释很重要）
- 最终回归测试：两个平台全部测例通过

---

## 六、关键技术对照表

这是 RISC-V 和 LoongArch 最重要的概念对应关系，开发时随时查阅：

| 功能 | RISC-V | LoongArch |
|---|---|---|
| 关中断 | `csrci sstatus, SIE` | `csrxchg r0, t0, CRMD` |
| 获取核心ID | `csrr a0, mhartid` | `cpucfg rd, 0x5` 或 `CSR.CPUID` |
| 设置异常向量 | `csrw stvec, t0` | `csrwr t0, EENTRY` |
| 页表基址 | `csrw satp, t0` (含模式) | `csrwr t0, PGDL` |
| TLB 刷新 | `sfence.vma` | `invtlb 0, r0, r0` |
| 定时器 | CLINT（MMIO） | `CSR.TCFG` / `CSR.TVAL` |
| 中断控制器 | PLIC（MMIO） | EIOINTC / LIOINTC |
| 特权级 | M/S/U 三级 | PLV0/PLV3 两级（PLV0=内核） |
| 页表格式 | sv39（3级，9-9-9-12） | 类似，但 PTE 格式不同 |

---

## 七、立即可以开始的行动

1. **今天**：克隆 xv6-riscv-rev5，`make qemu` 跑通，确认环境正常
2. **今天**：创建自己的 git 分支 `git checkout -b hal-dev`，后续所有改动在此分支
3. **本周**：按第1周计划阅读源码，完成代码标注

有任何具体问题（比如某个 RISC-V CSR 怎么对应 LoongArch 的寄存器、某段汇编怎么改），随时告诉我，我可以给出具体的代码实现。祝项目顺利！

---

## 八、最新进展（2026-05-27）

### ✅ HAL 接口命名统一（第3周收尾）

**目标**：将内核调用统一到 `hal_` 前缀接口，为 LoongArch 移植提供清晰的实现契约。

**实现策略**：薄包装（thin wrapper）——在 RISC-V 平台代码中添加 `hal_` 前缀函数转发到原有实现，同时更新内核调用点。

**代码规模**：
- HAL 接口头文件：8 个，179 行
- RISC-V 平台实现：11 个文件，1382 行
- 总计：19 个文件，1561 行

**已验证**：
- ✅ `make qemu` 正常启动（xv6 kernel is booting，3 hart 启动）
- ✅ `usertests` 全部通过（ALL TESTS PASSED）
- ✅ 崩溃恢复测试通过（log/forphan/dorphan）

### 🐛 已修复 Bug

| Bug | 症状 | 根因 | 修复 |
|---|---|---|---|
| hal_get_hartid 特权级错误 | 内核静默崩溃，无输出 | `hal_get_hartid()` → `r_mhartid()` 读 M 态 CSR，S 态不可访问 | 改为 `r_tp()` 读 tp 寄存器 |

### 🚀 下一步（第4周）
1. 搭建 LoongArch 开发环境（交叉编译工具链 + QEMU 7.1+）
2. 编写 LoongArch 最小启动代码（arch.h + memlayout.h + hal_entry.S）
3. 目标：能进入 main() 并完成基本初始化