# HAL 边界修复 7 天实施规划

## 1. 目标与范围

本轮工作的直接目标是把 `kernel/` 中 10 个 `ARCH_loongarch` 条件块全部移到 HAL，使通用内核只表达“要完成什么”，不再表达“LoongArch 如何完成”。

完成标准：

1. `kernel/*.c`、`kernel/*.h`、`kernel/*.S` 中不再出现 `ARCH_loongarch` 或 `ARCH_riscv`。
2. RISC-V 和 LoongArch 的现有行为保持不变，尤其不能把边界重构与 DMW、页表布局或 UART 策略优化混在同一个提交里。
3. 两个平台均能干净编译、启动到 shell，并通过 `usertests -q`。
4. LoongArch 的高地址双页内核栈及 guard page 仍然通过 PGDH 页表工作。
5. LoongArch 的低两页用户地址别名保护、KSave1 trapframe 传递和 UART 定时轮询不能丢失。
6. HAL 不接收 `struct proc *`，只接收页表、地址、标量和必要的回调，避免 HAL 反向认识进程内部结构。

本轮不做以下工作：

- 不重新设计 DMW0，也不删除当前看似冗余的低地址内核页表映射。
- 不改变 LoongArch 四级页表、TLB refill 汇编或 `MAXVA`。
- 不合并两套 trampoline 汇编。
- 不把编译期 HAL 改成 `struct hal_ops` 运行时函数指针表。
- 不同时改 ext2、FAT32、VFS 或块设备语义。

原因是本轮首先要完成可验证的“等价搬迁”。边界稳定以后，再单独判断哪些映射或兼容宏可以删除。

---

## 2. `kernel/` 中 10 个条件块审计结果

| 编号 | 位置 | 当前职责 | 为什么属于 HAL | 目标接口 | 难度 |
|---|---|---|---|---|---|
| 1 | `kernel/exec.c:54` | LoongArch 为 VA 高位别名保留低两页 PLV0 guard | 这是 LA464 VALEN/TLB 匹配行为的规避策略 | `hal_vm_reserve_user_low()` | 3/5 |
| 2 | `kernel/trap.c:119` | 把当前 trapframe KVA 写入 KSave1 | KSave1 是 LoongArch 专用 CSR 和 trampoline 协议 | `hal_trap_bind_user_frame()` | 2/5 |
| 3 | `kernel/trap.c:187` | 每次 timer tick 轮询 UART RX | 这是 LoongArch QEMU UART 中断 quirk | `hal_console_poll()` | 1/5 |
| 4 | `kernel/proc.c:195` | RISC-V 用户页表映射 trampoline/trapframe，LA 跳过 | 用户 trap 入口如何找到代码和 trapframe 是架构 ABI | `hal_vm_map_user_trap()` | 2/5 |
| 5 | `kernel/proc.c:224` | RISC-V 解除上述两个映射，LA 跳过 | 必须与平台创建的特殊映射生命周期对称 | `hal_vm_unmap_user_trap()` | 2/5 |
| 6 | `kernel/vm.c:34` | 设备 MMIO、内核 text/flash 映射差异 | 物理地址布局、设备总线和链接布局属于平台 | `hal_vm_map_kernel()` | 4/5 |
| 7 | `kernel/vm.c:65` | 内核 data/bss/可用 RAM 映射差异 | LA 的 VMA/LMA 分离和 RAM 起点属于平台 | 合并进 `hal_vm_map_kernel()` | 4/5 |
| 8 | `kernel/vm.c:117` | LoongArch 额外设置 PGDH | PGDL/PGDH 是 LoongArch MMU 寄存器模型 | `hal_vm_enable()` | 5/5 |
| 9 | `kernel/vm.c:126` | 配置 DMW、PWCL/PWCH、RVACFG、TLBRENTRY、CRMD | 完整的 LoongArch MMU 启用序列 | 合并进 `hal_vm_enable()` | 5/5 |
| 10 | `kernel/vm.c:361` | 用 MAT 判断 LoongArch PTE 是否为叶项 | PTE 编码属于架构页表格式 | `hal_pte_is_leaf()` | 1/5 |

10 个条件块最终收敛为 7 个接口职责，其中用户 trap 特殊映射包含 map/unmap 两个生命周期函数。

---

## 3. 接口设计总则

### 3.1 使用编译期链接，不使用函数指针表

RISC-V 和 LoongArch 分别提供同名函数，Makefile 只链接当前平台实现。这样没有热路径间接调用，也不需要运行时平台判断。

### 3.2 接口表达语义，不表达某个寄存器名字

