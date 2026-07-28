# vm.c 完整代码分析

## 文件总览

`kernel/vm.c`（612 行）是 xv6 的**虚拟内存管理核心**。它负责页表的创建、遍历、映射、查询、释放，以及内核页表的初始化和 MMU 的启动。

### 依赖关系

```
vm.c 包含的头文件:
  param.h, types.h     → 基本类型和常量
  hal/hal.h            → 平台抽象层（arch.h + memlayout.h）
  elf.h                → ELF 加载格式
  defs.h               → 全局函数声明
  spinlock.h           → 自旋锁
  proc.h               → 进程结构体
  fs.h                 → 文件系统
```

### 全局变量

```c
pagetable_t kernel_pagetable;  // 内核页表物理地址，所有 hart 共享
extern char etext[];           // 内核代码段末尾（由 kernel.ld 定义）
extern char trampoline[];      // trampoline 代码（由 hal_swtch.S/hal_tramp.S 定义）
```

### 函数分类

| 分类 | 函数 | 用途 |
|------|------|------|
| **内核页表** | `kvmmake`, `kvminit`, `kvminithart`, `kvmmap` | 启动时构建和激活内核页表 |
| **页表遍历** | `walk` | 核心原语：在页表中查找/创建 PTE |
| **映射操作** | `mappages`, `uvmclear` | 建立和修改 VA→PA 映射 |
| **用户空间** | `uvmcreate`, `uvmalloc`, `uvmdealloc`, `uvmfree`, `uvmcopy` | 用户页表的创建/增长/缩小/释放/复制 |
| **地址翻译** | `walkaddr` | 用户 VA→PA 转换 |
| **数据拷贝** | `copyout`, `copyin`, `copyinstr` | 内核↔用户空间的数据拷贝 |
| **缺页处理** | `vmfault` | 懒分配和 COW 的缺页补页 |
| **释放** | `freewalk`, `uvmunmap` | 递归释放页表结构 |
| **查询** | `ismapped` | 检查 VA 是否已映射 |

---

## 一、内核页表构建

### 1.1 `kvminit()` — 创建内核页表

```
调用: main() → kvminit()，仅 hart 0 调用一次，在 MMU 开启之前
```

```c
void kvminit(void) {
    kernel_pagetable = kvmmake();   // 创建并返回内核页表的物理地址
}
```

`kvminit()` 是一个薄包装，在 MMU 未开启时（DA=1，所有低地址都是物理地址），用 kalloc/memset/mappages 构建页表数据结构。此时 PGDL/PGDH 尚未写入，但**内存中的页表结构已经完整**。

---

### 1.2 `kvmmake()` — 构建内核页表的所有映射

```
调用: kvminit() → kvmmake()
时机: 启动早期，每个 hart 调用 kvminithart() 之前
```

功能：创建内核页表（`kernel_pagetable`），建立以下恒等映射（VA=PA）：

| 映射 | VA 范围 | 权限 | 说明 |
|------|---------|------|------|
| UART0 | `PGROUNDDOWN(0x1FE001E0)` | R\|W | 串口寄存器 |
| PLIC/EIOINTC | `0x0FE00000` 开始的 64MB | R\|W | 中断控制器 |
| VIRTIO(0/1/2) | 特定 MMIO 地址（仅 RISC-V） | R\|W | 磁盘 MMIO |
| 内核代码 | `KERNBASE 到 etext` | R\|X | flash 中的内核代码（仅 RISC-V） |
| RAM 数据+BSS | `_data_start 到 _bss_end` | R\|W | RAM 中的内核数据（仅 LoongArch） |
| 空闲 RAM | `end 到 PHYSTOP` | R\|W | kalloc 可分配的内存 |
| TRAMPOLINE | `TRAMPOLINE`（1 页） | R\|X | 陷阱跳板 |
| **内核栈** | `KSTACK(0..63)` 各 2 页 | R\|W | **调用 proc_mapstacks()** |

#### RISC-V vs LoongArch 差异

```
RISC-V：
  - VIRTIO 磁盘是 MMIO 型，直接映射 VIRTIO0/1/2 地址
  - 内核代码和数据从 KERNBASE(0x80000000) 连续映射到 PHYSTOP
  - 中间需要映射 trampoline 页到内核页表

LoongArch：
  - VIRTIO 是 PCI 型，不直接映射（PCI 枚举访问配置空间）
  - 内核代码在 flash(0x1C000000)，数据在 RAM(0x00400000)，不连续
    需要分开映射，中间要跳过 TRAMPOLINE 所在的一页
  - 空闲 RAM 从 kernel end(0x00420000) 到 PHYSTOP(0x07C00000)
  - TRAMPOLINE 通过 DMW0 访问，但也需要页表映射（给 refill 查）
```

