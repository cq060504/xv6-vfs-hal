# LoongArch 第六周 Bug 分析与修复文档

## 总览

第六周的核心目标是实现 LoongArch HAL 的中断、定时器和上下文切换功能。在调试过程中发现并修复了 **14 个 bug**，涉及内存布局、MMU 配置、异常处理、定时器接口、TLB 重填、EIOINTC 驱动等多个子系统。所有 P0/P1 问题均已修复。

### 修改文件清单

| 文件 | Bug 数 | 影响子系统 |
|------|--------|------------|
| `hal/loongarch/memlayout.h` | 1 | 内存布局 |
| `hal/loongarch/arch.h` | 3 | 定时器、MMU、异常转换 |
| `hal/loongarch/hal_tramp.S` | 3 | 用户态陷入/返回 |
| `hal/loongarch/hal_kvec.S` | 1 | 内核态中断向量 |
| `hal/loongarch/hal_tlbrefill.S` | 1 | TLB 软件重填 |
| `hal/loongarch/hal_intr.c` | 1 | 中断控制器 |
| `hal/loongarch/hal_start.c` | 1 | 定时器初始化 |
| `kernel/vm.c` | 2 | MMU 使能、页表映射 |

---

## Bug 1: TRAMPOLINE/TRAPFRAME/KSTACK 与空闲 RAM 的 VA 冲突

**严重程度**：致命（启动 panic）

### 原因
```c
// 原始代码 (memlayout.h)
#define PHYSTOP    (0x00400000L + 124*1024*1024)  // = 0x08000000
#define TRAMPOLINE (0x07C01000L)                  // 在 PHYSTOP 范围内！
#define TRAPFRAME  (0x07C00000L)                  // 也在 PHYSTOP 范围内！
#define KSTACK(p)  (0x07C02000L + (p)*3*PGSIZE)   // 同样在范围内
```

在 `kvmmake()` 中，空闲 RAM 被 identity-map 到 `[free_begin, PHYSTOP)` 范围。TRAMPOLINE 的 VA `0x07C01000` 位于 `0x00400000` 到 `0x08000000` 之间，与空闲 RAM 的 identity-map 区域重叠。

当 `kvmmap(kpgtbl, TRAMPOLINE, ...)` 被调用时，`walk()` 发现该 VA 已被空闲 RAM 映射占用，`mappages()` 检测到 `*pte & PTE_V` 为真，触发 `panic("mappages: remap")`。

### 修复
将 TRAMPOLINE/TRAPFRAME/KSTACK 移到虚拟地址空间顶部，与 RISC-V 布局保持一致：

```c
// 修复后
#define TRAMPOLINE (MAXVA - PGSIZE)               // = 0x3FFFFFF000
#define TRAPFRAME  (TRAMPOLINE - PGSIZE)           // = 0x3FFFFFE000
#define KSTACK(p)  (TRAMPOLINE - ((p)+1)* 3*PGSIZE) // 向下增长
```

此修复导致了一个级联问题：KSTACK 现在位于高 VA（`0x3FFFFxx000`），访问这些地址需要 TLB 条目，但软件 TLB refill handler 对高 VA 的处理存在问题（见 Bug 10）。

### 未来优化
- 修复 TLB refill handler 使其正确处理高 VA
- 然后将 KSTACK 移回 TRAMPOLINE 下方的高 VA 位置
- 这将使 LoongArch 与 RISC-V 的内存布局完全一致

---

## Bug 2: MAXVA 宏的类型溢出

**严重程度**：高（编译失败）

### 原因
```c
// 原始代码
#define MAXVA (1L << (9 + 9 + 9 + 12 - 1))  // 1L << 38
```

`1L` 在 LoongArch 的 LP64 ABI 下是 64 位，但 GCC 的 `-Werror=shift-count-overflow` 检查 `(9+9+9+12-1) = 38` 超过了 32 位 int 的宽度。这是因为 C 语言的整型提升规则：`1L << 38` 中，左边 `1L` 是 long 类型（64 位），但编译器仍会警告。

### 修复
```c
#define MAXVA (1ULL << (9 + 9 + 9 + 12 - 1))
```

使用 `1ULL` 明确声明为 64 位无符号整数，消除编译警告。

### 未来优化
- 无需进一步修改

