# xv6-riscv HAL 项目技术报告

---

## 一、HAL 设计思想与整体框架

### 1.1 设计思想

xv6-riscv HAL 项目遵循以下核心设计原则：

**（1）关注点分离（Separation of Concerns）**

原版 xv6-riscv 的内核代码直接使用 RISC-V 的 CSR 寄存器操作、PLIC 中断控制器、CLINT 定时器、Sv39 页表等硬件细节。HAL 的核心思想是将这些**平台相关代码**与**内核通用逻辑**彻底解耦：

```
┌──────────────────────────────────────────┐
│     内核通用代码（kernel/*.c）            │
│     进程调度 / 文件系统 / 系统调用 / ...    │
│     → 只调用 HAL 接口，不包含任何 CSR 操作  │
├──────────────────────────────────────────┤
│     HAL 接口层（hal/hal_*.h）             │
│     统一的函数声明 / 宏定义                │
├────────────────────┬─────────────────────┤
│  RISC-V 实现       │  LoongArch 实现      │
│  hal/riscv/        │  hal/loongarch/     │
│  arch.h (CSR+PTE)  │  arch.h (CSR+PTE)   │
│  hal_start.c       │  hal_start.c        │
│  hal_tramp.S ...   │  hal_tramp.S ...    │
└────────────────────┴─────────────────────┘
```

**（2）接口优先（Interface First）**

先定义统一的 HAL 接口（`hal_arch.h`, `hal_intr.h`, `hal_timer.h`, `hal_vm.h`, `hal_memlayout.h`, `hal_console.h`），再由各平台分别实现。内核代码只依赖接口声明，不感知底层实现。

**（3）只搬家不改逻辑（Migration, not Refactoring）**

第2-3周的 RISC-V HAL 化遵循"将平台代码移入 HAL 目录，保持系统可运行，不重构逻辑"的原则。所有函数行为与原版 xv6-riscv 完全一致，仅改变代码的物理位置和 include 路径。

**（4）渐进式开发（Progressive）**

分阶段完成：第1周理解源码 → 第2周建立框架 → 第3周完成 RISC-V 实现 → 第4周搭建 LoongArch 环境 → 第5-7周完成 LoongArch 实现。

### 1.2 整体项目框架

#### Include 层级

内核源文件只 include 一个统一头文件：

```
kernel/*.c
  └── #include "hal/hal.h"
        ├── #include "types.h"            (kernel/types.h)
        ├── #include "arch.h"             → hal/riscv/arch.h  (CSR 内联函数、PTE 宏)
        ├── #include "memlayout.h"        → hal/riscv/memlayout.h (物理地址布局)
        ├── #include "hal_arch.h"         (CPU 控制接口声明)
        ├── #include "hal_vm.h"           (页表/MMU 接口声明)
        ├── #include "hal_intr.h"         (中断控制器接口声明)
        ├── #include "hal_timer.h"        (定时器接口声明)
        ├── #include "hal_memlayout.h"    (内存布局接口声明)
        ├── #include "hal_console.h"      (控制台 I/O 接口声明)
        └── #include "hal_ctx.h"          (上下文切换接口声明)
```

#### 编译流程

Makefile 通过 `ARCH` 变量（默认为 `riscv`）选择平台：

```makefile
ARCH ?= riscv
HAL = hal
CFLAGS += -I$(HAL) -I$(HAL)/$(ARCH) -DARCH_$(ARCH)

ARCH_OBJS = \
  $(HAL)/$(ARCH)/hal_entry.o \
  $(HAL)/$(ARCH)/hal_start.o \
  $(HAL)/$(ARCH)/hal_swtch.o \
  $(HAL)/$(ARCH)/hal_tramp.o \
  $(HAL)/$(ARCH)/hal_kvec.o \
  $(HAL)/$(ARCH)/hal_plic.o \
  $(HAL)/$(ARCH)/hal_uart.o \
  $(HAL)/$(ARCH)/hal_virtio.o

OBJS = $(K_OBJS) $(ARCH_OBJS)
$K/kernel: $(OBJS) $(HAL)/$(ARCH)/kernel.ld
```

`hal/{riscv,loongarch}/arch.h` 提供 `#ifndef __ASSEMBLER__` 保护，汇编文件（`.S`）和 C 文件均可安全 include。

---

## 二、所有新增与修改的文件（逐文件清单）

### 2.1 新增文件（hal/ 目录，共 18 个）

#### HAL 接口头文件（8 个，位于 hal/）