**关键调用：** 第 85 行 `proc_mapstacks(kpgtbl)` 为 64 个进程预先创建所有内核栈的页表映射。

---

### 1.3 `kvmmap()` — 内核映射的薄包装

```
调用: kvmmake() 中约 15 处，以及 proc_mapstacks()
```

```c
void kvmmap(pagetable_t kpgtbl, uint64 va, uint64 pa, uint64 sz, int perm) {
    if(mappages(kpgtbl, va, sz, pa, perm) != 0)
        panic("kvmmap");
}
```

`kvmmap` 对 `mappages` 的包装只做了两个简化：
1. 硬编码 `kpgtbl` 参数名（而非泛型的 `pagetable`），强调这是内核页表专用
2. 把任何失败都转成 panic（启动阶段内存不足 = 无法继续）

---

### 1.4 `kvminithart()` — 开启 MMU

```
调用: main() 中 kvminit() 之后 ⟶ hart 0
      main() 中 secondary hart 等待后 ⟶ hart 1, 2, ...
每个 hart 调用一次
```

功能：对当前 hart 执行以下操作，启用分页。

**RISC-V 路径：**

```c
sfence_vma();                           // ① 内存屏障
w_satp(MAKE_SATP(kernel_pagetable));    // ② 写 satp CSR
sfence_vma();                           // ③ 刷新 TLB
// 完成！Sv39 硬件 walk，无需额外 CSRs
```

**LoongArch 路径（117-170 行）：**

```
步骤 1：写 PGDL + PGDH
  w_satp(MAKE_SATP(kernel_pagetable))   → 实际写 PGDL
  w_pgdh(MAKE_SATP(kernel_pagetable))   → 写 PGDH（固定不变）

步骤 2：sfence_vma（= invtlb）刷新 TLB

步骤 3：配置 DMW0
  w_dmw0(0x11)  → PLV0, MAT=cached, VSEG=0
  DMW0 效果：VA[63:60]=0 且 当前 PLV=0 → VA=PA 恒等映射

步骤 4：配置 PWCL/PWCH（页表 walk 参数）
  PWCL: 四级 walk，每级 9 位索引，页偏移从 bit12 开始
  PWCH: 第四级从 bit39 开始，宽度 9 位

步骤 5：w_rvacfg(8) — 缩减有效 VA 到 40 位
  VA[63:40] 必须是 VA[39] 的符号扩展

步骤 6：设置 TLBRENTRY（TLB refill 入口点）
  TLBRENTRY = tlb_refill_entry 的物理地址

步骤 7：启动 MMU
  CRMD.DA=0（禁用直接地址翻译）
  CRMD.PG=1（启用页表映射）
```

**重要：** `kvminithart()` 的第 167-169 行注释指出，**QEMU la464 的 ldpte 指令在写入 TLBRELO 时自动清除 P(b7) 和 W(b8) 位**，所以 TLB refill handler 不需要手动清除这些位。

---

## 二、核心页表原语

### 2.1 `walk()` — 页表下降算法

```
签名: pte_t *walk(pagetable_t pagetable, uint64 va, int alloc)
作用: 在页表中查找 VA 对应的叶子 PTE 的地址
       alloc=1: 遇到缺失中间表则自动分配
       alloc=0: 遇到缺失则返回 NULL
```

#### 算法流程

```
┌──────────────────────────────────────────────────────────────────┐
│ walk(pagetable, va, alloc)                                       │
│                                                                  │
│ ① 地址合法性检查                                                  │
│   if(!hal_pagetable_va_valid(va)) panic("walk")                  │
│   → 用户 [2*PGSIZE, MAXVA) 或 内核栈 [BOTTOM, TOP)             │
│                                                                  │
│ ② 逐级下降（从 PT_LEVELS-1 到 1，不含 0）                       │
│   for(level = PT_LEVELS-1; level > 0; level--):                 │
│     pte = &pagetable[PX(level, va)]    ← 取本级索引              │
│     if(*pte & PTE_V):                      ← 跟随有效 PTE        │
│       pagetable = PTE2PA(*pte)                                   │
│     else if(alloc):                        ← 分配新中间表         │
│       pagetable = kalloc()                                       │
│       memset(pagetable, 0, PGSIZE)                               │
│       *pte = PA2PTE(pagetable) | PTE_V                            │
│     else:                                 ← alloc=0 → 放弃        │
│       return 0                                                   │
│                                                                  │
│ ③ 返回叶子 PTE 指针                                              │
│   return &pagetable[PX(0, va)]                                   │
└──────────────────────────────────────────────────────────────────┘
```

