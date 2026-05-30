# LoongArch xv6 HAL 移植问题汇总

> 时间：2026-05-29 ~ 2026-05-30 | 阶段：第4-6周
> 目标：将 xv6-riscv 移植到 LoongArch QEMU virt 机器，实现 HAL 接口

---

## 一、环境搭建阶段（第4周）

### 1.1 Docker HTTPS 证书缺失

**现象**：`apt-get update` 报 `Certificate verification failed`

**根因**：Ubuntu 24.04 Docker 基础镜像未预装 `ca-certificates` 包

**解决**：先用默认源安装 `ca-certificates`，再切换到清华镜像源

```dockerfile
RUN apt-get update && apt-get install -y ca-certificates
RUN sed -i 's|archive.ubuntu.com|mirrors.tuna.tsinghua.edu.cn|g' ...
```

---

### 1.2 交叉编译器包名版本化

**现象**：`E: Unable to locate package gcc-loongarch64-linux-gnu`

**根因**：Ubuntu 24.04 apt 仓库中无元包，实际包名为 `gcc-14-loongarch64-linux-gnu`，二进制名为 `loongarch64-linux-gnu-gcc-14`

**解决**：
```dockerfile
RUN apt-get install -y gcc-14-loongarch64-linux-gnu g++-14-loongarch64-linux-gnu ...
RUN ln -s loongarch64-linux-gnu-gcc-14 /usr/bin/loongarch64-linux-gnu-gcc
```

---

### 1.3 QEMU efi-virtio.rom 缺失

**现象**：`qemu-system-loongarch64: failed to find romfile "efi-virtio.rom"`

**根因**：LoongArch QEMU virt 机器默认配置了 virtio-net-pci 设备，其 PXE ROM 在 Ubuntu 24.04 的 qemu-system-data 包中不存在

**解决**：`-nic none` 禁用网络设备

---

### 1.4 QEMU -bios 只接受 raw binary

**现象**：传 ELF 文件时 QEMU 把 ELF 头 `\x7fELF...` 当指令执行

**根因**：`-bios` 是原始二进制加载，不是 ELF loader。RISC-V 的 `-kernel` 自动解析 ELF，LoongArch 无此功能。

**解决**：`loongarch64-linux-gnu-objcopy -O binary kernel/kernel kernel/kernel.bin`

---

### 1.5 QEMU LoongArch virt 要求至少 1GB RAM

**现象**：`ram_size must be greater than 1G`

**解决**：`-m 1G`（RISC-V 只需 128MB）

---

### 1.6 Makefile TOOLPREFIX/QEMU 变量覆盖顺序 bug

**现象**：`make ARCH=loongarch` 报 `Error: Couldn't find a riscv64 version of GCC`

**根因**：LoongArch 的 TOOLPREFIX/QEMU 设置位于 `ifndef TOOLPREFIX` 和 `QEMU = riscv64` 之后，被覆盖

**解决**：将 `ifeq ($(ARCH),loongarch)` 块移到 RISC-V 默认值之前

---

### 1.7 -mcmodel 差异

**根因**：RISC-V 使用 `-mcmodel=medany`，LoongArch 裸机程序需要 `-mcmodel=normal`

**解决**：条件编译：
```makefile
ifeq ($(ARCH),loongarch)
CFLAGS += -march=loongarch64 -mcmodel=normal
else
CFLAGS += -march=rv64gc -mcmodel=medany
endif
```

---

## 二、HAL 基础实现阶段（第5周）

### 2.1 PA2PTE/PTE2PA 宏类型错误

**现象**：`error: invalid operands to binary & (have 'uint64 *' and 'long long unsigned int')`

**根因**：`vm.c` 调用 `PA2PTE(pagetable)` 时 `pagetable` 是 `uint64 *`（指针类型），C 语言不允许对指针进行位运算

**解决**：在宏中添加 `(uint64)` 显式转换，拆分辅助宏：
```c
#define P2LOW(pa)  ((uint64)(pa) & 0xFFFFFFFFFF000ULL)
#define P2HIGH(pa) ((((uint64)(pa) >> 60) & 0x1F) << 7)
#define PA2PTE(pa)  (P2LOW(pa) | P2HIGH(pa))
#define PTE2PA(pte) (P2LOW(pte) | ((((uint64)(pte) >> 7) & 0x1F) << 60))
```

### 2.2 内核直接调用 RISC-V CSR 函数

**现象**：编译报 `implicit declaration` 错误：`w_satp`, `w_stvec`, `r_sstatus`, `wfi`, `SSTATUS_SPP`, `SSTATUS_SPIE`