| 文件 | 行数 | 内容 |
|---|---|---|
| `hal/hal.h` | 30 | 统一入口：根据 `ARCH_xxx` 宏选择 arch.h 和 memlayout.h，按顺序 include 全部子系统头文件 |
| `hal/hal_arch.h` | 38 | CPU 架构抽象接口声明：`hal_get_hartid()`, `hal_read_sstatus()`, `hal_write_sstatus()`, `hal_read_sepc()`, `hal_write_sepc()`, `hal_read_scause()`, `hal_read_stval()`, `hal_read_stvec()`, `hal_write_stvec()`, `hal_read_satp()`, `hal_write_satp()`, `hal_read_sp()`, `hal_read_ra()` |
| `hal/hal_intr.h` | 14 | 中断控制器接口声明：`hal_irq_init()`, `hal_irq_hart_init()`, `hal_irq_claim()`, `hal_irq_complete()` |
| `hal/hal_timer.h` | 17 | 定时器接口声明：`hal_get_time()`, `hal_set_timer()`, `hal_timer_init()` |
| `hal/hal_vm.h` | 18 | 虚拟内存/MMU 接口声明：`hal_tlb_flush_all()` |
| `hal/hal_memlayout.h` | 16 | 内存布局接口：声明 `HAL_ETEXT[]`, `HAL_END[]` 外部符号 |
| `hal/hal_console.h` | 14 | 控制台 I/O 接口声明：`hal_console_init()`, `hal_putchar_sync()`, `hal_putchar()`, `hal_getchar()`, `hal_console_intr()` |
| `hal/hal_ctx.h` | 31 | 上下文切换接口：`struct hal_context`（14个 callee-saved 寄存器），`hal_switch()` |

#### RISC-V 平台实现（10 个，位于 hal/riscv/）

| 文件 | 行数 | 原对应文件 | 内容 |
|---|---|---|---|
| `hal/riscv/arch.h` | 367 | `kernel/riscv.h` | RISC-V CSR 内联函数（r_mhartid, r/w_mstatus, r/w_sstatus, r/w_sepc, r/w_satp, r/w_stvec, r_scause, r_stval, r_time, r_tp, w_tp, intr_on/off/get, sfence_vma 等）、PTE 格式宏（PTE_V/R/W/X/U）、页表操作宏（PA2PTE/PTE2PA/PX/PXMASK）、Sv39 常量（MAXVA, SATP_SV39, MAKE_SATP）、PGSIZE/PGSHIFT/PGROUNDUP/PGROUNDDOWN |
| `hal/riscv/memlayout.h` | 58 | `kernel/memlayout.h` | RISC-V QEMU virt 机器物理地址布局：UART0(0x10000000), VIRTIO0(0x10001000), PLIC(0x0C000000), KERNBASE(0x80000000), PHYSTOP(KERNBASE+128M), TRAMPOLINE(MAXVA-PGSIZE), TRAPFRAME, KSTACK(p) 宏 |
| `hal/riscv/hal_entry.S` | 21 | `kernel/entry.S` | 启动入口 `_entry`：设置栈指针（stack0 + hartid*4096），跳转到 `start()` |
| `hal/riscv/hal_start.c` | 68 | `kernel/start.c` | M 态启动函数 `start()`：设置 mstatus.MPP=S，委托中断/异常到 S 态，配置 PMP，初始化定时器，写 tp 寄存器，mret 跳转到 `main()` |
| `hal/riscv/hal_tramp.S` | 149 | `kernel/trampoline.S` | 用户态陷入/返回：`uservec`（保存全部寄存器到 TRAPFRAME，加载内核页表，跳转 usertrap），`userret`（恢复寄存器，sret 返回用户态） |
| `hal/riscv/hal_kvec.S` | 62 | `kernel/kernelvec.S` | 内核态中断向量 `kernelvec`：保存/恢复 caller-saved 寄存器，调用 `kerneltrap()`，sret 返回 |
| `hal/riscv/hal_swtch.S` | 40 | `kernel/swtch.S` | 上下文切换 `swtch`：保存 ra/sp/s0-s11 到 *old，从 *new 加载，ret 跳转 |
| `hal/riscv/hal_plic.c` | 48 | `kernel/plic.c` | RISC-V PLIC 驱动：`plicinit()`（设置 UART/VIRTIO 优先级），`plicinithart()`（per-hart 使能和阈值），`plic_claim()`，`plic_complete()` |
| `hal/riscv/hal_uart.c` | 158 | `kernel/uart.c` | 16550a UART 驱动：`uartinit()`（波特率/FIFO/中断配置），`uartwrite()`（阻塞发送），`uartputc_sync()`（同步发送），`uartgetc()`，`uartintr()`（中断处理） |
| `hal/riscv/hal_virtio.c` | 326 | `kernel/virtio_disk.c` | Virtio 块设备驱动：`virtio_disk_init()`（MMIO 初始化/特性协商/队列配置），`virtio_disk_rw()`（读写，3 描述符链），`virtio_disk_intr()`（中断完成处理） |
| `hal/riscv/kernel.ld` | 45 | `kernel/kernel.ld` | RISC-V 链接脚本：OUTPUT_ARCH("riscv")，入口 `_entry`，加载地址 0x80000000，`.text.entry` 段置首，trampsec 段对齐 |