推荐：

```c
void hal_vm_enable(pagetable_t kernel_pagetable);
void hal_trap_bind_user_frame(uint64 trapframe_kva);
```

不推荐：

```c
void hal_write_pgdh(uint64 value);
void hal_write_ksave1(uint64 value);
```

前者让通用层说明“启用内核地址空间”“绑定用户 trapframe”；后者仍然要求通用层理解 LoongArch 寄存器协议。

### 3.3 HAL 不接收 `struct proc *`

例如 `prepare_return()` 只传：

```c
hal_trap_bind_user_frame((uint64)p->trapframe);
```

不能设计成：

```c
hal_prepare_return_hook(p);
```

否则 HAL 必须包含 `proc.h`，以后 `struct proc` 的布局变化也会影响平台代码。

### 3.4 失败回滚由创建特殊映射的一方负责

`hal_vm_map_user_trap()` 和 `hal_vm_reserve_user_low()` 若返回失败，必须保证它们已经创建的叶映射全部回滚。调用者随后才能安全执行 `uvmfree()`，否则 `freewalk()` 会因残留叶项 panic，或者发生重复释放。

### 3.5 第一轮严格保持行为

即使 DMW0 会覆盖部分低地址映射，`hal_vm_map_kernel()` 第一版仍应原样移动当前映射。删除冗余映射必须是后续独立优化，不应与 HAL 边界修复一起进行。

---

## 4. 建议的公共接口

在 `hal/hal_vm.h` 中增加：

```c
// Boot-time kernel address-space construction and per-hart activation.
void hal_vm_map_kernel(pagetable_t kpgtbl);
void hal_vm_enable(pagetable_t kpgtbl);

// Platform-specific mappings required by the user trap path.
int  hal_vm_map_user_trap(pagetable_t pagetable,
                          uint64 trampoline_pa,
                          uint64 trapframe_pa);
void hal_vm_unmap_user_trap(pagetable_t pagetable);

// Reserve platform-required low user VA pages. On success, *initial_sz is
// the first VA available for ELF segments and the reserved range is owned by
// the normal uvmfree(pagetable, sz) path.
int hal_vm_reserve_user_low(pagetable_t pagetable, uint64 *initial_sz);

// pte must be valid. Returns non-zero only for a terminal mapping.
int hal_pte_is_leaf(pte_t pte);
```

在 `hal/hal_arch.h` 中增加：

```c
// Make the current process trapframe discoverable by the platform
// trampoline before returning to user mode.
void hal_trap_bind_user_frame(uint64 trapframe_kva);
```

在 `hal/hal_console.h` 中增加：

```c
// Non-blocking polling hook. It is called with interrupts disabled and must
// invoke handler(c) once for every available input byte.
void hal_console_poll(void (*handler)(int));
```

建议新建：

```text
hal/riscv/hal_vm.c
hal/loongarch/hal_vm.c
```

并在 Makefile 两个平台的 `ARCH_OBJS` 中加入 `hal/$(ARCH)/hal_vm.o`。其余两个小接口直接放入现有 `arch.h` 和 `hal_uart.c`，避免为一行 CSR/no-op 新建额外 C 文件。

---

## 5. 各接口详细实施方案（由易到难）

## 5.1 `hal_pte_is_leaf()`：PTE 叶项判定

### 功能

隐藏 RISC-V 与 LoongArch PTE 的叶项编码差异。`freewalk()` 只需要知道一个有效 PTE 是子页表还是最终映射。

### RISC-V 实现

```c
static inline int
hal_pte_is_leaf(pte_t pte)
{
  return (pte & (PTE_R | PTE_W | PTE_X)) != 0;
}
```

### LoongArch 实现

优先使用 `PTE_P`，而不是继续使用 `PTE_MAT`：

```c
static inline int
hal_pte_is_leaf(pte_t pte)
{
  return (pte & PTE_P) != 0;
}
```

理由：当前 `mappages()` 给每个叶项加入 `PTE_V_CACHE = PTE_V | PTE_MAT | PTE_P`，非叶项只有 `PA2PTE(page) | PTE_V`。物理页对齐保证非叶项不会意外带 bit 7，因此 `PTE_P` 比缓存属性 `PTE_MAT` 更准确地表达“物理页映射存在”。

### 通用层修改

`kernel/vm.c::freewalk()` 改为：

```c
if(pte & PTE_V) {
  if(hal_pte_is_leaf(pte))
    panic("freewalk: leaf");
  ...
}
```

### 风险与验证