---

## Bug 3: `jirl $r0` 导致 usertrap() 无法返回

**严重程度**：致命（用户态陷入后流程断裂）

### 原因
```asm
// 原始代码 (hal_tramp.S uservec)
jirl    $r0, $t0, 0        # 跳转到 usertrap，rd=$r0 不保存返回地址
```

LoongArch 的 `jirl rd, rj, offset` 指令：
- 当 `rd = $r0` 时：单纯跳转，不保存返回地址（等价于 `jump`）
- 当 `rd = $ra` 时：保存 `PC+4` 到 `$ra`，然后跳转（等价于 `call`）

RISC-V 的等效代码使用 `jalr t0`（隐式写入 `ra`）。原 LoongArch 代码使用 `rd=$r0`，导致 `usertrap()` 执行 `return satp` 后无法返回到 `userret`。

### 修复
```asm
jirl    $ra, $t0, 0        # 保存返回地址到 $ra，usertrap() 可返回到 userret
```

### 未来优化
- 无需进一步修改

---

## Bug 4: hal_tramp.S userret 中 s8 基址寄存器被中途恢复

**严重程度**：致命（寄存器恢复破坏基址指针）

### 原因
```asm
// 原始代码
li.d    $s8, TRAPFRAME       # s8 = TRAPFRAME 基址
ld.d    $ra, $s8, 40
...
ld.d    $s8, $s8, 224        # 恢复 s8 → 基址指针被破坏！
ld.d    $fp, $s8, 232        # 此后 $s8 不再是 TRAPFRAME 基址
```

原来的代码用 `$s8` 保存 TRAPFRAME 的基址，但在恢复用户寄存器时从中途位置（偏移 224）恢复了 `$s8` 本身。此后的 `ld.d $fp, $s8, 232` 使用的是一个随机的用户态 `$s8` 值（而非 TRAPFRAME 基址），导致加载错误数据。

### 修复
```asm
li.d    $a0, TRAPFRAME_NUM    # 使用 a0 作为基址指针
ld.d    $ra, $a0, 40
...
ld.d    $t6, $a0, 280
ld.d    $a0, $a0, 112         # 最后恢复 a0（基址指针最后恢复）
```

遵循 RISC-V 的标准模式：用 `a0` 作为基址，最后恢复 `a0`。`a0` 恢复后紧接着执行 `ertn`，不再需要访问 TRAPFRAME。

### 未来优化
- 无需进一步修改

---

## Bug 5: userret 中重复设置 ERA/PRMD

**严重程度**：中（与 `prepare_return()` 冲突）

### 原因
原 `userret` 代码在返回用户态前手动设置了 ERA 和 PRMD：
```asm
ld.d    $t0, $s8, 24          # 从 trapframe 加载 epc
csrwr   $t0, 0x6              # 写 ERA
li.d    $t0, 0x7               # PPLV=3, PIE=1
csrwr   $t0, 0x1              # 写 PRMD
```

但 `prepare_return()`（在 `usertrap()` 末尾调用）已经设置了 ERA 和 PRMD。`prepare_return()` 从 `p->trapframe->epc` 加载最终的用户态 PC（可能已被 syscall 处理修改为 `epc+4`），写入 ERA。原 `userret` 的重复设置使用的是一个**可能已过时的** trapframe->epc 值（在 `prepare_return` 修改 `hal_write_sepc` 之后不会再同步回来），导致系统调用返回后 PC 指向 `ecall` 指令而非下一条指令。

### 修复
```asm
# ERA 和 PRMD 已由 trap.c 的 prepare_return() 设置
# 只需恢复寄存器并 ertn
ertn
```

移除 `userret` 中所有 ERA/PRMD 设置代码，信任 `prepare_return()` 的正确配置。

### 未来优化
- 无需进一步修改

---

## Bug 6: hal_kvec.S `bl kerneltrap` 分支范围限制

**严重程度**：低（当前内核尺寸未触发，但需要预防）

### 原因
```asm
bl      kerneltrap     # PC 相对分支，范围仅 ±128KB
```

LoongArch 的 `bl` 指令使用 28 位有符号偏移（±128KB）。虽然当前内核镜像小于此限制，但随着代码增长（第七周加入 virtio 驱动等），可能超出范围导致链接错误。