#### VA 分域（LoongArch 4 级）

```
 63        48 47     39 38     30 29     21 20     12 11       0
┌────────────┬─────────┬─────────┬─────────┬─────────┬──────────┐
│ 符号扩展   │ Level 3 │ Level 2 │ Level 1 │ Level 0 │ offset   │
│ 必须全1/0  │ PG(9bit)│ PU(9bit)│ PM(9bit)│ PT(9bit)│ 12 bit   │
└────────────┴─────────┴─────────┴─────────┴─────────┴──────────┘
      ↑            ↑         ↑         ↑         ↑
   RVACFG=8   PX(3,va)  PX(2,va)  PX(1,va)  PX(0,va)  页内偏移
```

#### alloc 参数的关键区别

| | alloc=1（建表模式） | alloc=0（查找模式） |
|---|---|---|
| **调用者** | mappages, uvmalloc, vmfault | walkaddr, uvmunmap, uvmcopy, uvmclear, ismapped |
| **遇缺失中间表** | kalloc 分配新页表页 | 返回 NULL |
| **修改页表结构** | 是 | **否** |
| **典型场景** | "我要建立映射" | "我查一下有没有映射" |

**walk() 自己不写叶子 PTE。** 它只返回叶子 PTE 的指针。叶子 PTE 的内容由调用者写（mappages 写 PA+flags）或读（walkaddr 读 V/U/PA）。

---

### 2.2 `mappages()` — 建立 VA→PA 映射

```
签名: int mappages(pagetable_t, uint64 va, uint64 size, uint64 pa, int perm)
返回: 0=成功, -1=失败(kalloc失败)
```

```c
int mappages(pagetable_t pagetable, uint64 va, uint64 size, uint64 pa, int perm) {
    // ① 参数检查: va/size 必须页对齐，size > 0
    // ② 循环: 每次处理一页
    for(a = va; a < va + size; a += PGSIZE, pa += PGSIZE) {
        pte = walk(pagetable, a, 1);       // alloc=1
        if(*pte & PTE_V) panic("remap");    // 防止重复映射
        *pte = PA2PTE(pa) | hal_pte_encode_perm(perm) | PTE_V_CACHE;
    }
}
```

**关键：** `hal_pte_encode_perm(perm)` 把通用的权限位（如 `PTE_R|PTE_W`）转换为平台特定的 PTE flag 位。这是 HAL 层提供的统一权限编码接口。

---

### 2.3 `walkaddr()` — 用户 VA→PA 翻译

```
签名: uint64 walkaddr(pagetable_t, uint64 va)
返回: 物理地址，0=失败
```

```c
uint64 walkaddr(pagetable_t pagetable, uint64 va) {
    if(va >= MAXVA) return 0;               // ① 独立防线：拒绝 ≥MAXVA
    pte = walk(pagetable, va, 0);           // ② alloc=0，只查找
    if(pte==0 || !V || !U) return 0;        // ③ 三层检查
    return PTE2PA(*pte);                     // ④ 提取物理地址
}
```

**为什么有自己的 MAXVA 检查？** `walk()` 已被放宽到接受高地址内核栈区域。但 `walkaddr` 只用于用户空间（copyin/copyout），必须保留独立的 MAXVA 防线。**walkaddr 的第①步保证了即使 walk() 接受高地址，walkaddr 也不会把内核栈的物理地址暴露给用户。**

---

## 三、用户空间页表操作

### 3.1 `uvmcreate()` — 创建空用户页表

```c
pagetable_t uvmcreate() {
    pagetable = kalloc();        // 分配根表（一页=4096 字节）
    memset(pagetable, 0, PGSIZE); // 清零所有 512 个 PTE 槽位
    return pagetable;
}
```

刚创建的用户页表只有一个根页表（所有 PTE=0），不含任何映射。