- 风险低，但必须确认所有 LA 叶项都由 `mappages()` 创建并带 `PTE_P`。
- 搜索所有直接写 PTE 的位置，确认没有遗漏的叶项编码。
- 运行 `usertests -q`，重点观察 `fork/exec/sbrk/lazy_unmap`，这些路径频繁释放页表。

---

## 5.2 `hal_console_poll()`：平台控制台轮询钩子

### 功能

让通用时钟中断无条件调用控制台轮询钩子，是否需要轮询由平台实现决定。

### 接口契约

- 必须非阻塞。
- 可能在中断关闭、持有时钟相关上下文时调用。
- 不得 `sleep()`。
- 对每个已到达字符调用一次 `handler(c)`。
- 没有字符时立即返回。

### RISC-V 实现

空操作。RISC-V 16550a 由 PLIC RX 中断驱动：

```c
void
hal_console_poll(void (*handler)(int))
{
  (void)handler;
}
```

### LoongArch 实现

在 `hal/loongarch/hal_uart.c` 中复用现有接收逻辑：

```c
void
hal_console_poll(void (*handler)(int))
{
  hal_console_intr(handler);
}
```

### 通用层修改

`kernel/trap.c::clockintr()` 删除条件编译，直接调用：

```c
hal_console_poll(consoleintr);
```

### 风险与验证

- 确认 LoongArch 的 `hal_console_intr()` 不再获取已经删除的异步 TX 锁，也不睡眠。
- QEMU 启动后实际输入 `echo console-ok`，不能只检查启动输出。
- 快速连续输入一整行，确认 timer polling 能清空 RX FIFO。

---

## 5.3 `hal_trap_bind_user_frame()`：返回用户态前绑定 trapframe

### 功能

在进入平台 trampoline 前，建立“当前进程 trapframe 如何被汇编入口找到”的架构协议。

### RISC-V 实现

空操作。RISC-V 通过用户页表中固定的 `TRAPFRAME` VA 找到 trapframe。

```c
static inline void
hal_trap_bind_user_frame(uint64 trapframe_kva)
{
  (void)trapframe_kva;
}
```

### LoongArch 实现

把 KVA 写入 KSave1。建议先在 `arch.h` 增加命名 CSR helper，再由接口调用，不要继续散布裸 `0x31`：

```c
static inline void w_ksave1(uint64 x)
{
  asm volatile("csrwr %0, 0x31" : "+r"(x));
}

static inline void
hal_trap_bind_user_frame(uint64 trapframe_kva)
{
  w_ksave1(trapframe_kva);
}
```

### 通用层修改

`kernel/trap.c::prepare_return()` 删除内联汇编和条件编译，改为：

```c
hal_trap_bind_user_frame((uint64)p->trapframe);
```

顺便把同文件已经有 HAL 等价接口的残留调用机械替换：

- `w_stvec()` 改为 `hal_write_stvec()`。
- `r_sstatus()` 改为 `hal_read_sstatus()`。

### 调用时序要求

- 必须在中断关闭后完成。
- 必须在跳转 `userret` 之前完成。
- 传入值必须是内核可访问的 KVA，并在本次用户运行期间保持有效。
- 不能把 KSave1 写入移动到 `allocproc()`，因为 CPU 可能切换进程，KSave1 是每 CPU 当前上下文状态，不是进程永久状态。

### 风险与验证

- 如果绑定过早，调度切换会让 KSave1 指向错误进程。
- 如果绑定遗漏，第一次用户 trap 会破坏随机内存或直接异常。
- 使用 `echo`、系统调用密集测试、`forktest` 和完整 `usertests -q` 验证。

---

## 5.4 `hal_vm_map_user_trap()` / `hal_vm_unmap_user_trap()`：用户页表的 trap 支撑映射

### 功能

隐藏两种 trampoline/trapframe 访问模型：

- RISC-V：每个用户页表必须映射固定高地址 `TRAMPOLINE` 和 `TRAPFRAME`。
- LoongArch：trampoline 由 PLV0 DMW0 访问，trapframe 由 KSave1 KVA 访问，用户页表无需这两个映射。

### 推荐参数

```c
int hal_vm_map_user_trap(pagetable_t pagetable,
                         uint64 trampoline_pa,
                         uint64 trapframe_pa);
void hal_vm_unmap_user_trap(pagetable_t pagetable);
```

传地址而不传 `struct proc *`，HAL 不需要知道 `p->trapframe` 字段。

### RISC-V map 实现