不完全一致，但差异很小。 下面是逐文件对比（基于 git diff 7472e3a..6546c92）：

  ┌──────────────┬────────────────┬───────────────────────────────────────────────────────────────────────────┐
  │     文件     │     相似度     │                                 实际改动                                  │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ arch.h       │ 全新文件       │ 从 kernel/riscv.h 中提取出的 380 行内联函数，完全保留逻辑                 │
  │              │ (367行)        │                                                                           │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_entry.S  │ 96%            │ 仅 .section .text → .section .text.entry（配合链接脚本变更）              │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_kvec.S   │ 96%            │ 仅删除了 2 行被注释掉的代码（# sd sp, 8(sp) 和对应的 restore）            │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_plic.c   │ 91%            │ #include "memlayout.h" + "riscv.h" → #include "hal/hal.h"，加了一行注释   │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_start.c  │ 87%            │ 同上 include 替换，加注释，删除了多余空行                                 │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_swtch.S  │ 92%            │ 删除了多余空行和行尾空格                                                  │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_tramp.S  │ 95%            │ #include "riscv.h" → #include "arch.h"，删除了 #include "memlayout.h"     │
  │              │                │ 前面的空行                                                                │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_uart.c   │ 89%            │ include 替换，ReadReg/WriteReg 宏的位置从文件末尾移到了开头（功能不变）   │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ hal_virtio.c │ 99%            │ include 替换，删除了多余空行                                              │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ kernel.ld    │ 72%            │ kernel/entry.o(_entry) → *(.text.entry)（配合 hal_entry.S 的段名变化）    │
  ├──────────────┼────────────────┼───────────────────────────────────────────────────────────────────────────┤
  │ memlayout.h  │ 81%            │ 加注释描述物理布局，加 #ifndef 头保护，删除了一段内存使用说明注释         │
  └──────────────┴────────────────┴───────────────────────────────────────────────────────────────────────────┘

  结论：逻辑零改动。 所有差异属于以下三类：

  1. include 路径替换：#include "riscv.h" + "memlayout.h" → #include "hal/hal.h"（5 个文件）
  2. 注释和格式：增加文件头注释、删除多余空行、对齐缩进（4 个文件）
  3. 配合重构的机械调整：hal_entry.S 的段名和 kernel.ld 的匹配方式（2 个文件）



### 2.2 修改的内核文件（kernel/ 目录，共 6 个）

所有修改遵循同一模式：**将 `#include "riscv.h"` 和 `#include "memlayout.h"` 替换为 `#include "hal/hal.h"`，代码逻辑不变**。

| 文件 | 修改内容 |
|---|---|
| `kernel/main.c` | Include 从 `"riscv.h"` + `"memlayout.h"` 改为 `"hal/hal.h"`。`start()` 函数移到 `hal/riscv/hal_start.c`，其余逻辑（`main()` 初始化流程）完全不变 |
| `kernel/trap.c` | Include 改为 `"hal/hal.h"`。`usertrap()`, `kerneltrap()`, `devintr()`, `clockintr()`, `prepare_return()` 等函数保持原样——它们调用 `r_scause()`, `r_sepc()`, `w_stvec()`, `intr_on()`, `plic_claim()` 等，这些全部来自 arch.h |
| `kernel/vm.c` | Include 改为 `"hal/hal.h"`。`kvmmake()`, `kvminithart()`, `walk()`, `mappages()`, `uvmalloc()` 等保持原样——调用 `sfence_vma()`, `w_satp()`, `MAKE_SATP()` 等来自 arch.h |
| `kernel/proc.c` | Include 改为 `"hal/hal.h"`。`cpuid()`, `scheduler()`, `forkret()` 等保持原样——调用 `r_tp()`, `intr_on()`, `swtch()` 等。其中 `swtch()` 在 `hal/riscv/hal_swtch.S` 中实现，Makefile 负责链接 |
| `kernel/console.c` | Include 增加 `"hal/hal.h"`（原无 riscv.h 依赖）。调用 `uartinit()`, `uartwrite()`, `uartputc_sync()`, `uartgetc()`, `uartintr()`——这些在 `hal/riscv/hal_uart.c` 中实现 |
| `kernel/defs.h` | 无实质变化。继续声明 `plicinit()`, `plic_claim()`, `uartinit()` 等——它们的实现位于 `hal/riscv/` 目录，由 Makefile 统一链接 |

