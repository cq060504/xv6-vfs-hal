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

## 四、LoongArch 环境搭建（第4周核心任务）

### 4.1 为什么用 Docker？

- **隔离性**：LoongArch 工具链与 RISC-V 工具链完全隔离
- **可复现**：Dockerfile 即文档，任何人可重建相同环境
- **WSL2 友好**：Docker Desktop 在 WSL2 中运行良好
- **一键切换**：通过 volume 挂载源码，宿主机编辑，容器内编译

### 4.2 Dockerfile（基于 Ubuntu 24.04）

```dockerfile
FROM ubuntu:24.04
ENV DEBIAN_FRONTEND=noninteractive
RUN apt-get update && apt-get install -y --no-install-recommends \
    gcc-loongarch64-linux-gnu \
    g++-loongarch64-linux-gnu \
    binutils-loongarch64-linux-gnu \
    qemu-system-misc \
    qemu-system-data \
    gdb-multiarch \
    make python3 perl git build-essential \
    && rm -rf /var/lib/apt/lists/*
WORKDIR /xv6
CMD ["/bin/bash"]
```

> 构建：`docker build -t xv6-loongarch -f Dockerfile.loongarch .`
> 启动：`docker run -it --rm -v $(pwd):/xv6 xv6-loongarch /bin/bash`

### 4.3 Makefile 双架构支持

关键修改：`ifeq ($(ARCH),loongarch)` 分支
- TOOLPREFIX = `loongarch64-linux-gnu-`
- QEMU = `qemu-system-loongarch64`
- CFLAGS += `-march=loongarch64 -mcmodel=normal`
- QEMUOPTS = `-machine virt -cpu la464`

### 4.4 工具链变量对照

| 变量 | RISC-V | LoongArch |
|---|---|---|
| TOOLPREFIX | `riscv64-unknown-elf-` | `loongarch64-linux-gnu-` |
| QEMU | `qemu-system-riscv64` | `qemu-system-loongarch64` |
| MIN_QEMU_VERSION | 7.2 | 7.1 |
| CFLAGS -march | `rv64gc` | `loongarch64` |
| CFLAGS -mcmodel | `medany` | `normal` |
| kernel.ld OUTPUT_ARCH | `riscv` | `loongarch` |

### 4.5 第4周具体任务清单

1. [ ] 编写 Dockerfile，构建镜像，验证 `loongarch64-linux-gnu-gcc` 可用
2. [ ] 验证 QEMU：启动 LoongArch virt 机器
3. [ ] 编写最小汇编启动代码，通过 UART 输出字符
4. [ ] 修改 Makefile（`ARCH=loongarch` 分支）
5. [ ] 实验确认 QEMU virt 内存布局：UART 地址(0x1FE001E0)、RAM 基址、内核加载地址

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

**目标**：Docker 化 LoongArch 交叉编译工具链和 QEMU，能编译最小启动代码并输出字符。

**任务**：
- 编写 `Dockerfile.loongarch`（Ubuntu 24.04 + gcc-loongarch64-linux-gnu + qemu-system-loongarch64）
- 构建 Docker 镜像，验证工具链可用
- 修改 Makefile：`ifeq ($(ARCH),loongarch)` 分支（TOOLPREFIX, QEMU, CFLAGS）
- 实验性编写 `hal/loongarch/hal_entry.S` 和 `hal/loongarch/kernel.ld`（最小版本）
- 从 UART (0x1FE001E0) 输出 "Hello LoongArch" 确认启动成功
- 确认 QEMU LoongArch virt 机器内存布局

**交付物**：Dockerfile + 最小启动代码 + `make ARCH=loongarch` 可用

**参考**：完整 Docker 搭建步骤见 `loongarch-migration-guide.html` Part 一

---

### 🗓️ 第5周：实现 LoongArch HAL——CPU、内存、串口（6个文件）

**目标**：实现基础 HAL 接口，能进入 `main()` 并完成内存初始化。

**具体文件与任务**：