**根因**：内核代码（`vm.c`, `trap.c`, `proc.c`）并非 100% 使用 `hal_` 前缀接口，仍直接调用 RISC-V CSR 函数

**影响文件**：
| 调用点 | RISC-V 原名 | 所在文件 |
|--------|------------|---------|
| 页表切换 | `w_satp(pgdl)` | vm.c:78 |
| TLB刷新 | `sfence_vma()` | vm.c:76,81 |
| 状态读 | `r_sstatus()` | trap.c:41,123,139 |
| 模式检查 | `SSTATUS_SPP` | trap.c:41,124,142 |
| 中断检查 | `SSTATUS_SPIE` | trap.c:125 |
| 向量设置 | `w_stvec(e)` | trap.c:28,46 |
| 停机等待 | `wfi` | proc.c:459 |

**解决**：在 `hal/loongarch/arch.h` 中添加 RISC-V 兼容层：
```c
#define w_satp(x)    w_pgdl(x)
#define r_satp()     r_pgdl()
#define w_stvec(x)   w_eentry(x)
#define r_stvec()    r_eentry()
#define sfence_vma() asm volatile("invtlb 0, $r0, $r0")
```
同时新增 `hal_cpu_idle()` 函数替代 `wfi`

---

### 2.3 SSTATUS_SPP/SPIE → PRMD 转换

**根因**：RISC-V 使用 `sstatus.SPP/SPIE` 位判断 trap 来源，LoongArch 使用 `PRMD.PPLV/PIE`

**Week 5 临时方案**：`#define SSTATUS_SPP 0`（无用户态时不检查）

**Week 6 正式方案**：转换函数映射 PRMD → RISC-V sstatus 位：
```c
static inline uint64 r_sstatus_compat(void) {
  uint64 prmd = r_prmd();
  uint64 sstatus = 0;
  if ((prmd & 0x3) != 3)  sstatus |= (1L << 8);  // SPP
  if (prmd & PRMD_PIE)     sstatus |= (1L << 5);  // SPIE
  return sstatus;
}
```

---

### 2.4 PTE PPN 分两段存储

**关键差异**：LoongArch 的物理页号在 PTE 中分成两段：
- `PPN[47:0]` → `PTE[59:12]`
- `PPN[52:48]` → `PTE[11:7]`

RISC-V 是连续存储：`PPN[53:10]` → `PTE[53:10]`

---

### 2.5 Callee-saved 寄存器数量不同

| 平台 | 寄存器 | 数量 |
|------|--------|------|
| RISC-V | ra, sp, s0-s11 | 14 |
| LoongArch | ra, sp, fp, s0-s8 | 12 |

**解决**：`hal/hal_ctx.h` 使用 `#ifdef ARCH_loongarch` 条件编译不同的结构体定义

---

### 2.6 软件 TLB 重填

**最大差异**：RISC-V MMU 在 TLB miss 时硬件自动 walk 页表（Sv39），LoongArch MMU 触发 TLB 重填异常，由软件完成 walk 和填入。

**涉及**：
- 入口 CSR：`TLBRENTRY`（0x88），独立于通用异常入口 `EENTRY`（0xC）
- 填入指令：`tlbwr`（随机写）、`tlbfill`（指定写）
- 辅助 CSR：`TLBEHI`(0x11)、`TLBELO0`(0x12)、`TLBELO1`(0x13)、`TLBIDX`(0x10)

**当前策略**：推迟到内核页表初始化完成后再实现。内核空间通过 DMW（直接映射窗口）避免 TLB miss。

---

## 三、中断/上下文切换阶段（第6周）— 重大调试发现

### 3.1 UART 波特率重编程破坏输出 ⭐

**现象**：`uartinit()` 中 `WriteReg(0, 0x03)` 改变波特率除数后，后续 UART 输出变为乱码

**根因**：QEMU LoongArch virt 的 UART 工作在默认波特率，改变除数寄存器导致实际传输波特率变化，字符损坏

**解决**：`uartinit()` 跳过波特率编程，只执行 `initlock`

---

### 3.2 UART FCR FIFO 重置触发 QEMU 内部断言崩溃 ⭐

**现象**：`WriteReg(FCR, FCR_FIFO_ENABLE | FCR_FIFO_CLEAR)` 后 QEMU 崩溃：
```
ERROR:system/cpus.c:504:qemu_mutex_lock_iothread_impl: assertion failed
```

**根因**：QEMU 8.2 LoongArch 的 16550a 模拟在 FIFO 重置时存在线程竞态条件 bug

**解决**：`uartinit()` 不操作 FCR 寄存器

---

### 3.3 UART TX idle 轮询不可靠 ⭐

