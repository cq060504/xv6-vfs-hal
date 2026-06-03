# LoongArch `usertests -q` MAXVA 调试记录

更新时间：2026-06-03

## 结论

LoongArch 版本已经通过：

```sh
python3 test-xv6.py -q usertests
```

关键修复不是继续扩大 LoongArch 用户虚拟地址空间，而是恢复 xv6 原始用户地址契约：

- `MAXVA = 1 << 38`
- `TRAPFRAME = MAXVA - 2 * PGSIZE`
- `USER_TOP = TRAPFRAME`

LoongArch 硬件和 4 级页表仍可表达更宽的 VA，但 xv6 测试，特别是 `MAXVAplus`，要求超过 `MAXVA` 的用户地址不能被用户态读写。最终实现让内核 C 路径和 LoongArch TLB refill 路径都在 `MAXVA` 边界前拒绝这些地址。

## 原始失败现象

`usertests -q` 后段失败在 `MAXVAplus`：

```text
test MAXVAplus:
usertrap(): unexpected scause 0xf pid=6516
  sepc=0x2290 stval=0x400000000000
usertrap(): unexpected scause 0xf pid=6517
  sepc=0x2290 stval=0x800000000000
MAXVAplus: oops wrote 0x0001000000000000
FAILED
```

这说明用户态对高地址的写入没有稳定失败，某些地址经 LoongArch TLB lookup 或软件 refill 后可能别名到低地址页表项。

## 根因

LoongArch QEMU `la464` 的有效虚拟地址位数和 xv6 原始 Sv39 语义不同。之前 LoongArch 侧把 `MAXVA` 放宽到 `1 << 46`，同时 TLB refill 使用 `lddir/ldpte` 按硬件支持的地址位走 4 级页表。

这样会破坏 xv6 的用户地址契约：

- `usertests` 认为 `1 << 38` 以上的地址应失败。
- `sbrk`/lazy allocation 只应增长到 trapframe guard 前。
- 高 VA 在 LoongArch TLB lookup 中可能被截断或按 VALEN 处理，导致 `MAXVAplus` 构造的非法地址没有按 xv6 语义失败。

因此只在 C 层检查 `copyin/copyout` 不够，TLB refill 也必须拒绝越界 VA。

## 主要代码修改

### 1. 恢复 xv6 用户地址上限

`hal/loongarch/arch.h`：

```c
#define MAXVA (1ULL << 38)
```

`hal/loongarch/memlayout.h`：

```c
#define TRAMPOLINE 0x1C009000L
#define TRAPFRAME  (MAXVA - 2*PGSIZE)
#define USER_TOP TRAPFRAME
```

LoongArch trap path 不再依赖用户页表映射高地址 `TRAPFRAME`。trapframe 由 `KSave1` 保存的内核地址访问，低地址 trampoline 通过 PLV0 DMW0 访问。

RISC-V 侧也增加 `USER_TOP = TRAPFRAME`，让 `growproc()` 和 `sys_sbrk()` 用统一名字表达“用户堆最大边界”。

### 2. 用户增长和 lazy fault 统一边界

`kernel/proc.c`：

- `growproc()` 检查 `sz + n < sz` 防溢出。
- `growproc()` 不再直接写死 `TRAPFRAME`，改用 `USER_TOP`。

`kernel/sysproc.c`：

- lazy `sbrk()` 同样以 `USER_TOP` 为上限。

`kernel/vm.c`：

- `copyin()`、`copyinstr()` 对 `va0 >= MAXVA` 直接失败。
- `vmfault()` 拒绝 `va >= MAXVA`。
- `vmfault()` 拒绝 `va < 2 * PGSIZE`，保留低页 guard，避免高地址别名命中低页。
- 已存在 PTE 的 lazy fault 还会检查 `PTE_U` 和写权限，避免把 guard 页重新填入用户 TLB。

### 3. LoongArch exec 低页 guard

`kernel/exec.c` 在 LoongArch 下预先映射低 2 页为非用户 guard：

```c
if(mappages(pagetable, sz, PGSIZE, (uint64)guard, PTE_R) < 0)
  goto bad;
```

这些页没有 `PTE_U`，用户态不能访问。它们的目的不是给用户程序使用，而是让可能被硬件截断到低 VPPN 的非法高地址也落到 PLV0-only 页，最终按权限失败。

因此 LoongArch 用户程序链接地址改为 `0x2000`：

- `hal/loongarch/user.ld` 起始地址设为 `0x2000`
- `Makefile` 中 LoongArch `_forktest` 也用 `-Ttext 0x2000`

### 4. TLB refill 先做边界拒绝

`hal/loongarch/hal_tlbrefill.S` 在 `lddir` 前检查 `TLBRBADV`：

```asm
csrrd   $t0, 0x89
srli.d  $t0, $t0, 38
bnez    $t0, .Ltlbr_invalid
csrrd   $t0, 0x89
srli.d  $t0, $t0, 13
beqz    $t0, .Ltlbr_invalid
```

含义：

- `VA >= 1 << 38`：拒绝。
- `VA < 2 * PGSIZE`：拒绝。

这样非法 VA 不会进入 `lddir/ldpte`，避免高位被 LoongArch 页表 walker 指令截断后命中低页表。

同时 refill 后清掉 `TLBRELO0/1` 中内存 PTE 的 P/W 位：

```asm
bstrins.d $t0, $r0, 8, 7
```

因为这两个 bit 在 TLB entry 中不是通用 PTE 的 P/W 语义，保留会污染 TLB entry 属性。

### 5. 降低硬件有效 VA 并保持软件边界

`kernel/vm.c` 的 LoongArch `kvminithart()` 写 `RVACFG=8`：

```c
w_rvacfg(8);
```

QEMU `la464` 报告 VALEN=48，`RVACFG=8` 可把硬件 mapped-mode 有效 VA 降到约 40 bit。xv6 仍用更严格的 `MAXVA=1<<38`，所以 `RVACFG` 是硬件层保护，软件检查和 refill 检查才是 xv6 测试语义的主边界。

### 6. 启动和链接布局修正

`hal/loongarch/kernel.ld` 固定：

- `.tlbrefill` 在 `0x1c008000`
- trampoline 在 `0x1c009000`
- 其余 text 从 `0x1c00a000` 继续
- boot stack 放入独立 `.bootstack (NOLOAD)`

`hal/loongarch/hal_start.c`：

- boot stack 不再在 `.bss`，避免主核清零 `.bss` 时破坏正在使用的栈。
- 仅 0 号核复制 `.data`、清 `.bss`，其他核等待 `boot_done`。
- 每个核尽早设置 `tp=cpuid`。

这些不是 `MAXVAplus` 的直接根因，但保证 LoongArch 多核启动和 refill/trampoline 固定页布局稳定。

## 验证结果

最终在恢复 `user/usertests.c` 原始 `MAXVA/TRAPFRAME` 检查后执行：

```sh
python3 test-xv6.py -q usertests
```

结果：`usertests -q` 通过。

## 后续可优化项

当前实现优先保证 xv6 测试语义正确。后续可以继续优化：

- 把 LoongArch 用户 VA 策略整理成明确设计文档：教学兼容模式使用 Sv39 范围，实验扩展模式再开放更宽 VA。
- 将低页 guard 的原因写入 `exec.c` 附近注释，避免后续误删。
- 继续清理 LoongArch TLB entry 属性转换，把内存 PTE 到 TLBRELO 的格式差异封装得更明显。
- 增加一个专门回归测试，覆盖 `MAXVA`、`1<<40`、`1<<48` 和低页别名访问。