| 文件 | 任务 |
|---|---|
| `hal/loongarch/arch.h` (~400行) | 所有 CSR 内联函数（csrrd/csrwr）、PTE 格式宏（PA2PTE/PTE2PA 分两段）、中断控制、HAL 统一包装 |
| `hal/loongarch/memlayout.h` (~60行) | QEMU virt 物理地址布局：UART0=0x1FE001E0, RAM, KERNBASE, TRAMPOLINE |
| `hal/loongarch/hal_entry.S` (~30行) | 启动入口：读 CPUID CSR → 设栈 → 跳 start() |
| `hal/loongarch/hal_start.c` (~80行) | PLV0 初始化（无 M 态切换）：配置 CRMD/EENTRY → 定时器初始化 → tp=cpuid → 跳 main() |
| `hal/loongarch/hal_uart.c` (~160行) | 16550a UART 驱动（逻辑复用 RISC-V 版本，仅地址不同） |
| `hal/loongarch/kernel.ld` (~50行) | 链接脚本：OUTPUT_ARCH("loongarch")，加载地址 0x1c000000 |

**关键技术注意事项**：
- **PTE PPN 分两段**：PA2PTE(pa) = `((pa>>12)<<12) | ((pa>>48)<<7)`，与 RISC-V 完全不同
- **无 M 态**：start() 直接 PLV0 运行，无需 mret。用 `w_eentry(kernelvec)` 设置异常入口
- **定时器**：`w_tcfg(0x1)` 使能 + `w_tval(1000000)` 设初值，中断在 ESTAT.IS[11]

**验证**：能输出 "xv6 kernel is booting"，`kinit()` 完成物理内存初始化

---

### 🗓️ 第6周：实现 LoongArch HAL——中断、定时器、上下文切换（5个文件）

**目标**：完整的中断和调度支持。

**具体文件与任务**：

| 文件 | 任务 |
|---|---|
| `hal/loongarch/hal_tramp.S` (~200行) | 用户态陷入/返回：uservec（保存GPR→TRAPFRAME→切页表→跳usertrap）+ userret（恢复GPR→ertn返回）。还需实现 TLBRENTRY 的软件 TLB 重填 handler |
| `hal/loongarch/hal_kvec.S` (~70行) | 内核态中断向量：保存caller-saved寄存器 → kerneltrap() → 恢复 → ertn |
| `hal/loongarch/hal_swtch.S` (~50行) | 上下文切换：保存/恢复 12 个 callee-saved 寄存器（ra, sp, fp, s0-s8） |
| `hal/loongarch/hal_intr.c` (~100行) | EIOINTC 中断控制器的 init/hart_init/claim/complete。参考 Linux `irq-loongarch-eiointc.c` 和 QEMU `loongarch_extioi.c` |
| `hal/loongarch/hal_timer.c` (~30行) | 定时器包装（timerinit 已在 hal_start.c，此文件可选） |

**同时修改的通用文件**：

| 文件 | 修改 |
|---|---|
| `hal/hal_ctx.h` | `#ifdef ARCH_loongarch` 分支：struct hal_context 12 字段（vs RISC-V 14） |
| `kernel/trap.c` | devintr() 中 scause 比较 → 使用 `hal_read_scause()`（由 arch.h 中 estat_to_scause 转换） |

**关键技术注意事项**：
- **软件 TLB 重填**：先在内核空间用 DMW 直接映射避免 TLB miss；用户态页表 walk 在 TLBRENTRY handler 中实现
- **EIOINTC**：与 PLIC 完全不同，寄存器布局需参考 Linux 内核。第5周可先跳过，轮询 UART
- **ertn 代替 sret**：所有异常返回用 `ertn`
- **ertn 返回用户态**：需设 ERA=user_pc, PRMD.PPLV=3(PLV3), PRMD.PIE=1(开中断)

**验证**：定时器中断触发、两个进程交替运行、shell 响应键盘输入

---

### 🗓️ 第7周：整合调试，通过测例

**目标**：在 LoongArch 上跑通 xv6 的核心测例。

**任务**：
- `hal/loongarch/hal_virtio.c` (~330行)：PCI virtio 磁盘驱动
  - **关键差异**：LoongArch QEMU virt 用 PCI virtio，非 MMIO virtio
  - **简化方案**：先用 `-initrd` ramdisk 绕过 PCI 枚举，后期再实现完整 PCI virtio