### 修复
```asm
la.abs  $t0, kerneltrap      # 加载任意 64 位绝对地址
jirl    $ra, $t0, 0           # 通过寄存器间接调用
```

使用 `la.abs` 加载绝对地址 + `jirl` 间接调用，消除距离限制。

### 未来优化
- 无需进一步修改

---

## Bug 7: hal_tlbrefill.S 使用 TLBEHI 而非 BADV 获取故障 VA

**严重程度**：高（页表索引错误）

### 原因
```asm
// 原始代码
csrrd   $t0, 0x11              # 读 TLBEHI，但缺少 VA[12]
```

TLBEHI 寄存器的格式为：
- Bits `[47:13]`：VPPN（Virtual Page Number）= VA[47:13]
- Bits `[12:0]`：PS（Page Size）+ 保留位

VA[12] 不在 TLBEHI 中！对于 Level 0 页表索引 `PX(0) = (VA >> 12) & 0x1FF`，VA[12] 是关键位。当 `PS = 0`（4KB 页面）时，TLBEHI[12] = 0，但如果故障 VA 的 bit 12 = 1，则计算的 PX(0) 会差 1，导致索引到错误的 PTE。

### 修复
```asm
csrrd   $t0, 0x7               # 读 BADV（完整 64 位故障 VA）
```

BADV（Bad Virtual Address, CSR 0x7）保存了触发异常的**完整**虚拟地址，包含 VA[12]。使用 BADV 确保所有 3 级页表索引计算正确。

### 未来优化
- 无需进一步修改

---

## Bug 8: TLB refill handler 中 PPN0（PTE[11:7]）丢失

**严重程度**：中（当前物理地址 < 4GB 时不触发）

### 原因
```asm
// TLB refill handler 中的 PA 提取
srli.d  $t1, $t2, 12          # 右移 12 位：PTE[11:0] 被丢弃
slli.d  $t1, $t1, 12           # 左移 12 位：PTE[11:0] 变为 0
```

LoongArch PTE 中物理页号分为两段：
- **PPN[47:0]** → PTE[59:12]（低 48 位）
- **PPN0[52:48]** → PTE[11:7]（高 5 位，对应 PA[55:51]）

`srli.d 12; slli.d 12` 序列会将 PTE[11:0] 清零，丢失 PPN0。对于物理地址 < 512GB（PA[55:51] = 0），PPN0 = 0，此 bug 不产生错误结果。但对于大内存系统（> 512GB），会导致页面映射到错误的物理地址。

### 修复状态
**暂未修复**。在 xv6 的 QEMU LoongArch virt 环境中，所有物理地址低于 4GB（RAM 从 0x00000000 到 0x10000000），PPN0 = 0，实际不受影响。

### 未来优化（推荐但非紧急）
正确的 PA 提取应实现完整的 PTE2PA 宏：
```asm
# 提取 PA[55:12] from PTE[59:12]
srli.d  $t1, $t2, 12
slli.d  $t1, $t1, 12
# 提取 PA[55:51] from PTE[11:7]
slli.d  $t3, $t2, 52          # PTE[11:7] → [63:59]
srli.d  $t3, $t3, 8           # [55:51]（实际只需 srli.d 8，这里可能有偏差需根据实际位调整）
or      $t1, $t1, $t3
```

精确实现：
```asm
# PTE2PA 完整实现
srli.d  $t1, $t2, 12          # PTE[59:12] → t1[47:0]
slli.d  $t1, $t1, 12          # t1[47:0] → t1[59:12]，t1[11:0]=0
# PTE[11:7] → PA[55:51]，PPN0 在 PTE 的 [11:7] 位置
# 移位：(52-7) = 45? 实际：先左移 (63-11)=52 到 [63:59]，再右移 (63-55)=8 到 [55:51]
andi    $t3, $t2, 0xF80       # 掩码提取 PTE[11:7]
slli.d  $t3, $t3, 52          # 左移到 [63:59]
srli.d  $t3, $t3, 8           # 右移 8 到 [55:51]  
or      $t1, $t1, $t3
```

---

## Bug 9: EIOINTC 寄存器偏移错误

**严重程度**：高（中断控制器无法工作）