**现象**：`while((ReadReg(LSR) & LSR_TX_IDLE) == 0)` 在 QEMU LoongArch 上永不满足

**根因**：QEMU LoongArch 的 16550a LSR 寄存器在未初始化时行为与标准 16550a 不同

**临时方案**：直接写入 THR 不等待 TX idle

---

### 3.4 定时器中断风暴（Timer Storm）⭐

**现象**：调用 `timerinit()` 后 CPU 陷入无限中断循环

**根因**：
1. `timerinit()` 配置 TVAL 定时器并启用中断
2. `kernelvec` 桩只含 `ertn` 一条指令
3. `ertn` 返回后，中断标志未清除（未写 `TICLR`）
4. CPU 立即再次响应同一中断 → 无限循环

**解决**：
- 将 `timerinit()` 调用从 `start()` 移到 `main()` 末尾（所有初始化完成后）
- 在 `clockintr()` 中添加 `w_ticlr(0)` 清除中断标志
- 使用足够的 TVAL 初始值（100M cycles ≈ 1秒）

---

### 3.5 `kerneltrap: not from supervisor mode` panic ⭐⭐⭐⭐⭐

**这是整个移植过程中最关键的 bug，调试时间最长。**

**现象**：
```
ppppppppppppppp...  (panic 循环，只输出 'p')
```

**GDB 捕获的 panic 信息**：
```
panic (s="kerneltrap: not from supervisor mode") at kernel/printf.c:137
```

**逐步排查过程**：

1. **直接 UART 写入标记法**：在 main() 的每个 init 函数前后插入 `*UART0 = 'A'/'B'/...` 逐步定位
   - 发现：`'M'` 标记（main() 开头）可输出，consoleinit() 后无输出

2. **`for(;;)` 暂停测试**：在 consoleinit() 后加死循环
   - 结果：内核稳定运行，无 panic → consoleinit() 本身不 crash

3. **移除 printf() 的 push_off/pop_off**：怀疑 spinlock/atomic 操作触发异常
   - 结果：无效，panic 依然发生

4. **使用 uart_puts 替代 printf**：绕过 printf/locking 路径
   - 关键发现：用 `uart_puts`（直接 UART 写）BEFORE consoleinit() 时运行正常
   - 但 AFTER consoleinit() 时 panic

5. **GDB 远程调试**：最终捕获到 panic 消息
   - **确切 panic：`kerneltrap: not from supervisor mode`**
   - **触发点：`trap.c:142` — `if((sstatus & SSTATUS_SPP) == 0) panic(...)`**

6. **根因分析**：为什么 kerneltrap 会被调用？没有开启定时器，没有用户进程。
   - `consoleinit()` → `uartinit()` → **不操作任何硬件寄存器**（Week 6 版本只有 `initlock`）
   - `initlock(&cons.lock, "cons")` → 写入 `cons.lock.locked = 0`
   - `cons.lock` 在**`.bss` 段**
   - `.bss` 段位于内核链接地址内（0x1c000000+）
   - QEMU `-bios` 将内核加载到 **flash 区域（0x1c000000-0x1d000000，只读）**
   - **写入只读 flash → 存储访问异常 → EENTRY → kernelvec → kerneltrap → panic！**

**最终根因：`.bss` 和 `.data` 被链接在只读 flash 区域内，写操作触发存储异常。**

---

### 3.6 修复 .bss/.data 布局 — flash/ROM 隔离 ⭐⭐⭐⭐⭐

**根本问题**：xv6 内核需要将全局变量（`.bss`, `.data`）放在可写内存中，但 `-bios` 加载方式把整个二进制放在只读 flash。

**最终方案**：分离链接布局
```
+------------------+  0x1c000000  Flash（只读）
| .text            |    内核代码，从 flash 执行
| .rodata          |    只读数据
| .eh_frame        |    异常处理帧
+------------------+  _data_lma     .data 初始值在 flash 中的位置
        |
        |  start() 复制 .data 初始值, 清零 .bss
        v
+------------------+  0x00400000  RAM（可读写）
| .data            |    已初始化的全局变量（spinlock, 计数器等）
| .bss             |    未初始化的全局变量
+------------------+  end
```

**链接脚本关键部分**：
```ld
.data : AT(_data_lma) {  /* VMA=0x00400000, LMA=flash */
    _data_start = .;
    *(.data .data.*) *(.got .got.*)
    _data_end = .;
}
.bss (NOLOAD) : {
    _bss_start = .;
    *(.bss .bss.*)
    _bss_end = .;
}
```