1. 映射 `TRAMPOLINE -> trampoline_pa`，权限 `PTE_R | PTE_X`，不带 `PTE_U`。
2. 映射 `TRAPFRAME -> trapframe_pa`，权限 `PTE_R | PTE_W`，不带 `PTE_U`。
3. 第二步失败时解除第一步映射。
4. 失败返回 `-1` 时，页表中不能残留任何由本函数创建的叶项。

### RISC-V unmap 实现

按当前顺序解除两个固定映射，`do_free=0`：

```c
uvmunmap(pagetable, TRAMPOLINE, 1, 0);
uvmunmap(pagetable, TRAPFRAME, 1, 0);
```

trapframe 物理页由 `freeproc()` 单独释放，不能在这里释放。

### LoongArch 实现

map 返回 0，unmap 为空操作。参数用 `(void)` 消除告警。

### 通用层修改

`proc_pagetable()`：

1. `uvmcreate()`。
2. 调用 `hal_vm_map_user_trap(pagetable, (uint64)trampoline, (uint64)p->trapframe)`。
3. 失败时只需 `uvmfree(pagetable, 0)`，前提是 HAL 已经回滚叶项。

`proc_freepagetable()`：

1. 先调用 `hal_vm_unmap_user_trap(pagetable)`。
2. 再调用 `uvmfree(pagetable, sz)`。

### 风险与验证

- 最大风险是失败路径残留叶项，最终在 `freewalk()` panic。
- 第二个 `mappages()` 失败时，不能释放 `p->trapframe`；它不归接口所有。
- 执行 `forktest`、`exec` 相关 usertests，并检查反复 exec 失败时没有内存泄漏。

---

## 5.5 `hal_vm_reserve_user_low()`：用户低地址平台保留区

### 功能

封装 LA464/QEMU 对超出 VALEN 的 VA 在 TLB lookup 中可能别名到低 VPPN 的规避方案。当前 LoongArch 用户程序从 `0x2000` 链接，低两页必须存在有效但 PLV0-only 的 PTE，防止非法高地址命中正常用户数据。

### 接口契约

```c
int hal_vm_reserve_user_low(pagetable_t pagetable, uint64 *initial_sz);
```

- 成功返回 0。
- RISC-V 设置 `*initial_sz = 0`，不创建映射。
- LoongArch 创建 `[0, 2 * PGSIZE)` 两页 supervisor-only 映射，并设置 `*initial_sz = 2 * PGSIZE`。
- 成功后，这些页纳入正常的 `uvmfree(pagetable, sz)` 所有权。
- 失败返回 `-1`，设置 `*initial_sz = 0`，释放已分配物理页并移除已建立映射。

### LoongArch 实现步骤

1. `mapped = 0`。
2. 每次 `kalloc()` 一页并清零。
3. 用 `PTE_R` 映射，不加 `PTE_U`。
4. 映射成功后增加 `mapped`。
5. 任一步失败，释放当前尚未映射的页，再用 `uvmunmap(..., do_free=1)` 回滚先前成功的页。
6. 全部成功后令 `*initial_sz = mapped`。

建议在 LoongArch 私有实现中使用有名字的常量，例如：

```c
#define USER_LOW_GUARD_PAGES 2
```

不要把数字 2 或 LA464 的解释重新暴露给 `exec.c`。

### 通用层修改

`kernel/exec.c` 创建页表后：

```c
if(hal_vm_reserve_user_low(pagetable, &sz) < 0)
  goto bad;
```

之后 ELF 段装载和 `bad:` 清理继续使用同一个 `sz`，保持当前所有权逻辑。

### 风险与验证

- `sz` 不只是“ELF 末尾”，它也承担失败清理边界；不能把接口改成仅返回布尔值。
- 不能把低两页简单留成无效 PTE；当前 workaround 的关键是有效但用户不可访问。
- 必须检查 LoongArch `user.ld` 仍从 `0x2000` 开始，任何低于 `initial_sz` 的 PT_LOAD 段应明确拒绝或触发 remap panic。建议接口完成后在 `exec.c` 增加显式范围校验，而不是依赖 `mappages: remap` panic。
- 完整运行 `MAXVAplus`、`kernmem`、`exec`、`badarg`、`lazy_*` 相关测试。

---

## 5.6 `hal_vm_map_kernel()`：建立平台内核映射

### 功能

把物理设备布局、链接脚本布局和内核 RAM 布局从 `kvmmake()` 移入平台 VM 实现。

### 接口契约

```c
void hal_vm_map_kernel(pagetable_t kpgtbl);
```