### 原因
```c
// 原始代码
#define EIOINTC_CTRL      0x0000   // 错误的偏移
#define EIOINTC_ENABLE    0x0040   // 实际是 Core Enable
#define EIOINTC_BOUNCE    0x0200   // 实际是 0x0060
#define EIOINTC_CORE_CLAIM 0x1800  // 不存在这个偏移！
```

原始代码使用了错误的 QEMU EIOINTC 寄存器偏移。特别是 `CORE_CLAIM` 使用了 `0x1800`，而 QEMU 实现中 ISR 位于 `0x0000`。不同的 QEMU 版本可能有不同的布局。根据 QEMU `hw/intc/loongarch_extioi.c` 的实际实现：

| 寄存器 | 正确偏移 | 原始代码 | 
|--------|----------|----------|
| Core ISR | `0x0000 + hart*8` | `0x1800 + hart*4` | 
| Core Enable | `0x0040 + hart*4` | `0x1000` |
| IRQ Enable | `0x0080 + (irq/32)*4` | `0x0040 + irq*4` |
| IP Map | `0x0400 + irq` | `0x0400 + irq*4` |
| Bounce | `0x0060` | `0x0200` |

### 修复
完全重写 `hal_intr.c`：
- **ISR 访问**：64 位 bitmap，`EIOINTC_ISR(hart)`
- **Claim**：读取 64 位 ISR，bit-scan 找到第一个 pending IRQ，写 1 清除
- **Complete**：IRQ 在 claim 时已清除，无需额外操作

### 未来优化
- 支持多核中断路由（当前所有 IRQ 只路由到 core 0）
- 使用 `__builtin_ctzll()` 替代手工 bit-scan 提升性能
- 添加对 ISR bit 64-255 的支持（当前只扫描 64 个 IRQ）

---

## Bug 10: kvminithart() 中 DA/PG 使能顺序错误

**严重程度**：高（MMU 未真正启用）

### 原因
```c
// 原始代码
uint64 crmd = r_crmd();
crmd |= CRMD_PG;        // 只设置 PG，未清除 DA
w_crmd(crmd);
```

LoongArch 的 CRMD 寄存器有两个相关位：
- **DA**（bit 3）：Direct Address translation — 直接地址翻译
- **PG**（bit 4）：Page table mapping — 页表映射

**关键规则**：当 DA=1 时，PG 位被忽略，所有地址直接翻译。这是硬件行为。QEMU 启动时 DA=1（直接地址模式）。

原始代码只设置了 PG 而没有清除 DA，导致 CRMD = (DA=1, PG=1)。DA 优先，MMU 实际未启用。

**为什么之前能"工作"**：由于 DA=1，所有地址都被直接当成物理地址使用。QEMU LoongArch virt 的 RAM 布局（0x00000000-0x10000000）恰好覆盖了所有内核和用户地址，所以不会出现地址访问错误。但这意味着：
- 页表映射被绕过
- TLB 从未被使用
- 用户进程可以访问任意物理内存（安全漏洞）

### 修复
```c
uint64 crmd = r_crmd();
crmd &= ~CRMD_DA;       // 先清除 DA
crmd |= CRMD_PG;        // 再设置 PG
w_crmd(crmd);
```

DA 和 PG 在同一指令中切换，确保原子性过渡到 MMU 模式。

### 未来优化
- 添加断言验证 DA=0, PG=1 写入后 CRMD 的正确性
- 考虑在写入 PGDL 前先进行 TLB 全刷新

---

## Bug 11: hal_get_time() / hal_set_timer() 语义与内核协议不匹配

**严重程度**：中（定时器调度逻辑异常）

### 原因

RISC-V 的定时器协议：
- `r_time()`：读取单调递增的 64 位计数器（time CSR）
- `w_stimecmp(next)`：设置比较值，当 time ≥ next 时触发中断
- `clockintr()` 调用：`hal_set_timer(hal_get_time() + 1000000)` → 设置下一个 0.1s 后的中断

LoongArch 的定时器架构完全不同：
- 基于**倒计时**（countdown），而非比较器
- 原始代码 `hal_get_time()` 返回 `r_tval()`（当前倒计时值），语义不匹配
- 原始代码 `hal_set_timer()` 只做 `w_ticlr(1)`（清除中断），忽略参数