**RISC-V 额外步骤（proc_pagetable）：**
```c
pagetable = uvmcreate();
// RISC-V ONLY:
mappages(pagetable, TRAMPOLINE, PGSIZE, trampoline, PTE_R|PTE_X);
mappages(pagetable, TRAPFRAME,  PGSIZE, p->trapframe, PTE_R|PTE_W);
// LoongArch 不需要这些映射（DMW0+KSave1 替代）
```

---

### 3.2 `uvmalloc()` — 用户空间增长（急切分配）

```
调用链: sbrk() → growproc() → uvmalloc()
```

```c
uint64 uvmalloc(pagetable_t, uint64 oldsz, uint64 newsz, int xperm) {
    oldsz = PGROUNDUP(oldsz);
    for(a = oldsz; a < newsz; a += PGSIZE) {
        mem = kalloc();         // ① 分配数据页
        memset(mem, 0, PGSIZE); // ② 清零
        mappages(pagetable, a, PGSIZE, mem, PTE_R|PTE_U|xperm); // ③ 映射
    }
}
```

**与 vmfault 的区别：** uvmalloc 是**急切分配**——调用时立即分配所有物理页。xv6 的 sbrk 实现使用混合策略：sbrk 只改 p->sz（懒），真正的分配由之后的 vmfault 触发。uvmalloc 只在 exec 加载程序段时使用。

---

### 3.3 `uvmdealloc()` — 用户空间缩小

```
调用链: sbrk(负值) → growproc() → uvmdealloc()
```

```c
uint64 uvmdealloc(pagetable_t, uint64 oldsz, uint64 newsz) {
    if(newsz >= oldsz) return oldsz;
    npages = (PGROUNDUP(oldsz) - PGROUNDUP(newsz)) / PGSIZE;
    uvmunmap(pagetable, PGROUNDUP(newsz), npages, 1);  // do_free=1
}
```

释放从 newsz 到 oldsz 之间的页——既解除映射又释放物理内存。

---

### 3.4 `uvmcopy()` — fork 时复制用户空间

```
调用链: fork() → kfork() → uvmcopy()
```

```c
int uvmcopy(pagetable_t old, pagetable_t new, uint64 sz) {
    for(i = 0; i < sz; i += PGSIZE) {
        pte = walk(old, i, 0);           // 读父进程叶子 PTE
        if(!pte || !V) continue;          // 懒分配未访问 → 跳过
        pa = PTE2PA(*pte);               // 提取物理地址
        flags = PTE_FLAGS(*pte);         // 保留权限位
        mem = kalloc();                   // 分配新的数据页
        memmove(mem, (char*)pa, PGSIZE);  // 复制内容
        mappages(new, i, PGSIZE, mem, flags); // 写入子进程页表
    }
}
```

**关键行为：**
- **只复制"已经实际分配"的页**（V=1 的叶子 PTE）。懒分配未触发的页保持未映射状态。
- 每个已存在页在子进程中获得**独立的物理页**（深拷贝内容）。
- 失败时回滚已分配的所有页。

---

### 3.5 `uvmclear()` — 清除用户访问权限

```
调用链: exec() → uvmclear()
用途: 在用户栈底部创建 guard page
```

```c
void uvmclear(pagetable_t pagetable, uint64 va) {
    pte = walk(pagetable, va, 0);
    *pte &= ~PTE_U;     // 清除 U 位 → 用户访问触发缺页
}
```

不释放物理页，只修改权限位。这和内核栈 guard（完全不建 PTE）不同——用户栈 guard 保留 PTE 但去掉了 U 位。

---

## 四、数据拷贝函数（内核↔用户）

### 4.1 `copyout()` — 内核 → 用户空间

```c
int copyout(pagetable_t pagetable, uint64 dstva, char *src, uint64 len) {
    while(len > 0) {
        va0 = PGROUNDDOWN(dstva);
        if(va0 >= MAXVA) return -1;          // ① 边界检查

        pa0 = walkaddr(pagetable, va0);      // ② VA→PA
        if(pa0 == 0) {                        // ③ 懒分配补页
            if(vmfault(pagetable, va0, 0) == 0) return -1;
            pa0 = walkaddr(pagetable, va0);   // 重试 walkaddr
        }

        if((*walk(pagetable, va0, 0) & PTE_W) == 0) return -1;  // ④ 只读保护

        n = min(PGSIZE - (dstva - va0), len);
        memmove((void *)(pa0 + (dstva - va0)), src, n);  // ⑤ 实际拷贝
    }
}
```