- 移植用户态程序（`user/` 用 loongarch 工具链编译）
- 调试常见问题：
  - 页表 PPN 两段式存储导致映射错误（最常见）
  - TLB 重填 handler 的页表 walk 逻辑错误
  - 中断向量未对齐或 ECFG.VS 配置错误
  - 定时器中断未触发导致无法调度
  - 上下文切换寄存器偏移与 hal_ctx.h 不一致

**验证**：`fork`、`exec`、`pipe`、`read/write` 基本测例通过

**调试工具**：
```bash
# QEMU 调试
qemu-system-loongarch64 -S -gdb tcp::1234 &
gdb-multiarch kernel -ex "target remote :1234"

# 打印每条指令和 CPU 状态
qemu-system-loongarch64 -d in_asm,cpu,int

# 反汇编验证
loongarch64-linux-gnu-objdump -d kernel

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

## 六、关键技术对照表（RISC-V ↔ LoongArch）

### 6.1 总体差异

| 维度 | RISC-V (rv64) | LoongArch (LA64) | HAL 影响 |
|---|---|---|---|
| 字长 | 64-bit | 64-bit | 无差异 |
| 特权级 | M/S/U 三级 | PLV0(内核)/PLV3(用户) 两级 | 无需 M 态，start() 极简 |
| CSR 指令 | `csrr/csrw/csrci/csrsi` | `csrrd/csrwr/csrxchg` | 重写 CSR 内联函数 |
| 页表 walk | 硬件自动（Sv39 3级） | **软件 TLB 重填** + DMW | 最大差异！需 TLBRENTRY handler |
| TLB 刷新 | `sfence.vma` | `invtlb 0, $r0, $r0` | hal_tlb_flush_all() |
| 异常返回 | `sret`/`mret` | `ertn` | trampoline.S/kvec.S |
| 中断控制器 | PLIC (MMIO) | EIOINTC (MMIO) | hal_intr.c 完全重写 |
| 定时器 | CLINT MMIO + stimecmp CSR | TCFG/TVAL CSR（纯CSR） | hal_timer.h |
| virtio 磁盘 | MMIO @0x10001000 | PCI virtio（PCIe 总线） | hal_virtio.c 需 PCI 枚举 |
| UART | 16550a @0x10000000 | 16550a @0x1FE001E0 | 仅地址不同 |
| Callee-saved | ra, sp, s0-s11 (14个) | ra, sp, fp, s0-s8 (12个) | hal_ctx.h 需条件编译 |

### 6.2 CSR 寄存器对照

| 用途 | RISC-V CSR | LoongArch CSR | 编号 |
|---|---|---|---|
| 当前状态 | `sstatus` | `CRMD` (PLV+IE+PG+DA) | 0x0 |
| 异常前状态 | `sstatus.SPP/SPIE` | `PRMD` (PPLV+PIE) | 0x1 |
| 中断使能 | `sie` | `ECFG` (LIE[12:0]) | 0x4 |
| 异常状态 | `scause` | `ESTAT` (IS+Ecode+EsubCode) | 0x5 |
| 异常返回地址 | `sepc` | `ERA` | 0x6 |
| 异常虚拟地址 | `stval` | `BADV` | 0x7 |
| 异常入口 | `stvec` | `EENTRY` | 0xC |
| 页表基址 | `satp` (含模式) | `PGDL` (纯物理地址) | 0x19 |
| 核心ID | `mhartid` | `CPUID` | 0x20 |
| 定时器配置 | (CLINT MMIO) | `TCFG` | 0x41 |
| 定时器值 | `stimecmp` | `TVAL` | 0x42 |
| 定时器清中断 | (写 stimecmp) | `TICLR` | 0x44 |
| TLB 重填入口 | (无) | `TLBRENTRY` | 0x88 |
| 直接映射窗口 | (无) | `DMW0-DMW3` | 0x180-0x183 |

### 6.3 异常码对照（trap.c 适配）

| 事件 | RISC-V scause | LoongArch ESTAT.Ecode |
|---|---|---|
| 系统调用 | `8` | `EXCCODE_SYS = 11 (0xB)` |
| 缺页/load | `13` | `EXCCODE_TLBL = 1` |
| 缺页/store | `15` | `EXCCODE_TLBS = 2` |
| 缺页/fetch | `12` | `EXCCODE_TLBI = 3` |
| 非法指令 | `2` | `EXCCODE_INE = 13 (0xD)` |
| 定时器中断 | `0x8000000000000005` | Ecode=0, IS[11]=1 |
| 外部中断 | `0x8000000000000009` | Ecode=0, IS[12]=1 |

> 推荐在 `hal_read_scause()` 中转换 ESTAT → scause，让 trap.c 无需修改。

### 6.4 页表格式差异

```
RISC-V PTE: |保留| PPN[53:10] | Flags[9:0] |
            V(0) R(1) W(2) X(3) U(4) G(5) A(6) D(7)

