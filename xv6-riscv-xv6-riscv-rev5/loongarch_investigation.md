Based on my extensive search through the LoongArch reference manual, here are detailed answers to all your questions.

---

# LoongArch 技术调查问题解答

---

## 一、TLB Refill 异常与 ertn 返回机制 [P0]

### Q1.1 — TLB refill 返回地址寄存器

**中文：**
返回地址**仅保存在 `CSR.TLBRERA`（CSR 0x8A）**，不保存在普通的 `CSR.ERA`（CSR 0x6）。手册第 6.3.4 节明确：硬件将触发例外指令的 PC 的 `[GRLEN-1:2]` 位记录到 `CSR.TLBRERA` 的 ERA 域，同时将 `CSR.TLBRERA.IsTLBR` 位置 1。普通的 `CSR.ERA` 在 TLB refill 时**不更新**。

**English:**
The return address is saved **only in `CSR.TLBRERA` (CSR 0x8A)**, not in the normal `CSR.ERA` (CSR 0x6). The manual (§6.3.4) states that hardware records the faulting PC's `[GRLEN-1:2]` bits into the ERA field of `CSR.TLBRERA` and sets `TLBRERA.IsTLBR=1`. The regular `CSR.ERA` is **not updated** during TLB refill.

---

### Q1.2 — ertn 在 TLB refill 上下文中的行为

**中文：**
`ertn` 指令的行为**由 `CSR.TLBRERA.IsTLBR` 位决定**（此即"处于 TLB 重填上下文"的判断依据）：
- 当 `IsTLBR=1` 时：PPLV/PIE/PWE 从 **`CSR.TLBRPRMD`** 恢复；返回地址取自 **`CSR.TLBRERA`**；且硬件额外将 `CRMD.DA` 清 0、`CRMD.PG` 置 1（恢复页表翻译模式）；最后将 `IsTLBR` 清 0。
- 当 `IsTLBR=0` 时：PPLV/PIE/PWE 来自 `CSR.PRMD`，返回地址来自 `CSR.ERA`。

CPU 不依赖 CRMD/ESTAT 的状态位来区分，**唯一判断依据是 `TLBRERA.IsTLBR`**。