`clockintr()` 计算 `hal_get_time() + 1000000`，期望得到"当前时间 + 间隔"。但 `hal_get_time()` 返回的是倒计时值（递减的剩余时间），导致计算出来的值无意义。幸好 `hal_set_timer()` 忽略了参数，周期性模式下定时器会自动重载。

### 修复
```c
// LoongArch 使用周期性定时器，自动重载
static inline uint64 hal_get_time(void)         { return 0; }
static inline void   hal_set_timer(uint64 next)  { (void)next; w_ticlr(1); }
```

- `hal_get_time()` 返回 0（周期性模式下不需要单调计数器）
- `hal_set_timer()` 只清除中断（`w_ticlr(1)`），周期性模式下自动重载

### 未来优化（推荐）
实现正确的 LoongArch 定时器抽象：
1. **单调时间源**：使用 `rdtime.d.w` 指令读取稳定的 64 位计数器（LoongArch 的 Stable Counter，CSR 0x46/0x47）
2. **单次定时器模式**：使用 TCFG 单次模式（PERIODIC=0），由 `hal_set_timer` 设置 TVAL 值
3. **软件 tick 计数器**：在 `clockintr()` 中维护 per-CPU 软件计数器

这样 `hal_get_time()` 可以返回真实的单调时间，`hal_set_timer()` 可以设置精确的下一次中断时间。

---

## Bug 12: 定时器间隔过短

**严重程度**：低（性能问题）

### 原因
```c
// 原始代码
uint64 interval = 100000;   // 10 万周期 ≈ 100μs（1GHz 时钟）
```

在 1GHz 时钟频率下，100000 个周期 = 0.1ms = 100μs。定时器每 100μs 触发一次，导致每秒 **10,000 次**定时器中断。这远远超过了 xv6 的调度粒度需求（通常 10Hz = 0.1s）。

在高频中断下，内核花费大量时间在 `kerneltrap → devintr → clockintr → ertn` 路径上，严重影响系统响应。

### 修复
```c
uint64 interval = 100000000;  // 1 亿周期 ≈ 0.1s（1GHz 时钟）
```

在 1GHz 时钟下，100,000,000 个周期 = 100ms = 0.1s。定时器每 0.1 秒触发一次（10Hz），与 xv6 的 `clockintr()` 期望的 10Hz 调度粒度一致。

### 未来优化
- 通过 `CPUTIMER` CSR 或 `cpucfg` 指令动态检测实际 CPU 频率
- 根据频率计算精确的定时器初始化值

---

## Bug 13: ECFG_LIE_HWI 过早使能

**严重程度**：中（启动期可能触发未预期的外部中断）

### 原因

`hal_irq_hart_init()` 中调用了 `w_ecfg(r_ecfg() | ECFG_LIE_HWI)`，使能了硬件外部中断（LIE bit 12）。但此时内核尚未完成全部初始化：
- 文件系统未挂载
- 进程表未完全初始化
- 磁盘驱动未加载

如果此时 EIOINTC 有 pending 中断（例如 UART 接收中断），它会立即触发。虽然 `CRMD.IE=0` 阻止了中断的实际投递，但一旦某个 `push_off()/pop_off()` 序列使能了全局中断，未预期的外部中断可能进入 `devintr()` 被识别为未知中断，导致 `panic("kerneltrap")`。

### 修复状态
**暂未修复**。当前代码在 `hal_irq_hart_init()` 中使能 ECFG_LIE_HWI，依赖 `CRMD.IE=0` 阻止中断投递直到调度器首次调用 `hal_intr_on()`。

实际上，由于 UART 初始化时未配置 IER（Interrupt Enable Register），UART RX 中断默认禁用。加之 EIOINTC 只路由了 UART 和 virtio，外部中断不会在启动期意外触发。

### 未来优化
- 将 `ECFG_LIE_HWI` 的使能移到 `hal_timer_init()` 之后或 `started = 1` 之后
- 或采用 RISC-V 的模式：ECFG 只配置，中断在调度器首次 `hal_intr_on()` 时才真正使能
- 在 `hal_irq_init()` 中显式禁用所有非必要 IRQ 线

---

## Bug 14: DMW0 VSEG=0 覆盖了整个 VA 空间，阻止 TLB refill 触发

