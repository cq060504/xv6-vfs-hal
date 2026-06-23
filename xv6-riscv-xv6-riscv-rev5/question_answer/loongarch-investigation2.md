---

## LoongArch TLB Refill 待确认问题 — 手册原文答复

---

## Q1. TLBRPRMD 的 CSR 编号与位域定义

**CSR 编号：`0x8F`**

CSR 地址表（§7.5，手册第 147-151 页）明确列出：

| 地址 | 名称 |
|------|------|
| 0x88 | TLBRENTRY |
| 0x89 | **TLBRBADV**（注意：不是 TLBRSAVE！） |
| 0x8A | TLBRERA |
| 0x8B | TLBRSAVE |
| 0x8C | TLBRELO0 |
| 0x8D | TLBRELO1 |
| 0x8E | TLBREHI |
| **0x8F** | **TLBRPRMD** |

> ⚠️ 原问题列表有一处错误：`0x89 = TLBRSAVE` 是错的，**0x89 = TLBRBADV**，0x8B 才是 TLBRSAVE。

**位域定义（Table 54，§7.5.17）：**

| Bits | Name | 说明 |
|------|------|------|
| **1:0** | **PPLV** | TLB refill 触发时，硬件将 CSR.CRMD.PLV 保存到此。ertn (IsTLBR=1) 时恢复到 CSR.CRMD.PLV |
| **2** | **PIE** | TLB refill 触发时，硬件将 CSR.CRMD.IE 保存到此。ertn (IsTLBR=1) 时恢复到 CSR.CRMD.IE |
| 3 | 0 | 无虚拟化扩展时只读 0 |
| **4** | **PWE** | TLB refill 触发时，硬件将 CSR.CRMD.WE 保存到此。ertn (IsTLBR=1) 时恢复到 CSR.CRMD.WE |
| 31:5 | 0 | 保留 |

**解答四小问：**
1. ✅ TLBRPRMD 的 CSR 编号是 **0x8F**
2. ✅ 格式与 PRMD 相似但有差异：bit[1:0]=PPLV，bit[2]=PIE，bit[4]=PWE（bit[3] 是虚拟化 Guest 模式位，裸机实现为 0）
3. ✅ 是的，TLB refill 触发时硬件**自动**将 {PLV, IE} 分别写入 TLBRPRMD.{PPLV, PIE}，同时 DA→1, PG→0
4. ✅ ertn (IsTLBR=1) 时：CRMD.PLV ← TLBRPRMD.PPLV，CRMD.IE ← TLBRPRMD.PIE，CRMD.WE ← TLBRPRMD.PWE

---

## Q2. TLB Refill 时 DA/PG 的状态

§6.3.4 原文（硬件触发 TLB refill 时做的事）：

> *"set PLV in CSR.CRMD to 0, IE to 0, **DA to 1** and **PG to 0**"*

ertn 返回时（IsTLBR=1）：

> *"Set **DA** in CSR.CRMD **to 0** and **PG to 1**"*

**逐问解答：**

1. ✅ **DA=1 是真实修改了 CRMD.DA 寄存器**，并非"内部模式"。硬件在触发 TLB refill 时直接写 CRMD：DA←1，PG←0。可以通过 csrrd 读出确认。

2. ✅ 是的。DA=1 模式下取指也走直接地址映射（Physical Address = VA[PALEN-1:0]），handler 在 flash 0x1C008000，DA=1 时 VA 直接等于 PA，取指完全正确。

3. ✅ ertn (IsTLBR=1) 由硬件直接将 DA←0, PG←1，**不从任何 CSR 恢复**，而是硬编码写入。PGDL/PGDH **不会在 TLB refill 期间被硬件修改**，userret 设置的 PGDL 值在整个 TLB refill 过程中保持不变。ertn 后 PGDL 仍然是用户页表基址。

4. ✅ 正确。在 DA=1 期间，handler 通过 PGDL 读页表时，PGDL 中存的是**物理地址**，DA=1 将该地址直接当作物理地址访问，逻辑完全正确。手册原文：
   > *"the addresses configured in the PGD and in the directory entries of the page table in memory must be physical addresses."*（§5.4.3.3 末尾）

---

## Q3. ertn (IsTLBR=1) 后取指是否再次触发 TLB miss？

**解答四小问：**

1. ✅ **tlbfill 填充的条目在 ertn 后立即可用**。TLBFILL 将条目写入硬件 TLB（随机槽），写入即生效，没有 pipeline flush 要求。ertn 后 CPU 以 PC=0x0 取指时，TLB 查找会命中刚填入的条目（假设条目正确：V=1，ASID 匹配，VPPN 匹配）。

2. ✅ **ertn (IsTLBR=1) 本身不触发任何隐式 INVTLB**。手册明确列出 ertn 的动作：恢复 PPLV/PIE/PWE，DA←0，PG←1，IsTLBR←0，跳转。没有任何 TLB flush 操作。

3. ✅ TLB refill 期间 PGDL **不被硬件修改**。ertn 后 PGDL 仍是 userret 设置的用户页表基址。

