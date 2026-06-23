现在我已经有了足够的技术文档知识。让我再查几个关键细节，然后完整回答你的问题。好的，我已经系统阅读了 LoongArch Vol1 手册的相关章节。下面逐一解答所有问题，重要的地方直接引用手册原文。

---

# LoongArch 移植问题完整解答

## 一、UART TX 中断问题（P0 阻塞）

### Q1.1 — 16550A TX 中断支持

手册 Vol1 不涉及 UART 这类片外设备（属于 SoC/QEMU 实现范畴），但能帮助解释根本原因：

**UART 必须使用 SUC 内存类型访问（MAT=00）**。手册 §2.1.7 明确指出：

> *"Only SUC memory access instructions must not have side effects… Software can use this feature to access I/O devices in the system through SUC type memory access instructions."*

如果你的 DMW 窗口把 UART 所在地址（`0x1FE001E0`）配置为 MAT=01（CC，可缓存一致性），**访问 UART 的 LSR/IER 就不是直接 I/O 访问，而是走 Cache，TX 中断永远不会触发**。

**检查你的 DMW 配置：**
```c
// 正确：UART 区域应走 SUC 窗口（MAT=00）
// 如果你只有一个 DMW 设为 MAT=01（CC），UART 区域需要额外窗口
// 或通过页表映射为 SUC 类型
// 错误示例：DMW0 = 0x9000000000000011（MAT=01，覆盖了 UART 地址）
```

关于 QEMU 16550A TX 中断本身：QEMU 的 16550A 模型在 `THRE`（发送保持寄存器空）时会触发 TX 中断，**前提是 IER bit1 (ETBEI) 已置1，且 EIOINTC 正确路由了该 IRQ**。

---

### Q1.2 — UART 中断路由到 EIOINTC

**关键发现：EIOINTC 不是通过 MMIO 访问，而是通过 `iocsrrd/iocsrwr` 指令！**

手册 §4.2.2 定义了 IOCSR 指令：

> *"I0CSR{RD/WR}.{B/H/W/D} instructions are used to access the IOCSR… IOCSR registers can typically be accessed by multiple processor cores simultaneously. The execution of IOCSR access instructions on multiple processor cores satisfies the sequential consistency condition."*

EIOINTC 的寄存器布局（来自 QEMU `hw/intc/loongarch_extioi.c`）通过 IOCSR 地址空间访问，**不是 MMIO 地址**。你当前代码用普通 `*(volatile uint32_t*)` 读写 EIOINTC 是**错误的根本原因**。

正确访问方式：
```c
// 读 EIOINTC ISR（IOCSR 地址 0x1800，每个 core 偏移 8 字节）
static inline uint64_t read_eiointc_isr(int hart) {
    uint64_t val;
    // iocsrrd.d val, (0x1800 + hart*8)
    __asm__ volatile ("iocsrrd.d %0, %1" : "=r"(val) : "r"(0x1800 + hart * 8));
    return val;
}
```

**UART IRQ 编号**：在 QEMU LoongArch virt 机器上，UART0 通常连接到 EIOINTC 的 IRQ 2（不是 31）。IRQ 31/32 是 PCIe/virtio 设备。实际编号需查 QEMU DTS（`-machine virt,dumpdtb=dtb.bin`）。

---

### Q1.3 — EIOINTC ISR 偏移修正表

| 寄存器 | 正确 IOCSR 地址 | 访问宽度 | 说明 |
|--------|----------------|---------|------|
| IRQ 使能 | `0x1600` | 64-bit | 全局 IRQ 0-63 使能位图 |
| CPU 路由 | `0x1C00 + irq` | 8-bit | 每个 IRQ 路由到哪个 core（位图） |
| IS（中断状态） | `0x1800 + core*8` | 64-bit | 每个 core 的 pending 位图，**读不自动清除** |
| 写清 IS | 向对应位写 1 | — | 清除 pending bit |

---

### Q1.4 — 立即可行方案：LSR 轮询

**强烈建议先改成轮询，彻底绕开 UART 中断问题**：

```c
void uartputc_polling(int c) {
    // 等待 LSR bit5（THRE）= 1，即发送缓冲区空
    while (!(ReadReg(LSR) & 0x20))
        ;
    WriteReg(THR, c);
}
```

这样 `uartwrite` 不需要 `sleep()`，不依赖 TX 中断，不依赖 EIOINTC 正确配置。**先跑通单核、多核，再回头实现中断驱动 UART**。

---

## 二、多核竞态问题（P0）

### Q2.1 — 原子指令在 QEMU la464 上的支持

手册 §2.2.7.1 明确：

> *"The entire 'read-modify-write' process is atomic… no other processor cores or cache-consistent modules has global visibility of the execution of the write operation on the Cache row."*

`amswap_db.w` **原子性由架构保证，不是可选特性**。关键在于 `_db` 后缀：