- 输入页表已经分配并清零。
- 仅在启动阶段调用一次。
- 只建立平台固有映射，不启用 MMU。
- 映射失败沿用当前 `kvmmap()` 的 panic 语义。
- 不负责通用的 trampoline 映射和 `proc_mapstacks()`；这两项继续由 `kvmmake()` 调用。

### RISC-V 实现必须搬入的内容

1. UART 页。
2. PLIC 区域。
3. `VIRTIO0`，以及由 `VIRTIO_NDISK` 控制的 `VIRTIO1/VIRTIO2`。
4. `[KERNBASE, etext)` 的只读可执行映射。
5. `[etext, PHYSTOP)` 的可读写 RAM 映射。

### LoongArch 实现必须搬入的内容

1. UART 页。
2. EIOINTC 区域；不再依赖通用层把它伪装成 `PLIC`。
3. flash 中 trampoline 前后的只读可执行代码区。
4. `_data_start` 到 `_bss_end` 的 RAM data/bss 映射。
5. `end` 对齐后到 `PHYSTOP` 的可分配 RAM 映射。
6. 保持当前 trampoline 页从 flash text 映射中挖洞的行为。

平台链接符号应留在各自实现中：

- RISC-V：`etext`。
- LoongArch：`_data_lma`、`_trampoline`、`_data_start`、`_bss_end`、`end`。

### 修改后的 `kvmmake()` 结构

```c
pagetable_t kpgtbl = (pagetable_t)kalloc();
if(kpgtbl == 0)
  panic("kvmmake");
memset(kpgtbl, 0, PGSIZE);

hal_vm_map_kernel(kpgtbl);
kvmmap(kpgtbl, TRAMPOLINE, (uint64)trampoline,
       PGSIZE, PTE_R | PTE_X);
proc_mapstacks(kpgtbl);
return kpgtbl;
```

这里建议顺便补上当前缺失的 `kalloc()` 空指针检查，但应作为同一小提交中的独立一行说明，不要夹带其他 VM 优化。

### HAL 与通用 VM 的依赖规则

平台 `hal_vm.c` 可以调用以下窄机制服务：

- `kvmmap()` / `mappages()` / `uvmunmap()`；
- `kalloc()` / `kfree()`；
- `panic()`。

但不能包含 `proc.h`，不能访问进程表，不能调用调度器、VFS 或文件系统。这里的调用关系是“平台策略使用通用页表机制”，不会形成递归：`kvmmake -> hal_vm_map_kernel -> kvmmap -> mappages`。

### 风险与验证

- LoongArch 链接符号同时涉及 VMA/LMA，不能把 `_data_lma` 错当 RAM 地址。
- trampoline 洞的起止必须保持页对齐，否则可能产生可写/可执行权限重叠。
- 不要在本阶段因为 DMW0 存在而删除低地址映射。
- 启动后检查三 hart 均进入 scheduler，运行 shell、`forktest` 和 `usertests -q`。

---

## 5.7 `hal_vm_enable()`：每 hart 启用 MMU

### 功能

完整封装“让当前 CPU 使用内核页表”的架构序列。这是风险最高的接口，因为错误的 CSR 顺序可能在 MMU 开启瞬间失去取指、栈或 trap 能力。

### 接口契约

```c
void hal_vm_enable(pagetable_t kpgtbl);
```

- 每个 hart 调用一次。
- 调用时内核页表已经完整建立。
- 不分配内存、不获取锁、不睡眠。
- 不改变调用者期望的中断状态。
- 返回后当前 hart 能访问内核代码、data/bss、设备和高地址内核栈。
- 必须保留当前启用序列中的全部 TLB 刷新点。

### RISC-V 实现

严格保持当前三步：

1. `sfence.vma`。
2. `satp = MAKE_SATP(kpgtbl)`。
3. `sfence.vma`。

优先调用已经存在的 HAL/arch helper，不在新 C 文件重复内联汇编。

### LoongArch 实现顺序

第一版必须按照当前 `kvminithart()` 的有效顺序原样迁移：

1. 刷新 TLB/页表写入可见性。
2. 设置 PGDL 为内核页表。
3. 设置 PGDH 为内核页表；高地址内核栈依赖它。
4. 再次刷新 TLB。
5. 配置 DMW0 为 PLV0、cacheable、VSEG=0，清空 DMW1。
6. 配置 PWCL/PWCH，必须与四级 `walk()` 和 `hal_tlbrefill.S` 的 9-9-9-9-12 划分完全一致。
7. 设置 RVACFG=8，保留当前硬件 VA 有效范围约束。
8. 设置 TLBRENTRY 为 `tlb_refill_entry`。
9. 原子更新 CRMD：清 DA，置 PG。
10. 写 CRMD 后按当前实现直接返回；第一版不自行增加或删除刷新点。