**流程：VA 边界检查 → VA→PA 翻译 → 缺页时懒分配 → 写权限检查 → memmove 拷贝。** 每次最多拷贝到页边界，跨页时分多次。

---

### 4.2 `copyin()` — 用户空间 → 内核

```c
int copyin(pagetable_t pagetable, char *dst, uint64 srcva, uint64 len) {
    while(len > 0) {
        va0 = PGROUNDDOWN(srcva);
        if(va0 >= MAXVA) return -1;
        pa0 = walkaddr(pagetable, va0);
        if(pa0 == 0) { vmfault(...); pa0 = walkaddr(...); }
        n = min(PGSIZE - (srcva - va0), len);
        memmove(dst, (void *)(pa0 + (srcva - va0)), n);
    }
}
```

与 copyout 对称，区别是不检查 PTE_W（读不需要写权限）。

---

### 4.3 `copyinstr()` — 拷贝用户空间字符串

```c
int copyinstr(pagetable_t pagetable, char *dst, uint64 srcva, uint64 max) {
    while(!got_null && max > 0) {
        // VA→PA 翻译 + 逐字节拷贝直到 '\0' 或 max
        p = (char *)(pa0 + (srcva - va0));
        while(n > 0) {
            if(*p == '\0') { got_null = 1; break; }
            *dst++ = *p++;
        }
    }
}
```

与 copyin 区别：不调用 vmfault（字符串拷贝不应该触发缺页分配），逐字节而非逐块。

---

## 五、页表释放

### 5.1 `uvmunmap()` — 解除映射

```c
void uvmunmap(pagetable_t, uint64 va, uint64 npages, int do_free) {
    for(a = va; a < va + npages*PGSIZE; a += PGSIZE) {
        pte = walk(pagetable, a, 0);   // alloc=0: 不创建
        if(pte==0 || !V) continue;      // 懒分配未触发的页 → 跳过
        if(do_free) kfree(PTE2PA(*pte)); // 可选释放物理页
        *pte = 0;                        // 清空 PTE
    }
}
```

**do_free 参数：**
- `do_free=1`：释放数据页（堆缩小、进程退出）
- `do_free=0`：只清空 PTE，不释放物理页（exec 后释放旧页表前清理 trampoline 映射）

---

### 5.2 `freewalk()` — 递归释放页表页

```c
void freewalk(pagetable_t pagetable) {
    for(i = 0; i < 512; i++) {
        pte_t pte = pagetable[i];
        if(pte & PTE_V) {
            // ★ 区分叶子 PTE vs 中间 PTE
#ifdef ARCH_loongarch
            if(pte & PTE_MAT) panic("freewalk: leaf");
#else
            if(pte & (PTE_R|PTE_W|PTE_X)) panic("freewalk: leaf");
#endif
            freewalk((pagetable_t)PTE2PA(pte)); // 递归释放子表
            pagetable[i] = 0;
        }
    }
    kfree((void*)pagetable);
}
```

**关键约束：必须先在 `uvmunmap(do_free=1)` 中清除所有叶子映射**，然后调用 `freewalk` 递归释放中间页表页。如果 freewalk 遇到了叶子 PTE（带了权限位），说明调用者有 bug（叶子映射未清理完）。

**架构差异——如何区分叶子 PTE vs 中间 PTE：**

| | RISC-V | LoongArch |
|---|---|---|
| 中间 PTE | 只有 V=1，无 R/W/X 位 | 只有 V=1，无 MAT 位 |
| 叶子 PTE | V=1，且有 R/W/X 位 | V=1，且有 MAT=1 位 |
| 检测方法 | `pte & (PTE_R\|PTE_W\|PTE_X)` | `pte & PTE_MAT` |

---

### 5.3 `uvmfree()` — 完整释放用户空间

```c
void uvmfree(pagetable_t pagetable, uint64 sz) {
    if(sz > 0)
        uvmunmap(pagetable, 0, PGROUNDUP(sz)/PGSIZE, 1); // ① 清叶子+释放数据页
    freewalk(pagetable);                                   // ② 递归释放中间页表
}
```

两步顺序不能颠倒：先清除叶子（释放数据页），再递归释放中间页表。

---

## 六、缺页处理

### 6.1 `vmfault()` — 懒分配和 COW 缺页补页

```
调用: usertrap() → vmfault(p->pagetable, stval, read_or_write)
时机: 用户访问了"承诺过但未分配物理页"或"权限不足"的 VA
```