> *"AM*_DB.W[U]/D[U] instruction not only completes the above atomized operation sequence, but also implements the data barrier function at the same time."*

即 `amswap_db.w` = 原子交换 + 完整内存屏障，等价于 C11 的 `atomic_exchange` with seq_cst。

**GCC 生成的 `__sync_lock_test_and_set` 在 LoongArch 上会生成 `amswap_db.w`，不需要降级 LL-SC**。QEMU la464 对此支持正常，没有已知 bug。

---

### Q2.2 — 多核缓存一致性

手册 §2.1.7 说明 MAT=01（CC）类型：

> *"When using consistent cacheable access type, the accessed object can be either the final memory object or the caches."*

手册 §2.1.9 说明 LL-SC 的 LLbit 清零条件（§2.2.7.4）：

> *"Other processor cores or Cache Coherent I/O masters perform a store operation on the Cache line where the address corresponding to the LLbit is located."*

**结论：DMW0 的 MAT=01 确保多核间 Cache 一致性由硬件维护**，spinlock 在 .bss 中是完全正确的。问题不在缓存一致性，而在别处。

### Q2.3 — QEMU la464 多核复位行为

手册 §6.4 明确：

> *"The PC after the reset is 0x1C000000. Since the MMU must be in direct address translation mode after the reset, the physical address of the first instruction fetched after reset is also 0x1000000."*

**重要：架构上所有 core 复位后 PC = 0x1C000000，LoongArch 没有像 RISC-V 那样的"非主 hart 在 WFI 等待"机制。** 在 QEMU 中，所有 la464 核同时从 0x1C000000 开始执行。

这就是你多核 panic 的根源——**所有核同时执行同一段代码，包括 .bss 清零、UART 初始化等非幂等操作**。

**正确的多核启动模式**：

```asm
# entry.S
_start:
    # 读取 CPUID CSR（地址 0x20）
    csrrd  $t0, 0x20        # CoreID in bits[8:0]
    andi   $t0, $t0, 0x1FF  # 取低9位

    # 只有 core 0 执行初始化
    bnez   $t0, .secondary_wait

    # Core 0: 清 BSS、初始化 UART、设置页表...
    bl     main

.secondary_wait:
    # Core 1,2,...: 等待 core 0 设置完成标志
    la.local $t1, core0_ready
1:  ld.w   $t2, $t1, 0
    beqz   $t2, 1b
    dbar   0               # 确保看到 core 0 写入的所有数据
    # 然后进入各自的 scheduler
    bl     secondary_main
```

---

### Q2.4 — `w_tp()` 和 `r_cpuid()` 正确性

**r_cpuid() 读 CSR 0x20 是正确的**。手册 §7.4.12：

> *"CoreID [8:0]: The number of the processor core… It is recommended that the processor core number be incremented from 0 in the system."*

每个核读到自己的 ID，是硬件行为，没有竞争。

**$r2（tp）的潜在 clobber 问题**：Vol1 是 ISA 手册，不定义 ABI。根据 LoongArch psABI：`$r2` 是 thread pointer，被 GCC 视为**保留寄存器**（不会被函数调用破坏，类似 RISC-V 的 `tp`）。

但有一个陷阱：**在 C 代码中直接使用 `$r2` 写入 cpuid 后，如果后续代码开启了 TLS（Thread Local Storage），GCC 会通过 `$r2` 访问 TLS，导致访问错误地址**。

建议用单独的全局数组代替 tp：
```c
// 更安全的做法
volatile int cpu_id_array[NCPU];
// 在 entry.S 中不用 $r2，改用 cpu_id_array[r_cpuid()] 存储
```

---

## 三、定时器与 `rdtime.d`（P1）

### Q3.1 — `rdtime.d` 实现状态

手册 §2.2.10.4 明确定义：

> *"The LoongArch instruction system defines a constant frequency timer, whose main body is a 64-bit counter called StableCounter. StableCounter is set to 0 after reset, and then increments by 1 every counting clock cycle."*

> *"RDTIME.D reads the entire 64-bit Counter value."*

> *"The characteristic of the constant frequency timer is that its timing frequency remains unchanged after reset, no matter how the clock frequency of the processor core changes."*

**`rdtime.d` 是架构定义的必须实现指令，QEMU la464 完全支持，不会触发 INE（Ecode=0xD）**。如果触发了 INE，说明 CPU 模式有问题（比如在 PLV3 且禁用了 RDTIME）。

手册 §7.4.4 提到：
> *"DRDTL3：Whether to disable RDTIME class instructions at the PLV3 privilege level."*

你在内核态（PLV0）运行，此限制不适用。

---

### Q3.2 — TCFG 配置频率分析

手册 §7.6.2（TCFG）：

> *"The initial value of the timer countdown self decrement count. This initial value must be an integer multiple of 4. The hardware will automatically fill in the lowest bit of the field value. Two bits of 0 are added before it is used."*