建议在 `hal/loongarch/arch.h` 中补齐命名 helper：

```c
void w_pwcl(uint64);
void w_pwch(uint64);
void w_tlbrentry(uint64);
```

同时用有名字的字段构造宏替代 `0x1C/0x1D/0x88` 裸 CSR 编号。即使来不及整理字段宏，也必须先把裸 CSR 限制在 LoongArch HAL 内，不能继续留在 `kernel/vm.c`。

### 通用层修改

`kvminithart()` 最终只保留：

```c
void
kvminithart(void)
{
  hal_vm_enable(kernel_pagetable);
}
```

### 关键风险

1. **PGDH 遗漏**：高地址内核栈第一次访问就 fault。
2. **DMW 过早关闭或配置错误**：当前正在执行的 flash text、data 或 UART 立即不可达。
3. **PWCL/PWCH 与软件 walk 不一致**：用户 TLB refill 会读取错误层级。
4. **TLBRENTRY 未在每 hart 设置**：辅助 hart 的用户页故障无法重填。
5. **MAKE_SATP 语义不同**：RISC-V 包含模式和 PPN，LoongArch 是纯页表基址，不能在通用层自行位移。

### 分级验证

1. 单 hart 启动到 `start: calling main` 之后。
2. 单 hart 启动到 shell。
3. 三 hart 启动并执行 `echo`、`ls`、`forktest`。
4. 运行 `usertests -q`。
5. 重点检查 `copyin/copyout/copyinstr`、`MAXVAplus`、`lazy_*`、`stacktest` 和抢占测试。

---

## 6. 文件修改清单

### 新增文件

| 文件 | 内容 |
|---|---|
| `hal/riscv/hal_vm.c` | RISC-V 内核映射、MMU 启用、用户 trap 映射、低地址保留空实现 |
| `hal/loongarch/hal_vm.c` | LA 内核映射、MMU 启用、用户 trap 空映射、低两页保护 |

### 修改公共 HAL 文件

| 文件 | 修改 |
|---|---|
| `hal/hal_vm.h` | 增加 VM 建图、启用、用户特殊映射、低地址保留接口契约 |
| `hal/hal_arch.h` | 增加 `hal_trap_bind_user_frame()` 声明 |
| `hal/hal_console.h` | 增加非阻塞 `hal_console_poll()` 声明 |
| `hal/riscv/arch.h` | RISC-V `hal_pte_is_leaf()` 和 trapframe bind 空实现 |
| `hal/loongarch/arch.h` | LA `hal_pte_is_leaf()`、KSave1 bind、可选 CSR 命名 helper |

### 修改平台已有文件

| 文件 | 修改 |
|---|---|
| `hal/riscv/hal_uart.c` | 增加 `hal_console_poll()` 空实现 |
| `hal/loongarch/hal_uart.c` | 增加 `hal_console_poll()`，复用 RX drain |

### 修改通用内核文件

| 文件 | 删除的条件块 | 替换方式 |
|---|---:|---|
| `kernel/vm.c` | 5 | `hal_vm_map_kernel()`、`hal_vm_enable()`、`hal_pte_is_leaf()` |
| `kernel/proc.c` | 2 | `hal_vm_map_user_trap()` / `hal_vm_unmap_user_trap()` |
| `kernel/trap.c` | 2 | `hal_trap_bind_user_frame()`、`hal_console_poll()` |
| `kernel/exec.c` | 1 | `hal_vm_reserve_user_low()` |
| `Makefile` | 0 | 两个平台对象列表加入 `hal_vm.o` |

---

## 7. 七天执行安排

## 第 1 天：建立基线并完成两个最低风险接口

任务：

1. 保存当前双架构干净编译和测试结果。
2. 在 `hal_vm.h`、`hal_arch.h`、`hal_console.h` 写接口契约。
3. 实现 `hal_pte_is_leaf()`。
4. 实现 `hal_console_poll()`。
5. 删除 `freewalk()` 和 `clockintr()` 的两个条件块。

当日验收：

- 两架构 `-Wall -Werror` 编译通过。
- LoongArch shell 能实际接收输入。
- `rg "ARCH_loongarch" kernel` 从 10 处降到 8 处。

建议提交：

```text
hal: abstract PTE leaf detection and console polling
```

## 第 2 天：trapframe 绑定和用户 trap 特殊映射

任务：