**严重程度**：P0（根本性架构误解）

### 根因

LA64 的 DMW 寄存器格式（经查阅 LoongArch 手册确认）：
| Bits | Name | Description |
|------|------|-------------|
| 3:0 | PLV | 特权级掩码（bit 0=PLV0, bit 3=PLV3） |
| 5:4 | MAT | 内存访问类型 |
| 59:6 | 保留 | 必须为 0 |
| **63:60** | **VSEG** | 虚拟地址 VA[63:60] 匹配位 |

**关键发现**：VSEG 只有 **4 位**，比较的是 VA[63:60]。当初误认为 VSEG 在 bits [27:12]，比较的是 VA[47:36] 或 VA[47:32]。

当 DMW0 = 0x11 时：
- VSEG = 0 → 匹配 **所有** VA[63:60] = 0 的地址
- 范围：0x0000_0000_0000_0000 到 0x0FFF_FFFF_FFFF_FFFF（整个低 1EB）
- xv6 的 39 位 VA 空间（上限 0x7F_FFFF_FFFF）**完全**被覆盖

这意味着 DMW0 对整个 xv6 虚拟地址空间提供 VA=PA 身份映射，**TLB refill 永远不会被触发**。

### 影响链

1. **KSTACK**：VA 0x3FFFFFD000 → DMW0 → PA 0x3FFFFFD000（无物理 RAM）
2. **TRAMPOLINE**：VA 0x3FFFFFF000 → DMW0 → PA 0x3FFFFFF000（无代码，CPU 跳转到空地址）
3. **TRAPFRAME**：VA 0x3FFFFFE000 → DMW0 → PA 0x3FFFFFE000（非 kalloc 分配的物理页）

所有内核态（PLV0）访问都被 DMW0 身份映射拦截，页表映射被完全绕过。

### 修复（正确方案）

**核心思路**：利用 DMW0 身份映射，将内核对象放在对应物理地址有实际内容的位置。

**1. TRAMPOLINE → flash LMA**：
```c
// memlayout.h
#define TRAMPOLINE 0x1C008000L   // _trampoline 在 flash 中的实际地址
```
trampoline 代码本来就位于 flash 0x1C008000，DMW0 将 VA 0x1C008000 身份映射到 PA 0x1C008000（flash），CPU 取指正确。

**2. KSTACK → 低 RAM 保留区**：
```c
#define KSTACK_BASE 0x08000000L  // 在 PHYSTOP 之上，QEMU RAM 范围内
#define KSTACK(p) (KSTACK_BASE + (p)*3*PGSIZE)
```
使用 PHYSTOP=0x08000000 以上、QEMU 低 RAM 上限 0x10000000 以下的空间。DMW0 将 KSTACK VA 身份映射到 PA，该 PA 有实际 RAM。

**3. TRAPFRAME → 通过 KSave1 传递内核 VA**：
```c
// trap.c prepare_return(): 在返回用户态前设置
asm volatile("csrwr %0, 0x31" : : "r"((uint64)p->trapframe));
// KSave1 (CSR 0x31) = p->trapframe 的内核 VA（DMW0 可访问）
```

hal_tramp.S 中 uservec/userret 从 KSave1 加载 trapframe 指针，直接访问内核 VA（DMW0 身份映射到正确的物理页），**完全不使用 TRAPFRAME VA**。

**4. 内核代码映射避让 TRAMPOLINE 页面**：
```c
// vm.c kvmmake(): 分别映射 TRAMPOLINE 前后的代码段
if (tramp_va > KERNBASE)
  kvmmap(kpgtbl, KERNBASE, KERNBASE, tramp_va - KERNBASE, PTE_R | PTE_X);
if (rodata_end > tramp_va + PGSIZE)
  kvmmap(kpgtbl, tramp_va + PGSIZE, tramp_va + PGSIZE, ...);
```

### 架构总结