4. ⚠️ **这是关键问题所在**。TLB 查找时使用的 ASID 来自 CSR.ASID。根据手册 §7.5.4：
   > *"It is used as the ASID key value information for querying the TLB when fetching instructions and executing the load/store instructions."*
   
   ASID 是"第一类 TLB 相关 CSR"（与 TLBEHI、TLBELO0/1 同组），**不是专属于 TLB refill context 的第三类 CSR**。这意味着：
   - TLB refill 触发时，**硬件不保存/修改 ASID**
   - TLBFILL 写入 TLB 时，条目的 ASID 字段来自 **CSR.ASID**（手册：*"the value written to the ASID field of the TLB table entry is derived from this"*）
   - ertn 后取指时，ASID 匹配同样用 CSR.ASID

   **因此：只要 userret 在进入用户态前正确设置了 CSR.ASID = 用户进程 ASID，TLBFILL 写入的条目 ASID 就与取指时查找的 ASID 一致。若 CSR.ASID 为 0 或错误值，则 TLB 命中后仍会 miss。**

---

## Q4. TLBRERA 格式

手册 §7.5.13，Table 48：

| Bits | Name | 说明 |
|------|------|------|
| **0** | **IsTLBR** | 1=当前处于 TLB refill 上下文。TLB refill 触发时硬件置 1；ertn (IsMERR=0) 时清 0 |
| 1 | 0 | 只读常量 0 |
| **GRLEN-1:2** | **PC** | 触发 TLB refill 的指令 PC 的 [GRLEN-1:2] 位 |

**逐问解答：**

1. ✅ PC 存放在 **bits [GRLEN-1:2]**，即 bits [1:0] **恒为 0**（4字节指令对齐，低2位不需要存储）。读出 TLBRERA 后左移 0 位即得完整 PC（低2位补0即可）。

2. ✅ **IsTLBR 在 bit 0**，bit 1 是只读常量 0。

3. ✅ 确切流程：ertn 执行时，若 `IsTLBR=1 && IsMERR=0`，则：
   - 从 TLBRPRMD 恢复 PLV/IE/PWE 到 CRMD
   - DA←0, PG←1
   - **IsTLBR 清 0**
   - 跳转到 TLBRERA 中存的 PC
   
   此后若再触发普通异常（如 PIF），IsTLBR 已为 0，PRMD/ERA 路径生效，跳转到 EENTRY（uservec），ERA 指向用户 PC。

---

## Q5. TLB Refill Handler 中是否允许嵌套例外？

手册 §5.4.3.1（TLB-related Exceptions）原文：

> *"While the TLB refill exception being caught, **CRMD will be set to 1 [DA=1]** and **PG will be set to 0**. This means the hardware will enter the direct address translation mode automatically, so that **the TLB refill exception handler itself will not trigger the TLB refill exception again**."*

**逐问解答：**

1. ✅ **"V=0 条目 → ertn → PIF → EENTRY → usertrap" 这个链条是正确的。** 流程：
   - handler 用 tlbfill 写入 V=0 的条目（或完全不写），然后 ertn
   - ertn 后 CPU 回到用户态 PLV=3，DA←0，PG←1，IsTLBR←0
   - 取指 VA=0x0，TLB 命中但 V=0 → 触发 PIF (Ecode=0x3，**不是 0xC**)
   - 这是**普通异常**，进入 EENTRY，ERA = 用户 PC，然后 usertrap 处理

2. ✅ TLB refill handler 自身执行期间（DA=1，PG=0）：
   - **不会**再次触发 TLB miss（直接地址模式下无需 TLB）
   - **可能**触发非 TLB 类普通异常（如地址对齐错误、指令未定义等），这些会以普通异常路径（EENTRY，PRMD/ERA）处理，**不会**覆盖 TLBRPRMD/TLBRERA
   - **不可能**触发嵌套 TLB refill（DA=1 保护机制）

---

## Q6. 最小 TLB Refill Handler 测试（身份映射）

**手册没有 QEMU 特定的内存布局信息**，以下是基于手册的分析：

手册说复位 PC = `0x1C000000`（§6.4）：
> *"The PC after the reset is 0x1C000000. Since the MMU must be in direct address translation mode after the reset, the physical address of the first instruction fetched after reset is also **0x1C000000**."*

注意：手册拼写为 `0x1000000`（`0x1C000000` 写法不一致，应为 `0x1C000000`）。总之手册确认**复位入口是物理地址 0x1C000000，这是 ROM/Flash 区域，不是 RAM**。

**对测试方案的分析：**

- **PA=0x0 是否有 RAM**：手册层面不指定，这是 SoC/QEMU 实现相关的问题。
- **QEMU LoongArch virt 机器**（手册外知识）：QEMU virt 机器的 RAM 通常从 `0x0` 或 `0x90000000` 等地址开始，具体看 QEMU 版本。
- **测试建议**：使用身份映射（PA=VA）测试时，若 PA=0x0 确实是 RAM 且有用户代码，执行会成功；若是 ROM 或未映射区域，会触发总线错误（machine error）而非 BRK/PIF。

---

## ⚠️ 重要诊断提示：scause=0xC ≠ PIF

根据手册 Table 21（§7.4.6）：

| Ecode | 名称 | 说明 |
|-------|------|------|
| **0x3** | **PIF** | Page Invalid exception for **Fetch** operation |
| **0xC** | **BRK** | **BReaKpoint** exception |

**`scause=0xC` 是 BRK（断点异常），不是 PIF。**

这意味着 ertn 后在 VA=0x0 执行的**第一条指令是 BRK 指令**（即 `break` 指令），而非触发了 Page Invalid。可能的原因：
- PA=0x0 处存放的是调试用 `break 0` 或 `dbar 0` 等指令
- 用户代码根本没有被加载到 PA=0x0（该位置是零初始化内存，而 LoongArch 的 `0x00000000` 编码恰好是某种特殊指令）

> 建议先检查 LoongArch 中全零指令 `0x00000000` 的编码含义——如果它是 `break 0`，则 VA=0x0 处根本没有有效用户代码。