LoongArch PTE:
|63|  62   |  61  | 60 |   59-12   |11-7  | 6 |5-4 |3-2 |1|0|
|NX_H|RPLV_H|RPLV| NX |  PPN[47:0]|PPN0  | G |MAT |PLV |D|V|
```

关键：LoongArch PPN 在 PTE 中分两段！`PA2PTE(pa) = ((pa>>12)<<12) | ((pa>>48)<<7)`

### 6.5 GPR ABI 对照

| 类别 | RISC-V | LoongArch |
|---|---|---|
| 零寄存器 | x0 | $r0 |
| 返回地址 | x1/ra | $r1/$ra |
| 线程指针 | x4/tp | $r2/$tp |
| 栈指针 | x2/sp | $r3/$sp |
| 参数 | a0-a7 (x10-x17) | $a0-$a7 ($r4-$r11) |
| 临时 | t0-t6 | $t0-$t8 ($r12-$r20) |
| 帧指针 | s0/fp (x8) | $fp ($r22) |
| 被调者保存 | ra,sp,s0-s11 (14) | ra,sp,fp,s0-s8 (12)

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

---

## 九、最新进展（2026-06-03）

### 当前验收状态

LoongArch 移植已经进入 `usertests -q` 后段调试阶段。前序测试已基本通过，当前明确失败点是 `MAXVAplus`：

```text
test MAXVAplus:
usertrap(): unexpected scause 0xf pid=6516
  sepc=0x2290 stval=0x400000000000
usertrap(): unexpected scause 0xf pid=6517
  sepc=0x2290 stval=0x800000000000
MAXVAplus: oops wrote 0x0001000000000000
FAILED
SOME TESTS FAILED
```

已完成的关键修复：
- LoongArch TLB refill handler 已恢复为 `hal/loongarch/hal_tlbrefill.S`。
- 使用 `lddir/ldpte/tlbfill`，匹配当前 4 级页表和 `PWCL/PWCH` 配置。
- refill 专用 CSR 编号已按 LoongArch 手册修正：`TLBRSAVE=0x8b`，`TLBREHI=0x8e`，`TLBRELO0=0x8c`，`TLBRELO1=0x8d`。
- Docker 内 `make ARCH=loongarch` 已通过；QEMU 短跑能进入 `usertests` 并通过前序 copy 测试。

### 当前策略

优先目标不是完善长期架构，而是在不违反 LoongArch 技术要求的前提下，以最小改动通过 xv6 测试。后续再回头优化页表格式、TLB refill 性能和异常路径。

### 下一步排查计划

1. 检查 `MAXVA` 定义是否满足 xv6 原始 `MAXVAplus` 测试语义。当前 LoongArch 4 级页表允许更大的 VA 范围，但 xv6 测试期望高地址不能被用户写入。
2. 检查 `walk()`、`walkaddr()`、`copyin()`、`copyout()`、`copyinstr()` 是否在入口处统一拒绝 `va >= MAXVA` 或非规范用户地址。
3. 检查 LoongArch TLB refill 对高地址 miss 的处理：对于 `0x400000000000`、`0x800000000000` 这类地址，不能让 `lddir/ldpte` 因索引截断或符号扩展命中低地址页表。
4. 简化优先方案：把用户地址上限先收敛到 xv6 原版兼容范围，确保所有用户态内存访问路径和 TLB refill 路径一致拒绝越界地址。