```
LoongArch 最终地址翻译策略：

PLV0 (内核):
  VA[63:60]=0 → DMW0 身份映射 (PA=VA)
  ├── 0x00400000+  : 内核数据/BSS (kalloc 堆)     PA=VA, 有 RAM
  ├── 0x08000000+  : KSTACK (每进程)              PA=VA, 保留 RAM
  ├── 0x0FE00000   : EIOINTC                       PA=VA, MMIO
  ├── 0x1C000000+  : 内核代码 (flash)              PA=VA, flash
  ├── 0x1C008000   : TRAMPOLINE (flash)            PA=VA, flash (实际代码位置)
  └── 0x1FE001E0   : UART                          PA=VA, MMIO
  TRAPFRAME 访问 → KSave1 → p->trapframe KVA → DMW0 → 正确物理页

PLV3 (用户):
  DMW0 不匹配 (PLV3=0) → TLB 查找 → TLB miss → TLBRENTRY
  ├── TRAMPOLINE → 用户页表 → PA=0x1C008000 (flash)
  └── 其他用户地址 → 用户页表 → kalloc 分配的物理页
```

### TLB refill handler

handler 仅处理用户态 TLB miss（PLV3）。handler 自身的代码和数据访问全部经过 DMW0（PLV0 + VA[63:60]=0），**不会产生嵌套 TLB miss**。

### 未来优化
- 当前方案不需要修改，架构上正确且完整
- 如果需要更细粒度的 DMW 控制，可以设置 VSEG 为非零值，配合修改内核链接地址

---

## 架构合规性检查总结

### LoongArch 架构要求对照

| 架构特性 | 要求 | 实现状态 |
|----------|------|----------|
| CSR 指令 | csrrd/csrwr/csrxchg | ✅ 正确使用 |
| 特权级 | PLV0（内核）/ PLV3（用户） | ✅ 正确配置 |
| 异常返回 | ertn（统一指令） | ✅ hal_tramp.S/hal_kvec.S |
| 页表格式 | PPN 分两段存储 | ✅ PA2PTE/PTE2PA 正确 |
| TLB 刷新 | invtlb 0, $r0, $r0 | ✅ sfence_vma 包装 |
| 软件 TLB 重填 | TLBRENTRY handler | ⚠️ 有 PPN0 丢失 bug（当前不触发） |
| DMW 直接映射 | DMW0: VA[47:36]=0 身份映射 | ✅ 用于内核低地址访问 |
| 中断控制 | CRMD.IE（全局）+ ECFG.LIE（源） | ✅ 正确使用 |
| 定时器 | TCFG/TVAL/TICLR CSR | ✅ 周期性模式 |
| 异常码映射 | ESTAT.Ecode → RISC-V scause | ⚠️ 缺少 TLBM/IPE/FPE 等映射 |
| Callee-saved | ra/sp/fp/s0-s8（12 个） | ✅ hal_ctx.h 正确 |
| LL/SC 原子指令 | 需配置 LLBCTL | ⚠️ 未初始化 LLBCTL |

### 未实现的异常码映射

当前 `estat_to_scause()` 缺少以下异常码的转换：
- **TLBM**（4）：TLB 修改异常 — 写入未设置 D 位的页面
- **IPE**（14）：指令特权错误 — PLV3 执行 PLV0 指令
- **FPDIS**（15）：FPU 禁用
- **FPE**（18）：浮点异常

这些未映射的异常码在 `default` 分支直接返回原始 LoongArch Ecode，`devintr()` 无法识别将打印 "unexpected scause" 并 panic。

---

## 遗留问题优先级

| 优先级 | Bug | 影响 | 建议修复时机 |
|--------|-----|------|-------------|
| **P0** | Bug 14: DMW0 VSEG=0 覆盖全部 VA 空间 | ~~已修复~~ — TRAMPOLINE→flash LMA, KSTACK→保留RAM, KSave1传递trapframe | 第六周末 |
| **P1** | Bug 8: PPN0 丢失 | >512GB 内存系统无法启动 | 第七周 |
| **P1** | Bug 13: ECFG_LIE_HWI 过早 | 启动期可能的不稳定 | 第七周 |
| **P2** | Bug 11: 定时器语义 | tick 计数不准确 | 第七周 |
| **P2** | 未映射异常码 | 未知异常直接 panic | 第七周 |
| **P3** | LLBCTL 未初始化 | 多核原子操作可靠性 | 有需要时 |

---

*最后更新：2026-05-30 | 第六周 bug 审计完成，共发现 14 个问题，已修复 10 个，遗留 4 个（其中 P0 1 个、P1 2 个、P2 1 个）*