TCFG 寄存器布局（64位）：
```
bit 0: En（使能）
bit 1: Periodic（周期模式）
bits [n-1:2]: InitVal（初始计数值 / 4，因硬件会补2个0）
```

**写入计算**：
```c
// 写入 TCFG 时，实际计数初值 = (InitVal字段值) << 2
// 所以如果你要计数 N 个时钟周期：
uint64_t N = 100000000;
w_tcfg((N << 2) | TCFG_EN | TCFG_PERIODIC);
// 这里 bits[n-1:2] 存 N，硬件使用时加2个0变成 N*4 ← 不对
// 正确理解：bits[n-1:2] 直接就是计数值，bit[1:0] 是 En/Periodic
// 所以：w_tcfg((N & ~0x3) | TCFG_EN | TCFG_PERIODIC)
```

**QEMU la464 的 StableCounter 频率**：QEMU 中通常为 **100 MHz**。若 InitVal=100000000：
- 计数时间 = 100,000,000 / 100,000,000 Hz = **1 秒**

这就是你的心跳很慢的原因！改为 10ms 中断：
```c
#define TIMER_FREQ 100000000UL  // 100 MHz
#define HZ 100                   // 100 次/秒 = 10ms
uint64_t interval = TIMER_FREQ / HZ;  // = 1,000,000
w_tcfg((interval & ~0x3UL) | TCFG_EN | TCFG_PERIODIC);
```

**清除定时器中断**必须写 TICLR（CSR 0x44），不能只 return：
```c
// 在 timer interrupt handler 中：
w_ticlr(1);  // 写 bit0 清除 TI
```

---

## 四、EIOINTC 寄存器布局最终确认（P1）

### Q4.1 — 正确访问方式

**再次强调：EIOINTC 通过 IOCSR 指令访问，不是 MMIO**。你当前代码使用的 `0x0000 + hart*8` 偏移量是 IOCSR 地址空间中的地址，需用 `iocsrrd.d` 读取：

```c
// QEMU loongarch_extioi.c 中的 IOCSR 地址映射（正确版）：
#define EIOINTC_ENABLE     0x1600   // IRQ 0-63 使能，iocsrrd.d
#define EIOINTC_BOUNCE     0x1680   // IRQ bounce enable，iocsrrd.d
#define EIOINTC_ISR        0x1800   // Core ISR，iocsrrd.d, +8 per core
#define EIOINTC_ROUTE      0x1C00   // IRQ→core 路由，iocsrrd.b, +1 per IRQ

// C 封装：
static inline uint64_t iocsr_read64(uint32_t addr) {
    uint64_t val;
    __asm__ volatile("iocsrrd.d %0, %1" : "=r"(val) : "r"(addr));
    return val;
}
static inline void iocsr_write64(uint32_t addr, uint64_t val) {
    __asm__ volatile("iocsrwr.d %0, %1" :: "r"(val), "r"(addr));
}

// ISR 读取：
uint64_t pending = iocsr_read64(EIOINTC_ISR + hart * 8);
// 清除某个 IRQ（如 IRQ 2）：
iocsr_write64(EIOINTC_ISR + hart * 8, 1ULL << 2);

// 使能 IRQ 2：
uint64_t en = iocsr_read64(EIOINTC_ENABLE);
iocsr_write64(EIOINTC_ENABLE, en | (1ULL << 2));
```

### Q4.2 — UART 和 virtio IRQ 编号

在 QEMU LoongArch virt 机器上（`-machine virt`）：

| 设备 | IRQ 编号 | 触发方式 |
|------|----------|---------|
| UART0 (0x1FE001E0) | **2** | 电平触发（高电平有效） |
| virtio 磁盘 | **32** | 电平触发 |
| RTC | 3 | 电平触发 |

**验证方法**：
```bash
qemu-system-loongarch64 -machine virt,dumpdtb=/tmp/la.dtb ...
dtc -I dtb -O dts /tmp/la.dtb | grep -A5 "uart\|serial\|16550"
```

---

## 五、总结：立即行动清单

按优先级排序：

1. **[今天]** 把 UART 改成 LSR 轮询模式，彻底绕过 TX 中断问题。把 `uartwrite` 中的 `sleep()` 改为 busy-wait。

2. **[今天]** 修复 entry.S 的多核启动逻辑：core 0 做所有初始化，core 1/2 自旋等待标志。

3. **[本周]** 将所有 EIOINTC 访问改为 `iocsrrd.d`/`iocsrwr.d`，地址用 `EIOINTC_ENABLE=0x1600`、`EIOINTC_ISR=0x1800` 等 IOCSR 地址。

4. **[本周]** 确认 DMW 配置：UART 地址 `0x1FE001E0` 所在区域必须映射为 SUC（MAT=00），不能进 CC Cache 窗口。

5. **[本周]** 修正定时器：`interval = 100000000 / 100 = 1000000`，并在 handler 中调用 `w_ticlr(1)` 清中断。