1. 实现 `hal_trap_bind_user_frame()`。
2. 实现 `hal_vm_map_user_trap()` 和 `hal_vm_unmap_user_trap()`。
3. 修改 `prepare_return()`、`proc_pagetable()`、`proc_freepagetable()`。
4. 人工审查所有失败回滚和 trapframe 所有权。

当日验收：

- `forktest` 通过。
- 连续执行多个用户命令，系统调用进入/返回正常。
- 页表释放路径无 `freewalk: leaf`。
- 条件块降到 5 处。

建议提交：

```text
hal: encapsulate per-process user trap mappings
```

## 第 3 天：低地址别名 guard

任务：

1. 实现 `hal_vm_reserve_user_low()`。
2. 完整实现分配失败和第二页映射失败的回滚。
3. 修改 `exec.c`，保持 `sz` 的清理语义。
4. 增加 PT_LOAD 低于保留边界时的显式失败检查，避免 remap panic。

当日验收：

- LoongArch 普通用户程序仍从 `0x2000` 运行。
- `exec` 失败不会泄漏页或触发 `freewalk` panic。
- `MAXVAplus` 和 `kernmem` 通过。
- 条件块降到 4 处。

建议提交：

```text
hal: move low user VA reservation behind VM policy
```

## 第 4 天：平台内核映射下沉

任务：

1. 新建两套 `hal_vm.c` 并加入 Makefile。
2. 原样迁移 UART、PLIC/EIOINTC、virtio、text、data/bss、RAM 映射。
3. 保持通用 trampoline 和高地址内核栈映射在 `kvmmake()`。
4. 删除 `kvmmake()` 中两个条件块。
5. 检查链接符号和每段权限。

当日验收：

- 两架构均能启动到 shell。
- LoongArch 三 hart 都能进入 scheduler。
- 高地址内核栈仍通过 PGDH 使用，guard 设计没有被绕回 DMW0。
- 条件块降到 2 处。

建议提交：

```text
hal: move kernel address-space layout into platform VM backends
```

## 第 5 天：MMU 启用序列下沉

任务：

1. 实现两平台 `hal_vm_enable()`。
2. 给 LA 的 PWCL/PWCH/TLBRENTRY 增加命名 helper。
3. 把 PGDL/PGDH、DMW、RVACFG、CRMD 和 TLB flush 顺序整体迁移。
4. 将 `kvminithart()` 缩减成一次 HAL 调用。

当日验收：

- `rg "ARCH_(loongarch|riscv)" kernel --glob '*.[chS]'` 无输出。
- 单 hart、三 hart LoongArch 均可启动。
- RISC-V 可启动。
- 完整 `usertests -q` 至少在 LoongArch 通过一次。

建议提交：

```text
hal: encapsulate per-hart MMU activation
```

## 第 6 天：双架构回归和边界审计

任务：

1. 在不同临时构建目录或每次 `make clean` 后分别构建两架构。
2. 两平台完整运行 `usertests -q`。
3. LoongArch 运行 `fat32test`；ext2 如时间允许只做 mount 和只读冒烟。
4. 扫描通用层中的裸 CSR、`asm volatile`、KSave、PGDH、DMW、PWCL 等平台词。
5. 只做机械替换：例如已有 HAL 接口的 `w_stvec/r_sstatus`。

注意：Makefile 的对象文件路径不包含架构名，切换 `ARCH` 不会自动识别 CFLAGS 变化，因此必须 clean 或使用两个独立工作副本，不能复用上一架构的 `.o`。

当日验收：

```sh
rg -n "ARCH_(loongarch|riscv)" kernel --glob '*.[chS]'
rg -n "KSave|PGDH|DMW|PWCL|PWCH|TLBRENTRY" kernel --glob '*.[chS]'
git diff --check
```

前两条应无输出，第三条应成功。

## 第 7 天：稳定性复测、diff 审查和文档收口

任务：

1. 再跑一次两平台干净编译。
2. LoongArch 运行完整 `usertests -q` 和 `fat32test`。
3. RISC-V 运行完整 `usertests -q`。
4. 按接口逐项检查前置条件、所有权、失败回滚、是否允许睡眠。
5. 检查 diff，拒绝与 HAL 边界无关的格式化或 VM 优化。
6. 在 HAL 文档记录 7 个接口的契约和双架构实现差异。

最终建议提交：

```text
docs: record the platform-independent kernel boundary
```

---

## 8. 每个提交必须执行的最小验证

### 静态验证

```sh
git diff --check
rg -n "ARCH_(loongarch|riscv)" kernel --glob '*.[chS]'
```

### 编译验证

RISC-V：

```sh
make clean
make ARCH=riscv -j2 kernel/kernel
```