**English:**
`ertn` behavior is **determined solely by `CSR.TLBRERA.IsTLBR`** (this is the CPU's only mechanism to identify "TLB refill context"):
- When `IsTLBR=1`: PPLV/PIE/PWE are restored from **`CSR.TLBRPRMD`**; the return PC comes from **`CSR.TLBRERA`**; hardware also clears `CRMD.DA` and sets `CRMD.PG=1` (restoring page-table translation mode); then `IsTLBR` is cleared to 0.
- When `IsTLBR=0`: PPLV/PIE/PWE come from `CSR.PRMD`, return PC from `CSR.ERA`.

The CPU does **not** use any CRMD/ESTAT bit for this distinction — **only `TLBRERA.IsTLBR`** matters.

---

### Q1.3 — TLB refill → TLBI 链中的 ERA 行为

**中文：**
这是当前 bug 的根源。当 TLB refill handler 填入了 V=0 的条目（或者根本没有正确写入）并执行 `ertn` 后，CPU 重试原指令又触发了**普通的 TLB Invalid 例外（TLBI/PIF，Ecode=0x3）**。此时 CPU 按照**普通例外流程**，将 faulting PC 写入 **`CSR.ERA`**（而非 TLBRERA）。

但问题是：如果你在 TLB refill handler 中意外覆盖了 `CSR.ERA`（例如通过 `csrwr` 调用了某个会修改 ERA 的路径），则 `usertrap()` 读到的 `ERA=0`。另一种原因：handler 在 IsTLBR=1 的状态下调用了普通例外路径的代码，此时 ertn 错误地用了 TLBRERA 的值。

**诊断关键**：ERA=0 意味着在 TLBI 触发时 faulting PC 是 0，这说明 TLB refill 的 ertn 根本没有正确返回用户态 PC（可能 handler 在取指失败，CPU 发现 PC 为 0 的情况下触发的 TLBI）。

**English:**
This is the root cause of the current bug. After the TLB refill handler fills an entry with V=0 (or fails to write correctly) and executes `ertn`, the CPU retries the faulting instruction, which triggers a **normal TLB Invalid exception (PIF, Ecode=0x3)**. In this normal exception path, the CPU writes the faulting PC to **`CSR.ERA`** (not TLBRERA).

ERA=0 in `usertrap()` means the faulting PC at the time of the TLBI exception was 0 — which implies `ertn` did not properly return to user-space, or the handler itself faulted (e.g., the TLB refill handler itself ran at PC=0 or the handler's `ertn` jumped to address 0 due to a corrupt `TLBRERA`).

---

### Q1.4 — TLBEHI/TLBREHI 在 TLB refill 时的自动加载

**中文：**（手册 §6.3.4 和 §7.5.16）

| 域 | 来源 |
|---|---|
| `TLBREHI.VPPN`（bits [VALEN-1:13]） | **硬件自动**将触发例外的虚地址的 `[VALEN-1:13]` 位写入 |
| `TLBREHI.PS`（bits [5:0]） | **软件负责设置**，复位值不确定；TLB refill 时硬件**不自动填充** PS |
| ASID | 来自 `CSR.ASID.ASID`，由 `tlbwr`/`tlbfill` 指令执行时自动使用 |

**重要**：`TLBREHI.PS` 需要软件在 TLB refill handler 中显式写入正确的页大小（如 4KB 对应 PS=12）。如果 PS 值错误，`tlbfill` 会创建错误页大小的 TLB 条目，导致地址翻译错误。

注意：TLB refill 上下文中（IsTLBR=1），`tlbwr`/`tlbfill` 使用的是 `TLBREHI`（不是 `TLBEHI`）和 `TLBRELO0/1`（不是 `TLBELO0/1`）。

**English:**
Per manual §6.3.4 and §7.5.16:

| Field | Source |
|---|---|
| `TLBREHI.VPPN` (bits [VALEN-1:13]) | **Hardware automatically** loads it from the faulting VA's [VALEN-1:13] bits |
| `TLBREHI.PS` (bits [5:0]) | **Software must set explicitly**; hardware does NOT auto-fill PS on TLB refill |
| ASID | Taken from `CSR.ASID.ASID` automatically by `tlbwr`/`tlbfill` |

**Critical**: `TLBREHI.PS` must be explicitly written in the refill handler (e.g., PS=12 for 4KB pages). Wrong PS creates incorrect-sized TLB entries. When IsTLBR=1, `tlbwr`/`tlbfill` use `TLBREHI`/`TLBRELO0/1`, not the non-refill registers.

---

### Q1.5 — tlbwr 指令的精确行为

**中文：**（手册 §4.2.4.3）
- `tlbwr` 按照 `CSR.TLBIDX.Index` 域所指定的位置写入 TLB，**不是随机选择**（随机选择是 `tlbfill` 的行为）。
- 当 `IsTLBR=1` 时，`tlbwr` **总是写入有效项**（TLB 项的 E 位强制为 1），忽略 `TLBIDX.NE`。
- 写入的信息来自 `CSR.TLBREHI`、`CSR.TLBRELO0`、`CSR.TLBRELO1`（IsTLBR=1 时）。
- 没有独立的 ITLB/DTLB 区分，`tlbwr` 写入统一的 TLB（由 STLB/MTLB 结构决定具体位置）。
- 若写入目标为 STLB 但 Index 与 VPPN/PS 矛盾，**行为未定义**。
- **无锁定 TLB 机制**（LoongArch 架构中 `tlbwr` 不会跳过所谓"wired"条目，它写固定位置）。

**English:**
Per manual §4.2.4.3:
- `tlbwr` writes to the TLB position specified by `CSR.TLBIDX.Index` — **not random** (random placement is `tlbfill`'s behavior).
- When `IsTLBR=1`, `tlbwr` **always writes a valid entry** (TLB E bit forced to 1, `TLBIDX.NE` is ignored).
- Data comes from `CSR.TLBREHI`, `CSR.TLBRELO0`, `CSR.TLBRELO1` when IsTLBR=1.
- There is no separate ITLB/DTLB distinction at this instruction level; `tlbwr` writes to the unified TLB structure.
- If writing to STLB but the Index contradicts VPPN/PS, **behavior is undefined**.
- **No wired/locked entries** concern — `tlbwr` always writes to the explicitly specified index.

---

### Q1.6 — TLBRENTRY (CSR 0x88) 的地址格式

**中文：**（手册 §7.5.11）

| 位域 | 含义 |
|---|---|
| `[11:0]` | 只读恒为 0（自动对齐到 4KB，即需要 4KB 页对齐）|
| `[PALEN-1:12]` | TLB refill 入口物理地址的高位 |
| `[63:PALEN]` | 只读恒为 0 |

- 地址**必须是物理地址**（因为触发 TLB refill 后处理器进入直接地址翻译模式 DA=1, PG=0）。
- bit 0 **没有**特殊的 "direct/vectored" 含义，低 12 位全部恒为 0（强制 4KB 对齐）。
- **无向量化模式**：LoongArch TLB refill 只有一个固定入口，无偏移机制。

**English:**
Per manual §7.5.11:

| Bits | Meaning |
|---|---|
| `[11:0]` | Read-only, always 0 (forcing 4KB page alignment) |
| `[PALEN-1:12]` | High bits of the physical address for TLB refill entry |
| `[63:PALEN]` | Read-only, always 0 |

- The address **must be a physical address** (CPU enters DA=1, PG=0 mode on TLB refill).
- Bit 0 has **no** special "direct/vectored" meaning — the entire lower 12 bits are fixed at zero.
- **No vectored mode**: there is only one fixed entry point for TLB refill, no offset mechanism.

---

## 二、DMW 寄存器格式 [P1]

### Q2.1 — LA64 DMWn 的精确位域定义

**中文：**（手册 §7.5.18，表 7-42）

**LA64 架构：**

| 位 | 名称 | 描述 |
|---|---|---|
| 0 | PLV0 | =1 表示 PLV0 可使用此窗口 |
| 1 | PLV1 | =1 表示 PLV1 可使用此窗口 |
| 2 | PLV2 | =1 表示 PLV2 可使用此窗口 |
| 3 | PLV3 | =1 表示 PLV3 可使用此窗口 |
| 5:4 | MAT | 存储访问类型（0=SUC, 1=CC, 2=WUC） |
| 59:6 | 0（保留）| 读返回 0 |
| 63:60 | VSEG | 匹配虚地址的 **[63:60]** 位 |

**LA64 无 PSEG**（PSEG 是 LA32 特有的）。物理地址直接等于虚地址的 `[PALEN-1:0]` 位（即 VA 的高位直接截断）。

**English:**
Per manual §7.5.18, Table 7-42:

**LA64 Architecture:**

| Bits | Name | Description |
|---|---|---|
| 0 | PLV0 | =1 enables this window at PLV0 |
| 1 | PLV1 | =1 enables this window at PLV1 |
| 2 | PLV2 | =1 enables this window at PLV2 |
| 3 | PLV3 | =1 enables this window at PLV3 |
| 5:4 | MAT | Memory Access Type (0=SUC, 1=CC, 2=WUC) |
| 59:6 | 0 (reserved) | Returns 0 on read |
| 63:60 | VSEG | Matches VA bits **[63:60]** |

**LA64 has NO PSEG field** (PSEG is LA32-only). PA = VA[PALEN-1:0] (upper bits truncated).

---

### Q2.2 — DMW 匹配优先级的精确规则

**中文：**（手册 §5.2.1）
- **DMW 优先于 TLB**：地址翻译时先检查直接映射窗口，若命中则使用 DMW 结果，不再查询 TLB。
- DMW0~DMW3 之间**无明确优先级定义**（手册未说明），实践中应避免多个 DMW 同时命中同一地址。
- 匹配方式是**精确等于**：VA[63:60] 必须与 `DMWn.VSEG` 完全相等，且当前 PLV 对应的 PLVn 位为 1。

**English:**
Per §5.2.1:
- **DMW takes priority over TLB**: address translation checks DMWs first; if a window matches, the TLB is bypassed entirely.
- Priority among DMW0~DMW3 is **not explicitly defined** in the manual — avoid overlapping configurations.
- Match is **exact equality**: VA[63:60] must exactly equal `DMWn.VSEG`, and the current PLV's corresponding bit must be set.

---

### Q2.3 — DMW 与 PLV 的关系

**中文：**
- PLV 掩码：bit 0 = PLV0, bit 1 = PLV1, bit 2 = PLV2, bit 3 = PLV3（即 `0x1` 表示仅 PLV0 可用，`0x9` 表示 PLV0 和 PLV3 可用）。
- DMW 在 TLB 查询**之前**检查（先 DMW，无命中才走 TLB）。
- 例：`DMW0 = 0x9000000000000011`：VSEG=0x9, MAT=1(CC), PLV0=1, PLV3=1。

**English:**
- PLV mask: bit 0=PLV0, bit 1=PLV1, bit 2=PLV2, bit 3=PLV3. Example: `0x1` = PLV0 only, `0x9` = PLV0 and PLV3.
- DMW is checked **before TLB** in the address translation pipeline.
- Example: `DMW0 = 0x9000000000000011` → VSEG=0x9, MAT=1(CC), PLV0=1, PLV3=1.

---

## 三、CSR 指令编码与内联汇编约束 [P1]

### Q3.1 — csrwr 指令的精确语义

**中文：**（手册 §4.2.1.1）
```
csrwr rd, csr_num
```
完整操作为：
1. 读出旧 CSR 值 → 写入 `rd`（**`rd` 被修改为旧值**）
2. `rd` 的原始值 → 写入 CSR

即：**`rd` 既是源操作数也是目的操作数**（类似 x86 的 xchg）。这是一个读-改-写的原子操作。

**English:**
Per §4.2.1.1:
```
csrwr rd, csr_num
```
Full operation:
1. Read old CSR value → write into `rd` (**`rd` is overwritten with the old CSR value**)
2. Original value of `rd` → written into the CSR

So **`rd` is both source and destination** (similar to x86 `xchg`). This is an atomic read-modify-write.

---

### Q3.2 — 内联汇编约束的正确写法

**中文：**
当前 `: "r"(x)` 写法**已经是 bug**。正确写法应为 `"+r"(x)`，因为 `csrwr` 会修改 `rd` 寄存器（写入旧 CSR 值）。GCC 不知道寄存器被修改，可能导致：
- 编译器认为 `x` 仍保持原值而使用
- 寄存器分配错误

**正确写法示例：**
```c
static inline void w_crmd(uint64_t x) {
    __asm__ volatile("csrwr %0, 0x0" : "+r"(x));
    // 此后 x 里是 CRMD 的旧值，通常忽略
}
```
或用临时变量：
```c
static inline void w_crmd(uint64_t x) {
    uint64_t tmp = x;
    __asm__ volatile("csrwr %0, 0x0" : "+r"(tmp));
}
```

**English:**
The current `: "r"(x)` is **already a bug**. The correct form is `"+r"(x)` since `csrwr` overwrites `rd` with the old CSR value. Without the `+` constraint, GCC may:
- Reuse the same register assuming its value is unchanged
- Cause incorrect register allocation

**Correct example:**
```c
static inline void w_crmd(uint64_t x) {
    __asm__ volatile("csrwr %0, 0x0" : "+r"(x));
    // x now holds the old CRMD value; typically discarded
}
```

---

## 四、页表与 TLB 条目格式 [P1]

### Q4.1 — TLBELO0/TLBELO1 的格式（手册表 7-23，LA64）

**中文：**

| 位 | 名称 | 描述 |
|---|---|---|
| 0 | V | 有效位 |
| 1 | D | 脏位 |
| 3:2 | PLV | 特权等级 |
| 5:4 | MAT | 存储访问类型 |
| 6 | G | 全局标志 |
| **11:7** | **0（保留）** | **只读恒为 0** |
| **PALEN-1:12** | **PPN** | **物理页号** |
| 60:PALEN | 0 | 只读恒为 0 |
| 61 | NR | 不可读位 |
| 62 | NX | 不可执行位 |
| 63 | RPLV | 受限特权等级使能 |

**关键区别**：TLBELO 格式与页表 PTE 格式**不完全相同**。PTE 格式中 PPN 字段是 `PA[PALEN-1:12]`，而 TLBELO 中 PPN 位置一致，但 PTE 文档中描述的 bits [11:7] 区域在 TLBELO 中**是保留位（恒为 0）**，不存在 `PA[52:48]` 的分割存储。请直接使用 `PA >> 12` 作为 PPN 即可。

**English:**
Per Table 7-23 (LA64):

| Bits | Name | Description |
|---|---|---|
| 0 | V | Valid bit |
| 1 | D | Dirty bit |
| 3:2 | PLV | Privilege level |
| 5:4 | MAT | Memory access type |
| 6 | G | Global flag |
| **11:7** | **0 (reserved)** | **Read-only, always 0** |
| **PALEN-1:12** | **PPN** | **Physical page number** |
| 60:PALEN | 0 | Reserved |
| 61 | NR | Not-readable bit |
| 62 | NX | Not-executable bit |
| 63 | RPLV | Restricted PLV enable |

**Key**: Bits [11:7] in TLBELO are **reserved/always-zero** — there is no split PPN encoding. Simply use `PA >> 12` for PPN.

---

### Q4.2 — 是否需要同时填充 TLBELO0 和 TLBELO1

**中文：**
**必须同时设置两者**。TLB 采用双页结构（偶数页在 TLBELO0，奇数页在 TLBELO1）。对于普通 4KB 页：
- 填写目标页对应的那个寄存器（根据 VA bit[12] 决定是偶还是奇）
- **另一个也必须写**，通常写 V=0（无效项）避免残留
- 如果 TLBELO1 有残留有效条目，可能导致相邻虚页命中错误的物理地址

**English:**
**Both must be set.** TLB uses a dual-page structure (even page in TLBELO0, odd in TLBELO1). For a 4KB page:
- Write the matching register (VA bit[12] determines even/odd)
- **The other register must also be written**, typically with V=0 to invalidate it
- Stale valid entries in the unused register can cause wrong physical address hits for adjacent virtual pages

---

### Q4.3 — 非叶子 PTE（目录项）的格式要求

**中文：**（手册 §5.4.5）
非叶目录项的格式非常简单：**仅包含下一级页表的物理基地址**（bit[6]=0 且 bit[14:13]=0 时，LDDIR 将其解释为物理地址）。

当前代码 `PA2PTE(child_pa) | PTE_V` 的写法：`PTE_V`（bit 0）在目录项中**不是必须的**（LDDIR 指令检查的是 bit[6] 是否为大页标志，而非 V 位）。但保留 V 位不影响功能，因为 LDDIR 只关心 `[14:13]` 和 `[6]` 这两个标志位来判断类型。

**实际格式**：目录项只需填入下一级页表的物理地址（低位为 0），LDDIR/LDPTE 指令会直接将其作为地址使用。

**English:**
Per §5.4.5, non-leaf directory entries are simple: they contain **only the physical base address of the next-level page table** (when bit[6]=0 and bit[14:13]=0, `LDDIR` treats the value as a physical address).

Current code `PA2PTE(child_pa) | PTE_V`: The `PTE_V` bit (bit 0) is **not required** for directory entries (LDDIR checks bit[6] for huge-page flag, not the V bit). Its presence doesn't break things since LDDIR only interprets bits [14:13] and [6].

**Practical format**: A directory entry is simply the physical address of the next-level table (lower bits = 0). LDDIR/LDPTE use it directly as an address.

---

### Q4.4 — NX/NR/RPLV 位语义

**中文：**（手册 §5.4.3）
- `NX=0` → **可执行**（NX=1 才触发 PNX 不可执行例外）
- `NR=0` → **可读**（NR=1 才触发 PNR 不可读例外）
- `RPLV=0` → 任何特权等级不低于 PLV 的程序均可访问（即"公开"页）
- `RPLV=1` → 只有特权等级**等于** PLV 的程序可访问（严格限制）

当前代码 PTE_R=0, PTE_X=0 依赖 `NX=0` 可执行的假设是**正确的**。

**English:**
Per §5.4.3:
- `NX=0` → **executable** (NX=1 triggers PNX exception)
- `NR=0` → **readable** (NR=1 triggers PNR exception)
- `RPLV=0` → any privilege level not lower than PLV can access (open/public page)
- `RPLV=1` → only programs with privilege level **equal to** PLV can access

The assumption that `NX=0` means executable is **correct**.

---

## 五、异常码映射 [P1]

### Q5.1 — LoongArch 完整异常码列表（ESTAT.Ecode）

**中文：**（手册表 7-8）

| Ecode | 代号 | 含义 |
|---|---|---|
| 0x0 | INT | 中断（仅 ECFG.VS=0 时） |
| 0x1 | PIL | load 操作页无效 |
| 0x2 | PIS | store 操作页无效 |
| 0x3 | PIF | **取指操作页无效**（对应 RISC-V scause=0xC）|
| 0x4 | PME | 页修改例外 |
| 0x5 | PNR | 页不可读 |
| 0x6 | PNX | 页不可执行 |
| 0x7 | PPI | 页特权等级不合规 |
| 0x8 | ADEF/ADEM | 地址错（取指/访存） |
| 0x9 | ALE | 地址非对齐 |
| 0xA | BCE | 边界检查错 |
| 0xB | SYS | **系统调用**（对应 RISC-V scause=8）|
| 0xC | BRK | 断点 |
| 0xD | INE | **指令不存在**（对应 RISC-V scause=2）|
| 0xE | IPE | 指令特权等级错 |
| 0xF | FPD | 浮点未使能 |
| 0x10 | SXD | 128位扩展未使能 |
| 0x11 | ASXD | 256位扩展未使能 |
| 0x12 | FPE/VFPE | 浮点/向量浮点例外 |

**重要**：TLB 重填例外（TLBR）**没有 Ecode**，触发时 `ESTAT.Ecode` 域**保持不变**（硬件不写入）。

**English:**
Per Table 7-8:

| Ecode | Mnemonic | Meaning |
|---|---|---|
| 0x1 | PIL | Load page invalid |
| 0x2 | PIS | Store page invalid |
| 0x3 | PIF | **Fetch page invalid** (→ RISC-V scause=0xC) |
| 0x4 | PME | Page modification |
| 0x5 | PNR | Page not readable |
| 0x6 | PNX | Page not executable |
| 0x7 | PPI | Page privilege violation |
| 0xB | SYS | **System call** (→ RISC-V scause=8) |
| 0xD | INE | **Instruction not exist** (→ RISC-V scause=2) |
| 0xE | IPE | Instruction privilege error |

**Critical**: TLB refill exception has **no Ecode** — `ESTAT.Ecode` is **not updated** by hardware when TLB refill triggers.

---

### Q5.2 — 缺失异常码的映射建议

**中文：**
- **PME (Ecode=0x4, 页修改)**：对应 RISC-V scause=15（Store/AMO page fault）
- **IPE (Ecode=0xE, 指令特权等级错)**：可映射为 scause=12（Instruction page fault）或自定义值。在 xv6-LA 中用户态代码不应触发（PLV3 下合规），内核中可以触发（PLV0 下访问了受限页）

**English:**
- **PME (Ecode=0x4, page modification)**: Map to RISC-V scause=15 (Store/AMO page fault)
- **IPE (Ecode=0xE, instruction privilege error)**: Map to scause=12 (Instruction page fault) or a custom value. Should not trigger from user PLV3 code if pages are correctly marked

---

## 六、定时器架构细节 [P2]

### Q6.1 — 稳定时钟源

**中文：**（手册 §2.2.10.4）
LoongArch 提供**恒定频率计时器（Stable Counter）**，这是一个 64 位单调递增计数器，复位后从 0 开始，以固定频率自增（不受处理器频率影响）。

访问方式：
```asm
rdtime.d  rd, rj   # rd = Stable Counter 值（64位），rj = Counter ID
```
- `rdtime.d` 是架构定义的指令，**QEMU la464 应当实现**（架构要求）
- 若在 PLV3 下被禁用（CSR.MISC 中的 DRDTL3=1），触发 IPE 例外，但默认值为 0（允许）

**English:**
Per §2.2.10.4: LoongArch defines a **Stable Counter** — a 64-bit monotonically increasing counter that starts at 0 on reset and increments at a fixed frequency independent of CPU clock.

Access:
```asm
rdtime.d  rd, rj   # rd = Stable Counter value (64-bit), rj = Counter ID
```
- `rdtime.d` is architecturally defined and **should be implemented in QEMU la464**
- Can be disabled in PLV3 via `CSR.MISC.DRDTL3=1`, but default is 0 (allowed)

---

### Q6.2 — TCFG 寄存器的精确位域

**中文：**（手册 §7.6.2，表 7-45）

| 位 | 名称 | 描述 |
|---|---|---|
| 0 | En | 定时器使能 |
| 1 | Periodic | =1 循环模式，=0 单次模式 |
| n-1:2 | InitVal | 初始值（**n 由实现决定**，不是固定的 32 位） |
| GRLEN-1:n | 0 | 只读恒为 0 |

**当前假设 bits[31:2] 共 30 位是否正确**：n 的值依赖于实现（QEMU la464 可能是 48 位宽），建议通过测试确认。InitVal 要求必须是 4 的整倍数（最低 2 位硬件自动补 0）。

Periodic=1 时，定时器减到 0 后自动重载为 InitVal 重新计时。

**English:**
Per §7.6.2, Table 7-45:

| Bits | Name | Description |
|---|---|---|
| 0 | En | Timer enable |
| 1 | Periodic | =1 repeating mode, =0 one-shot |
| n-1:2 | InitVal | Initial value (**n is implementation-defined**) |
| GRLEN-1:n | 0 | Reserved |

**Regarding your assumption of bits[31:2] = 30-bit InitVal**: The actual width `n` depends on the implementation (QEMU la464 may support up to 48 bits). InitVal must be a multiple of 4 (hardware auto-pads two zero bits at the bottom). In Periodic mode, timer auto-reloads from InitVal when it reaches zero.

---

## 七、原子指令与 LLBCTL [P2]

### Q7.1 — LLBCTL (CSR 0x60) 的配置需求

**中文：**（手册 §7.4.17，表 7-19）

| 位 | 名称 | 描述 |
|---|---|---|
| 0 | ROLLB | 只读，当前 LLbit 的值 |
| 1 | WCLLLB | 写 1 将 LLbit 清 0 |
| 2 | KLO | =1 时，下一次 ERTN 不清 LLbit（一次性有效） |

**结论**：LLBCTL 的默认值（复位后 KLO=0）已满足 LL/SC 正常工作，**无需预先配置**。LL/SC 只需要访存地址是一致可缓存（CC）类型，LLBCTL 无需特殊设置。

**English:**
Per §7.4.17, Table 7-19:

| Bit | Name | Description |
|---|---|---|
| 0 | ROLLB | Read-only, current LLbit value |
| 1 | WCLLLB | Write 1 to clear LLbit |
| 2 | KLO | =1 prevents next ERTN from clearing LLbit (one-shot) |

**Conclusion**: The default reset value (KLO=0) is sufficient for LL/SC to work correctly. **No pre-configuration needed.** LL/SC only requires the access address has CC (coherent cached) memory type.

---

## 八、QEMU la464 特定行为 [P1]

### Q8.1 — CPU 复位状态（手册 §6.4）

**中文：**（手册 §6.4 给出了架构定义的复位值）

| CSR 域 | 复位值 |
|---|---|
| CRMD.PLV | 0 |
| CRMD.IE | 0 |
| CRMD.DA | **1** |
| CRMD.PG | **0** |
| CRMD.DATF | 0 |
| CRMD.DATM | 0 |
| ECFG.VS, LIE | 0 |
| ESTAT.IS[1:0] | 0 |
| TCFG.En | 0 |
| LLBCTL.KLO | 0 |
| TLBRERA.IsTLBR | 0 |
| 所有 DMW.PLV0~PLV3 | **0**（全部禁用）|

复位后首条指令 PC = `0x1C000000`（物理地址，DA=1 直通模式）。

**English:**
Per §6.4 (architecture-defined reset values):

| CSR field | Reset value |
|---|---|
| CRMD.PLV | 0 |
| CRMD.IE | 0 |
| CRMD.DA | **1** |
| CRMD.PG | **0** |
| CRMD.DATF | 0 |
| CRMD.DATM | 0 |
| TCFG.En | 0 |
| TLBRERA.IsTLBR | 0 |
| All DMW.PLV0~PLV3 | **0** (all disabled) |

First instruction PC after reset = `0x1C000000` (physical address, DA=1 direct mode).

---

### Q8.2 — QEMU la464 TLB refill 的完整语义（手册层面）

**中文：**
从手册规范来说：
- TLBRENTRY 调用：CPU 在 TLB miss 时自动跳转到 TLBRENTRY 所指地址（物理地址，DA=1 模式）。
- `tlbwr` 同时更新 STLB 或 MTLB（根据页大小决定），无需担心 ITLB/DTLB 分离。
- QEMU la464 是否有已知 TLB bug：这需要实际测试，架构手册不涉及模拟器实现细节。建议在 QEMU 中用 `info tlb` 命令查看 TLB 状态来调试。

**English:**
From the architecture spec:
- TLBRENTRY invocation: CPU automatically jumps to the physical address stored in TLBRENTRY on TLB miss (DA=1 mode is active).
- `tlbwr` updates the unified TLB (STLB or MTLB based on page size) — no ITLB/DTLB split to worry about.
- Known QEMU la464 TLB bugs: must be verified empirically. Use QEMU's `info tlb` monitor command to inspect TLB state during debugging.

---

## 九、用户程序 ABI [P1]

### Q9.1 — ELF 入口约定

**中文：**
`loongarch64-linux-gnu-ld` 使用 Linux ABI，即使加了 `-nostdlib` 也默认链接 `crt1.o`（来自 glibc）。解决方法：
- 使用 `-nostartfiles` 阻止链接 CRT
- 或使用 `-e main` 配合 `--no-dynamic-linker`
- 完整推荐：`-nostdlib -nostartfiles -e main`

**English:**
`loongarch64-linux-gnu-ld` defaults to linking `crt1.o` from glibc even with `-nostdlib`. Solutions:
- Use `-nostartfiles` to prevent CRT linkage
- Or use `-e main` with `--no-dynamic-linker`
- Full recommendation: `-nostdlib -nostartfiles -e main`

---

### Q9.2 — syscall 指令语义

**中文：**（手册 §2.2.10.1）
`syscall code` 会**无条件立即触发系统调用例外（SYS，Ecode=0xB）**。`code` 域的值作为参数供例外处理程序使用，但对例外的触发没有条件判断。`syscall 0` 与 `syscall N` 效果相同（都触发 SYS 例外）。

**English:**
Per §2.2.10.1: `syscall code` **unconditionally and immediately triggers the system call exception (SYS, Ecode=0xB)**. The `code` field is available to the exception handler as a parameter, but the exception itself is always triggered. `syscall 0` behaves identically to `syscall N` in terms of triggering SYS.

---

## 十、关键 CSR 编号确认 [P1]

**中文 / English：**

| CSR 编号 | 名称 | 当前代码用途 | **确认状态** |
|---|---|---|---|
| 0x00 | CRMD | 当前模式控制 | ✅ 正确 |
| 0x01 | PRMD | 普通例外前模式 | ✅ 正确 |
| 0x04 | ECFG | 例外配置 | ✅ 正确 |
| 0x05 | ESTAT | 例外状态 | ✅ 正确 |
| 0x06 | ERA | 普通例外返回地址 | ✅ 正确 |
| 0x07 | BADV | 例外虚拟地址 | ✅ 正确 |
| 0x0C | EENTRY | 普通例外入口 | ✅ 正确 |
| 0x10 | TLBIDX | TLB 索引 | ✅ 正确 |
| 0x11 | TLBEHI | TLB 表项高位（非refill上下文） | ✅ 正确 |
| 0x12 | TLBELO0 | TLB 表项低位0（非refill上下文） | ✅ 正确 |
| 0x13 | TLBELO1 | TLB 表项低位1（非refill上下文） | ✅ 正确 |
| 0x19 | PGDL | 页表基址（低地址空间） | ✅ 正确 |
| 0x20 | CPUID | 核心 ID | ✅ 正确 |
| 0x30 | KSave0 | 临时保存0 | ✅ 正确 |
| 0x31 | KSave1 | 临时保存1 | ✅ 正确 |
| 0x41 | TCFG | 定时器配置 | ✅ 正确 |
| 0x42 | TVAL | 定时器值 | ✅ 正确 |
| 0x44 | TICLR | 定时器中断清除 | ✅ 正确 |
| 0x88 | TLBRENTRY | TLB refill 入口地址 | ✅ 正确 |
| 0x89 | TLBRSAVE | TLB refill 数据保存 | ⚠️ 注意：0x89 是 **TLBRSAVE**（通用数据保存寄存器），**不是 TLBRPRMD** |
| 0x8A | TLBRERA | TLB refill 返回地址 | ✅ 正确 |
| 0x180 | DMW0 | 直接映射窗口0 | ✅ 正确 |
| 0x181 | DMW1 | 直接映射窗口1 | ✅ 正确 |

**⚠️ 重要更正**：你的清单中写"0x89 = TLBRPRMD（待确认）"，但实际上：
- **0x89 = TLBRSAVE**（TLB refill 数据保存，相当于普通上下文的 KSave）
- **TLBRPRMD** 的实际 CSR 编号需查手册确认（位于 0x8x 区间，建议查表 7-41 对应的寄存器编号）

---

## 关键 Bug 诊断总结

**中文：**
基于以上分析，ERA=0 的最可能原因是：

1. **PS 未设置**：TLB refill handler 没有写 `TLBREHI.PS`（或写了 PS=0），`tlbfill` 创建了页大小不对的条目。指令重试时 TLB 命中了错误条目，导致映射到 PA=0，然后在 PA=0 处触发了新的例外，此时的 faulting PC 是 0。

2. **使用了 tlbwr 而非 tlbfill**：在 TLB refill 上下文中，应优先用 `tlbfill`（硬件随机选择槽位）而非 `tlbwr`（需要指定 TLBIDX.Index，若 Index 指向 STLB 但 VPPN/PS 矛盾则行为未定义）。

3. **TLBRELO0/TLBRELO1 没有正确设置**：在 IsTLBR=1 时，`tlbwr`/`tlbfill` 使用 TLBRELO 系列寄存器，不是 TLBELO 系列。

**English:**
Based on the analysis, ERA=0 is most likely caused by:

1. **PS not set**: TLB refill handler didn't write `TLBREHI.PS` (or wrote PS=0). `tlbfill` creates wrong-sized entries. On retry the TLB hits a wrong entry, maps to PA=0, and a new fault triggers at PC=0.

2. **Using `tlbwr` instead of `tlbfill`**: In TLB refill context, prefer `tlbfill` (hardware randomly selects slot). `tlbwr` requires a valid `TLBIDX.Index`; wrong index can cause undefined behavior.

3. **TLBRELO0/TLBRELO1 not written**: When IsTLBR=1, `tlbwr`/`tlbfill` consume `TLBRELO0/1`, not `TLBELO0/1`.