### 2.3 未修改的内核文件（kernel/ 目录，共 20+ 个）

以下文件**完全未修改**，它们没有平台相关依赖，纯逻辑代码：

`bio.c`, `fs.c`, `file.c`, `log.c`, `pipe.c`, `exec.c`, `syscall.c`, `sysproc.c`, `sysfile.c`, `kalloc.c`, `printf.c`, `spinlock.c`, `sleeplock.c`, `string.c`, `proc.h`, `spinlock.h`, `sleeplock.h`, `fs.h`, `file.h`, `buf.h`, `elf.h`, `fcntl.h`, `param.h`, `stat.h`, `types.h`, `virtio.h`

### 2.4 kernel/ 目录中已失效的遗留文件（可删除）

以下文件是原 xv6-riscv 的平台相关代码，已迁移到 `hal/riscv/`，kernel/ 中的副本不再参与编译：

| 文件 | 已被替代为 |
|---|---|
| `kernel/riscv.h` | `hal/riscv/arch.h` (内容完全相同的副本) |
| `kernel/memlayout.h` | `hal/riscv/memlayout.h` |
| `kernel/entry.S` | `hal/riscv/hal_entry.S` |
| `kernel/start.c` | `hal/riscv/hal_start.c` |
| `kernel/trampoline.S` | `hal/riscv/hal_tramp.S` |
| `kernel/kernelvec.S` | `hal/riscv/hal_kvec.S` |
| `kernel/swtch.S` | `hal/riscv/hal_swtch.S` |
| `kernel/plic.c` | `hal/riscv/hal_plic.c` |
| `kernel/uart.c` | `hal/riscv/hal_uart.c` |
| `kernel/virtio_disk.c` | `hal/riscv/hal_virtio.c` |
| `kernel/kernel.ld` | `hal/riscv/kernel.ld` |

**这 11 个文件是冗余遗留物，不参与编译，建议删除。**

### 2.5 修改的其他文件

| 文件 | 修改内容 |
|---|---|
| `Makefile` | 新增 `ARCH` 变量和 `HAL` 路径，`ARCH_OBJS` 从 `hal/riscv/` 编译平台对象文件，`CFLAGS` 添加 `-Ihal -Ihal/$(ARCH) -DARCH_$(ARCH)`，通用编译规则新增 HAL 目录支持 |
| `test-xv6.py` | 修复 4 个 bug（详见第三节），添加 `os.chdir()` 支持从任意目录运行，修复 `usertests` 测试名冗余问题 |

---

## 三、test-xv6.py 修改分析

### 3.1 结论：修改与 HAL 完全无关，是测试脚本本身的 bug

`test-xv6.py` 的所有修改都是**修复测试脚本自身的 bug**，与 HAL 项目无关。即使在没有 HAL 的原版 xv6-riscv 上运行，这些 bug 同样存在。具体如下：

### 3.2 修复的 4 个 bug

**Bug 1：`error()` 方法缺少参数（NameError）**

```python
# 问题代码：
def error(self, regexps):           # 强制要求 regexps 参数
    print("FAIL: match failed", regexps)  # NameError 当 regexps 未传入

def monitor(self, *regexps, ...):
    if timeleft < 0:
        self.error()                # 调用时未传参 → TypeError
```

`monitor()` 超时时调用 `self.error()` 未传参，触发 Python TypeError，导致崩溃测试无法报告超时失败。

**修复**：`regexps` 参数改为可选（默认 `None`），`monitor()` 超时时传入 `regexps`。

**Bug 2：`save_output()` 属性名错误（AttributeError）**

```python
# 问题代码：
self.output = ""          # 属性名是 output
...
f.write(self.out)         # 访问不存在的 self.out → AttributeError
```

任何调用 `match()` 失败触发 `save_output()` 的场景都会先报 AttributeError 而非打印正确的失败信息。

**修复**：`self.out` → `self.output`。

**Bug 3：`reset_fs()` 在 fs.img 不存在时失败**