LoongArch：

```sh
make ARCH=loongarch clean
make ARCH=loongarch -j2 kernel/kernel.bin
```

### 每日冒烟

启动到 shell 后至少执行：

```text
echo hal-boundary-ok
ls
forktest
```

### 关键节点全量测试

第 3、5、7 天运行：

```text
usertests -q
```

LoongArch 第 7 天额外运行：

```text
fat32test
```

测试中的预期非法地址 page fault 后跟 `OK` 不算失败，最终必须出现：

```text
ALL TESTS PASSED
```

---

## 9. 高风险点检查表

- [ ] `hal_vm_enable()` 在每个 hart 上设置 PGDH 和 TLBRENTRY。
- [ ] DMW0 配置与 CRMD.DA/PG 切换顺序未改变。
- [ ] PWCL/PWCH 仍与四级软件 `walk()` 和 TLB refill 汇编一致。
- [ ] LoongArch 高地址内核栈仍由 PGDH 映射，没有落回 DMW0。
- [ ] `hal_vm_map_user_trap()` 失败时不残留 trampoline 叶项。
- [ ] `hal_vm_unmap_user_trap()` 不释放独立所有的 trapframe 物理页。
- [ ] `hal_vm_reserve_user_low()` 部分失败时没有页泄漏或重复释放。
- [ ] 低两页成功映射后纳入 `sz`，最终由 `uvmfree()` 释放。
- [ ] KSave1 在每次返回用户态前绑定当前进程，而不是只在进程创建时设置。
- [ ] `hal_console_poll()` 不睡眠，RISC-V 实现确实为空操作。
- [ ] `freewalk()` 不再知道 MAT、R/W/X 等平台叶项编码。
- [ ] `kernel/` 中没有用新的 capability `#if` 变相替代旧 `ARCH_loongarch`。

---

## 10. 不建议采用的接口形式

### 10.1 一个万能 hook

```c
void hal_arch_hook(int stage, void *arg);
```

问题：无类型、无所有权契约、调用顺序只能靠魔数，评审时无法从接口判断用途。

### 10.2 把 `struct proc *` 传入 HAL

```c
void hal_prepare_return(struct proc *p);
```

问题：HAL 与进程结构耦合，平台层可以任意修改通用进程状态，边界比当前更差。

### 10.3 只用布尔 capability 隐藏 ifdef

```c
if(hal_needs_low_guard()) {
  // 仍在 exec.c 实现 LoongArch guard
}
```

问题：虽然 `ARCH_loongarch` 消失了，但平台机制仍留在通用层，只是把条件编译换成运行时分支。

### 10.4 在本轮顺手删除 DMW 覆盖的映射

问题：会同时改变边界和地址转换行为。一旦启动失败，无法快速判断是代码搬迁错误还是映射优化错误。

---

## 11. 边界完成后的后续审计项

以下内容没有直接形成这 10 个 `ARCH_loongarch` 条件块，不应阻塞本轮，但仍是 HAL 边界的下一阶段：

1. `trap.c::devintr()` 仍使用规范化后的 RISC-V scause 数字和 `VIRTIO*_IRQ` 宏。可给 `hal_read_scause()` 的规范化契约增加命名常量，或增加 trap 分类接口。
2. `virtio_disk_init/rw/intr` 的名称仍泄漏具体硬件；LoongArch 实际实现是 loader-backed RAM disk。后续可改为 `hal_disk_*`。
3. LoongArch `memlayout.h` 中 `PLIC=EIOINTC` 和 `VIRTIO0` 兼容别名在 `hal_vm_map_kernel()` 完成后可能失去用途，应重新搜索再删除。
4. `hal_set_timer(next)` 在两个平台的语义仍不完全一致，需要单独统一定时器契约。
5. 边界完成后再评估 DMW0 下哪些低地址内核映射确实可以删除，不能根据“看似未使用”直接删。

---

## 12. 最终验收结论模板

完成后应能够给出以下可复核结论：

```text
1. kernel/ 中 ARCH_loongarch/ARCH_riscv 条件编译：0 处。
2. 原 10 个条件块已由 7 个有类型、有契约的 HAL 职责覆盖。
3. RISC-V clean build：通过；usertests -q：ALL TESTS PASSED。
4. LoongArch clean build：通过；usertests -q：ALL TESTS PASSED。
5. LoongArch fat32test：ALL PASSED。
6. VM 启用、用户 trap 映射和低地址 guard 的失败回滚已逐项审查。
7. 本轮只做行为等价的边界迁移，没有夹带 DMW/页表布局优化。
```