**start() 中的初始化**：
```c
// 将 .data 初始值从 flash 复制到 RAM
uint64 *src = (uint64*)_data_lma;
uint64 *dst = (uint64*)_data_start;
while(dst < (uint64*)_data_end) *dst++ = *src++;
// 清零 .bss
dst = (uint64*)_bss_start;
while(dst < (uint64*)_bss_end) *dst++ = 0;
```

**修复结果**：
```
xv6 kernel is booting       ← printf 正常工作！
panic: mappages: va not aligned
```

printf/locking/spinlock 路径完全修复。下一步需解决 PHYSTOP 地址范围。

---

### 3.7 PHYSTOP 与 mappages 地址未对齐 ⚠️

**现象**：`panic: mappages: va not aligned`

**根因**：`etext`（0x1c009000，flash）和 `PHYSTOP`（0x00400000+RAM，RAM）之间的 `kvmmap` 范围计算为负数：
```
PHYSTOP - etext = 0x07C00000 - 0x1c009000 = 负值 → 无符号下溢为巨大值 → 映射越界
```

**当前状态**：待修复。方案为将 PHYSTOP 设为大于 flash+RAM 的覆盖值（如 `0x1d000000`）。

---

## 四、开发工具链问题

### 4.1 GDB 架构名大小写

- GDB 中正确的架构名是 `Loongarch64`（大写 L），不是 `loongarch64`

### 4.2 QEMU -d in_asm 跟踪

- `-d in_asm -D /tmp/qemu.log` 有效但产生大量输出
- 配合 `-d cpu,int` 可跟踪 CSR 操作和中断

### 4.3 Docker 交互式调试

```bash
# 终端 1
xv6-la
cd /xv6
make ARCH=loongarch
loongarch64-linux-gnu-objcopy -O binary kernel/kernel kernel/kernel.bin
qemu-system-loongarch64 -M virt -cpu la464 -m 1G -smp 1 -nographic -nic none \
  -bios kernel/kernel.bin -S -gdb tcp::1234

# 终端 2
xv6-la
cd /xv6
gdb-multiarch kernel/kernel
(gdb) set architecture Loongarch64
(gdb) target remote :1234
(gdb) break panic
(gdb) continue
```

---

## 五、关键数据

### 内存布局（最终版本）

```
0x00000000 ───────────────── RAM 起始
           │   (1GB, QEMU -m 1G)
0x00400000 ───────────────── .data / .bss VMA
           │   内核可写数据段
0x0040XXXX ───────────────── end（内核结束）
           │
0x0FE00000 ───────────────── EIOINTC MMIO
0x10000000 ───────────────── PCIe MMIO 区域
0x1C000000 ───────────────── Flash 起始（-bios 加载点）
           │   .text / .rodata / .eh_frame
0x1C00CXXX ───────────────── _data_lma（.data 初始值在 flash 中的位置）
0x1FE001E0 ───────────────── UART0 基址
```

### 已验证可用的 QEMU 命令

```bash
qemu-system-loongarch64 \
  -M virt -cpu la464 \
  -m 1G -smp 1 \
  -display none -serial stdio \
  -nic none \
  -bios kernel/kernel.bin
```

### 编译命令

```bash
# 宿主机 make（带 qemu 启动）
make ARCH=loongarch qemu

# 手动步骤
make ARCH=loongarch
loongarch64-linux-gnu-objcopy -O binary kernel/kernel kernel/kernel.bin
qemu-system-loongarch64 ... -bios kernel/kernel.bin
```

---

## 六、当前状态与待办

### 已完成 ✅

- [x] Docker + loongarch64 交叉编译工具链
- [x] QEMU LoongArch virt 启动
- [x] HAL 6 个核心文件（arch.h, memlayout.h, hal_entry.S, hal_start.c, hal_uart.c, kernel.ld）
- [x] 12 寄存器上下文切换（hal_swtch.S）
- [x] 内核中断向量完整实现（hal_kvec.S）
- [x] 用户态陷入/返回（hal_tramp.S）
- [x] EIOINTC 基础框架（hal_intr.c）
- [x] PRMD→RISC-V sstatus 转换
- [x] .bss/.data flash→RAM 重定位
- [x] printf 输出路径修复
- [x] `make ARCH=loongarch` 编译通过
- [x] "xv6 kernel is booting" 成功输出

### 待完成 ⬜

- [ ] **PHYSTOP 地址范围修复**（mappages panic）
- [ ] 定时器中断实际验证（timerinit 已实现但未端到端测试）
- [ ] UART 中断驱动输入
- [ ] 用户进程创建和执行
- [ ] 完整 usertests 通过
- [ ] PCI virtio 磁盘驱动
- [ ] TLB 重填 handler 实现
- [ ] -mcmodel=normal 下大地址访问验证

---

*最后更新：2026-05-30 | Week 5-6 过渡期*