```python
# 问题代码：
run(["rm", "fs.img"], check=True)   # fs.img 不存在时 rm 返回 1
run(["make", "fs.img"], check=True) # 上一行抛异常后，此行不执行
# except 块仅打印错误，不重试 make fs.img
```

当第一次构建时 `fs.img` 尚不存在，`rm fs.img` 返回非零退出码，`check=True` 引发 `CalledProcessError`，`make fs.img` 被跳过，导致 QEMU 启动时找不到磁盘镜像。

**修复**：`rm fs.img` → `rm -f fs.img`。

**Bug 4：`test_usertests()` 命令拼接错误（"NO TESTS EXECUTED"）**

```python
# main() 中：
test_usertests(test=args.testrex)   # test = "usertests"
# test_usertests() 中：
elif test != "":
    opt += " " + test               # opt = " usertests"
# 发送给 xv6: "usertests usertests\n"
# xv6 的 usertests 程序尝试找到名为 "usertests" 的测试 → 找不到 → 输出 "NO TESTS EXECUTED"
```

**修复**：仅当 `test` 非空且不等于 "usertests" 时才追加参数。

### 3.3 额外改进

- 添加 `os.chdir(os.path.dirname(os.path.abspath(__file__)))`：使脚本无论从哪个目录调用都能找到 Makefile，这对于项目移动到子目录后非常重要。

---

## 四、HAL 进一步优化方向

### 4.1 当前遗留问题

**问题 1：kernel/ 中存在 11 个失效遗留文件**

`kernel/riscv.h`, `kernel/memlayout.h`, `kernel/entry.S`, `kernel/start.c`, `kernel/trampoline.S`, `kernel/kernelvec.S`, `kernel/swtch.S`, `kernel/plic.c`, `kernel/uart.c`, `kernel/virtio_disk.c`, `kernel/kernel.ld` ——这些文件是原版的完全副本，不参与编译，应直接删除以减少混淆。

**问题 2：`hal_arch.h` 接口声明与 `arch.h` 内联实现重复**

`hal_arch.h` 声明了 `hal_get_hartid()`, `hal_read_sstatus()` 等函数，但实际的内核代码并没有调用这些包装函数——它们直接使用 `arch.h` 中的内联函数 `r_mhartid()`, `r_sstatus()` 等。这意味着 `hal_arch.h` 的包装层是**未使用的死代码**。

有两种优化路线：
- **路线 A（推荐）**：删除 `hal_arch.h` 中未使用的包装函数声明，承认内联 CSR 函数本身就是 HAL 接口的一部分。这也是 Linux 内核的做法——`arch/riscv/include/asm/csr.h` 直接提供内联函数，没有额外包装层。
- **路线 B**：修改所有内核调用点，从 `r_sstatus()` 改为 `hal_read_sstatus()`，实现真正的接口隔离。代价是改动量大、容易引入 bug，但可以获得编译期的平台类型安全。

**问题 3：`hal_intr.h` / `hal_timer.h` / `hal_console.h` 同样存在接口-实现不一致**

- `hal_intr.h` 声明 `hal_irq_init()` → 实际函数名是 `plicinit()`
- `hal_timer.h` 声明 `hal_get_time()` → 实际函数名是 `r_time()`
- `hal_console.h` 声明 `hal_putchar_sync()` → 实际函数名是 `uartputc_sync()`

这说明接口头文件是"超前设计"——为 LoongArch 预留的命名规范，但 RISC-V 实现时为了保持与原版兼容而保留了原始函数名。

### 4.2 优化建议（按优先级排列）

#### 优先级 1（立即执行）：删除失效遗留文件

删除 kernel/ 中不再参与编译的 11 个文件。这些文件不仅增加仓库体积，更重要的是让新参与者困惑——不知道哪些文件是真正在用的。

#### 优先级 2（短期）：统一接口命名

有两种策略：

**策略 A —— 保留原版命名（最小改动）**：
修改 `hal_intr.h`, `hal_timer.h`, `hal_console.h` 的接口声明，使其与实际实现的函数名一致：
```c
// hal_intr.h —— 与当前实现一致
void plicinit(void);
void plicinithart(void);
int  plic_claim(void);
void plic_complete(int);
```

**策略 B —— 统一为 hal_ 前缀命名（更规范但改动更大）**：
在 `hal/riscv/` 中添加一层薄包装，使接口名统一：
```c
// hal/riscv/hal_intr.c 中添加：
void hal_irq_init(void) { plicinit(); }
```

策略 A 的收益是消除接口-实现不一致，改动最小。策略 B 的收益是 LoongArch 实现时可以有完全不同的函数名（如 `eiointc_init()`），包装后对外统一为 `hal_irq_init()`。