```c
uint64 vmfault(pagetable_t pagetable, uint64 va, int read) {
    struct proc *p = myproc();

    // ① 三道防线
    if(va >= MAXVA)     return 0;   // 超出用户空间
    if(va < 2*PGSIZE)   return 0;   // NULL 指针 guard
    if(va >= p->sz)     return 0;   // 超出 sbrk 承诺范围 → 杀进程

    va = PGROUNDDOWN(va);

    // ② 检查是否已有 PTE（可能 COW 场景）
    pte = walk(pagetable, va, 0);
    if(pte && (*pte & PTE_V)) {
        if(!(*pte & PTE_U)) return 0;           // 内核页 → 不可访问
        if(!read && !(*pte & PTE_W)) return 0;   // COW: 尝试写只读页
        return 1;                                 // 已有有效映射 → 让调用者重试
    }

    // ③ 懒分配：首次访问 → 现在才分配
    mem = kalloc();
    memset(mem, 0, PGSIZE);
    mappages(p->pagetable, va, PGSIZE, mem, PTE_W|PTE_U|PTE_R);
    return mem;
}
```

**三道防线保证安全性：**
```
sbrk(1GB) → p->sz = 1GB → 但没分配物理页

进程访问 0x5000（在 p->sz 内）→ vmfault → kalloc → 正常
进程访问 0x50000000（在 p->sz 内）→ vmfault → kalloc → 正常
进程访问 0x50000000000（超出 p->sz）→ vmfault → return 0 → usertrap kills process
进程访问 0x1000（guard 区）→ vmfault → return 0 → usertrap kills process
```

---

## 七、辅助函数

### 7.1 `ismapped()` — 检查 VA 是否已映射

```c
int ismapped(pagetable_t pagetable, uint64 va) {
    pte = walk(pagetable, va, 0);
    return (pte && (*pte & PTE_V));
}
```

用于调试或条件判断，不创建新映射。

---

## 八、完整调用关系图

```
                         main()
                           │
                    ┌──────┴──────┐
                    │             │
               kvminit()     (secondary harts)
                    │         等待 boot_done
               kvmmake()         │
                    │        kvminithart()
          ┌────────┼────────┐
          │        │        │
     UART/PLIC   TRAMPOLINE proc_mapstacks()
     映射         映射         │
     (kvmmap)   (kvmmap)   (kvmmap→mappages→walk)
                               ↓
                          alloc=1, 建立 128 个内核栈 PTE

          ┌──────────── walk() ────────────┐
          │           (核心原语)            │
    alloc=1 (建表)                  alloc=0 (查表)
          │                              │
    ┌─────┴─────┐              ┌────────┼────────┐
    │ mappages  │              │         │         │
    └─────┬─────┘          walkaddr  uvmunmap  uvmcopy
          │                   │         │         │
    ┌─────┼─────┐         copyin    uvmdealloc  freewalk
    │     │     │         copyout
  kvmmap uvmalloc vmfault copyinstr

  用户空间:
    uvmcreate → uvmalloc → (运行时缺页) → vmfault → mappages
    fork → uvmcopy → mappages
    exec → uvmfree → uvmcreate
    exit → uvmfree
```

---

## 九、架构差异汇总

| 方面 | RISC-V | LoongArch |
|------|--------|-----------|
| PT_LEVELS | 3 (Sv39) | 4 |
| PX level 范围 | 2,1,0 | 3,2,1,0 |
| MAXVA | 1L << (27+12-1) = 0x40亿 | 1ULL << 38 = 相同 |
| PA2PTE | `(pa>>12)<<10` | `pa & 0x0FFFFFFFFFFF000ULL` |
| PTE2PA | `(pte>>10)<<12` | `pte & 0x0FFFFFFFFFFF000ULL` |
| PTE_FLAGS 掩码 | `& 0x3FF` | `& 0xE0000000000001FFUL` |
| leaf 检测 | R\|W\|X 位 | MAT 位 |
| valen 有效位 | 39 (Sv39 固定) | 48 (VALEN=48, RVACFG=8→40) |
| 用户页表映射 | TRAMPOLINE + TRAPFRAME | 无（DMW0+KSave1 替代） |
| 内核代码映射 | KERNBASE 连续映射 | flash+RAM 分开映射 |
| VIRTIO | MMIO 直接映射 | PCI 不直接映射 |
| MMU 启动 | 写 satp | 写 PGDL+PGDH+DMW0+PWCL+PWCH+RVACFG+TLBRENTRY+CRMD |