#### 优先级 3（中期）：消除 arch.h 中的非 CSR 内容

当前 `hal/riscv/arch.h` 混合了 3 类内容：
1. **CSR 寄存器操作内联函数**（如 `r_sstatus()`, `w_satp()`）—— 必须在 arch.h 中
2. **页表/PTE 宏**（如 `PTE_V`, `PA2PTE`, `PX()`）—— 属于 hal_vm.h 范畴
3. **通用页大小宏**（如 `PGSIZE`, `PGROUNDUP`）—— 属于 hal_vm.h 范畴

建议将第 2、3 类拆分到 `hal_vm.h`（或 `hal/riscv/vm_defs.h`），保持 arch.h 只含 CSR 操作。Linux 内核正是这样组织的：`arch/riscv/include/asm/pgtable.h` 处理页表格式。

#### 优先级 4（中长期）：LoongArch 实现时可借鉴的参考

**参考项目与论文：**

| 来源 | 借鉴方向 |
|---|---|
| **Linux kernel `arch/`** | 最成熟的 HAL 实践。每个架构在 `arch/xxx/` 下实现，通过 `include/asm-generic/` 提供默认实现。关键文件：`arch/loongarch/include/asm/csr.h`（CSR 操作）、`arch/loongarch/kernel/head.S`（启动入口） |
| **seL4 microkernel** | 形式化验证的操作系统。其 `arch/` 目录展示了如何最小化平台相关代码。每行代码都有验证覆盖率约束 |
| **xv6-riscv 原版** | 保持代码量的"基线"——新增 HAL 代码量不应超过原版平台代码量的 1.2× |
| **论文：*The Multikernel: A new OS architecture for scalable multicore systems*** (Baumann et al., SOSP 2009) | 将 OS 视为分布式系统，每个核心运行独立内核，通过消息通信。这为 HAL 提供了"异构多核"的视角——不同架构的核如何协作 |
| **论文：*Unikraft: Fast, Specialized Unikernels the Easy Way*** (EuroSys 2021) | 其微库（micro-library）架构展示了如何模块化 OS 组件，对 HAL 接口设计有参考价值 |
| **RISC-V SBI spec** | RISC-V 的 Supervisor Binary Interface 规范定义了一套标准的 M 态→S 态服务接口。HAL 接口设计可以对齐 SBI，使未来更容易支持 RISC-V 标准硬件 |
| **2025 OS 全国赛"静春山"团队** | 参考其 xv6-riscv LoongArch 移植方案，借鉴其 HAL 分层设计 |
| **2025 OS 全国赛"RuOK"团队** | 参考其跨架构抽象策略 |

#### 优先级 5（长期）：减少内核代码中的 #ifdef

当前设计通过 `-DARCH_$(ARCH)` 在编译期选择平台。更优雅的做法是让平台实现提供统一的符号表，内核完全通过函数指针或链接时选择来调用平台代码（类似 Linux 的 `struct platform_driver`）。但这会增加间接调用开销，对于教学 OS 来说可能过度设计。

### 4.3 代码量统计

| 类别 | 文件数 | 大致行数 |
|---|---|---|
| HAL 接口头文件 | 8 | ~200 |
| HAL RISC-V 实现 | 10 | ~1350 |
| 内核修改文件 | 6 | 仅 include 行变更 |
| **HAL 新增总代码量** | **18** | **~1550 行** |
| 原版平台代码（已迁移） | 11 | ~1500 行 |

HAL 引入的净增代码约 **50 行**（主要是接口头文件声明），证明"只搬家不改逻辑"的策略是高效的。

---


 清理完成。kernel/ 目录现在非常干净——只保留平台无关的 34 个文件：                                
                                                                                                                
  kernel/                                                                                                       
  ├── 通用逻辑（23个.c）:  bio, console, exec, file, fs, kalloc, log,                                           
  │                        main, pipe, printf, proc, sleeplock, spinlock,                                       
  │                        string, syscall, sysfile, sysproc, trap, vm                                          
  ├── 头文件（11个.h）:     buf, defs, elf, fcntl, file, fs, param,                                             
  │                        proc, sleeplock, spinlock, stat, syscall,                                            

  kernel/
  ├── 通用逻辑（23个.c）:  bio, console, exec, file, fs, kalloc, log,
  │                        main, pipe, printf, proc, sleeplock, spinlock,
  │                        string, syscall, sysfile, sysproc, trap, vm
  ├── 头文件（11个.h）:     buf, defs, elf, fcntl, file, fs, param,
  │                        proc, sleeplock, spinlock, stat, syscall,
  │                        types, virtio, vm
  └── 无汇编文件（.S）     全部平台汇编已移到 hal/riscv/

  同时修复了 Makefile 的 clean 规则——新增 hal/*/*.o hal/*/*.d hal/*/*.asm hal/*/*.sym 行，确保 make clean
  能正确清理嵌套的 HAL 平台目录

构建成功，所有内核目标文件、用户程序和文件系统镜像均已编译通过。                                                    
                                                                                          
  修复方式：创建了 kernel/riscv.h 兼容性头文件，内容很简单：                                                          
                                                                                                                      
  // Compatibility header — includes the architecture-specific HAL headers.                                           
  // User programs that still #include "kernel/riscv.h" will get the appropriate                                      
  // definitions (PGSIZE, KERNBASE, MAXVA, CSR inlines, etc.) for the target arch.                                    
  #include "hal/hal.h"                                                                                                
                                                                                                                      
  原理：                                                                                                              
  - 原 kernel/riscv.h 内容已迁移到 hal/riscv/arch.h（CSR内联函数、PTE宏、PGSIZE/MAXVA）和                             
  hal/riscv/memlayout.h（KERNBASE、TRAMPOLINE等物理地址布局）                                                         
  - hal/hal.h 会按 ARCH_riscv 自动包含对应的 arch.h 和 memlayout.h
  - Makefile 已有 -I. 和 -Ihal/riscv 的 include path，所以新文件会被正确找到                                          
  - user/usertests.c 使用的 PGSIZE、KERNBASE、MAXVA、r_sp() 等符号现在通过 HAL 层获取，做到了平台无关 

  Problem: user/usertests.c and user/grind.c both had #include "kernel/memlayout.h" — a file you deleted because it was moved to
  hal/riscv/memlayout.h.

  Fix: Removed the #include "kernel/memlayout.h" line from both files. They already had #include "kernel/riscv.h" on the very next line,
  which is the compatibility shim that pulls in hal/hal.h → arch.h + memlayout.h, providing all the same definitions (PGSIZE, KERNBASE,
  MAXVA, etc.).

  Changes made:
  - user/usertests.c:8 — removed stale #include "kernel/memlayout.h"
  - user/grind.c:12 — removed stale #include "kernel/memlayout.h"
## 五、HAL 接口命名统一（第3周收尾）

### 5.1 背景与策略

HAL 头文件（hal/hal_*.h）定义了统一的 hal_ 前缀接口规范，但内核代码仍使用旧的 RISC-V 原名（plicinit、uartputc_sync、swtch 等）调用。为给 LoongArch 移植提供清晰的实现契约，采用**薄包装策略**统一接口命名：

- 在 `hal/riscv/` 平台代码中添加 hal_ 前缀函数，转发到原有实现
- 同步更新 kernel/ 调用方

### 5.2 新增包装函数清单

**arch.h — 19 个内联包装**

```c
// CPU 控制
static inline uint64 hal_get_hartid(void)     { return r_tp(); }      // 注意：使用 tp 而非 mhartid
static inline uint64 hal_read_sstatus(void)   { return r_sstatus(); }
static inline void   hal_write_sstatus(uint64 x) { w_sstatus(x); }
static inline uint64 hal_read_sepc(void)      { return r_sepc(); }
static inline void   hal_write_sepc(uint64 x) { w_sepc(x); }
static inline uint64 hal_read_scause(void)    { return r_scause(); }
static inline uint64 hal_read_stval(void)     { return r_stval(); }
static inline uint64 hal_read_stvec(void)     { return r_stvec(); }
static inline void   hal_write_stvec(uint64 x) { w_stvec(x); }
static inline uint64 hal_read_satp(void)      { return r_satp(); }
static inline void   hal_write_satp(uint64 x) { w_satp(x); }
static inline uint64 hal_read_sp(void)        { return r_sp(); }
static inline uint64 hal_read_ra(void)        { return r_ra(); }
// 定时器
static inline uint64 hal_get_time(void)       { return r_time(); }
static inline void   hal_set_timer(uint64 next) { w_stimecmp(next); }
// MMU
static inline void   hal_tlb_flush_all(void)  { sfence_vma(); }
// 中断控制
static inline void   hal_intr_on(void)        { intr_on(); }
static inline void   hal_intr_off(void)       { intr_off(); }
static inline int    hal_intr_get(void)       { return intr_get(); }
```

**hal_plic.c — 4 个 C 包装**

```c
void hal_irq_init(void)           { plicinit(); }
void hal_irq_hart_init(void)      { plicinithart(); }
int  hal_irq_claim(void)          { return plic_claim(); }
void hal_irq_complete(int irq)    { plic_complete(irq); }
```

**hal_uart.c — 5 个 C 包装**

```c
void hal_console_init(void)                  { uartinit(); }
void hal_putchar_sync(int c)                 { uartputc_sync(c); }
void hal_putchar(int c)                      { uartputc_sync(c); }
int  hal_getchar(void)                       { return uartgetc(); }
void hal_console_intr(void (*handler)(int))  { uartintr(); }
```

**hal_start.c — 1 个 C 包装**

```c
void hal_timer_init(void) { timerinit(); }
```

**hal_swtch.S — 1 个汇编别名**

```asm
.globl hal_switch
hal_switch:
swtch:
```

### 5.3 内核调用方更新

| 文件 | 变更 |
|---|---|
| `kernel/main.c` | `plicinit/plicinithart` → `hal_irq_init/hal_irq_hart_init` |
| `kernel/trap.c` | 全部 CSR/PLIC/UART/定时器调用改用 hal_ 前缀（~30 处） |
| `kernel/console.c` | `uartputc_sync/uartinit` → `hal_putchar_sync/hal_console_init` |
| `kernel/proc.c` | `swtch` → `hal_switch`，`r_tp` → `hal_get_hartid`，`intr_*` → `hal_intr_*` |
| `kernel/proc.h` | `struct context {…}` → `#include "hal/hal_ctx.h"` + 字段类型用 `struct hal_context` |
| `kernel/defs.h` | 前向声明和 swtch 签名改用 `struct hal_context` |
| `hal/hal_ctx.h` | 修正 `hal_switch` 第一个参数从双指针改为单指针 |

### 5.4 Bug 修复：hal_get_hartid 特权级错误

- **症状**：内核静默崩溃，QEMU 无任何输出
- **根因**：`hal_get_hartid()` → `r_mhartid()` → `csrr mhartid`。`mhartid` 是 M 态 CSR，`start()` 执行 `mret` 切换到 S 态后无法访问，触发非法指令异常
- **修复**：改为 `r_tp()`（读 `tp` 寄存器）。`start()` 在 M 态时将 hartid 写入 `tp`，S 态通过 `tp` 获取
- **教训**：HAL 包装必须注意 CSR 的特权级访问权限

### 5.5 最终代码规模

| 类别 | 文件数 | 行数 |
|---|---|---|
| HAL 接口头文件（hal/hal*.h） | 8 | 179 |
| RISC-V 平台实现（hal/riscv/） | 11 | 1,382 |
| **总计** | **19** | **1,561** |
| 内核文件修改（kernel/） | 7 | ~100 行变更 |

### 5.6 当前 HAL 接口覆盖矩阵

| HAL 接口 | RISC-V 实现 | 类型 | 内核已迁移 |
|---|---|---|---|
| `hal_get_hartid()` | `r_tp()` | 内联 | ✅ |
| `hal_read_scause()` 等 13 个 CSR | `r_*()/w_*()` | 内联 | ✅ trap.c |
| `hal_intr_on/off/get()` | `intr_on/off/get()` | 内联 | ✅ proc.c, trap.c |
| `hal_irq_init/hart_init/claim/complete()` | `plic*()` | C 函数 | ✅ main.c, trap.c |
| `hal_get_time/set_timer/timer_init()` | `r_time/w_stimecmp/timerinit()` | 内联/C | ✅ trap.c, start.c |
| `hal_tlb_flush_all()` | `sfence_vma()` | 内联 | 就绪 |
| `hal_console_init/putchar_sync/putchar/getchar/intr()` | `uart*()` | C 函数 | ✅ console.c, trap.c |
| `hal_switch()` | `swtch`（汇编） | ASM | ✅ proc.c |

> LoongArch 移植时只需实现以上 hal_ 前缀函数，内核通用代码无需任何修改。

## 六、当前项目状态

| 里程碑 | 状态 |
|---|---|
| RISC-V HAL 全部接口完成 | ✅ |
| HAL 接口命名统一（hal_ 前缀） | ✅ |
| `make qemu` 正常启动 | ✅ |
| usertests 全部通过（ALL TESTS PASSED） | ✅ |
| 崩溃恢复测试 (log/forphan/dorphan) 通过 | ✅ |
| LoongArch 环境搭建 | 🚀 下一步（第4周） |
| LoongArch HAL 实现 | 待开始（第5-7周） |
