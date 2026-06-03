# xv6-riscv LoongArch HAL 移植 — 交接文档

## 一、项目简介

将 MIT xv6-riscv 第5版移植到 **LoongArch (LA64)** 架构并通过硬件抽象层(HAL)同时支持 RISC-V 和 LoongArch。

- RISC-V HAL: **已完成**，`usertests -q` 全部通过
- LoongArch HAL: **开发中**，系统可启动至内核初始化完成，**第一个用户进程(init)的 TLB refill 失败**

## 二、当前阻塞点

### TLB refill handler 填入了错误的页表条目

**症状**: forkret 加载 `/init` 后，init 首次用户态指令取指触发 TLB miss → TLB refill handler 填充 TLB → 取到乱码指令 → `scause=0x2`(illegal instruction) → panic。

**根因**: 内核 `walk()` 函数创建4级页表(L3→L2→L1→L0)，但 TLB refill handler 最初是3级 walk。两者不一致导致 handler 从错误位置取 PTE、产出错误 PPN。已尝试修复为4级但仍有问题。

**vmfault 手工填 TLB 方案**: 在 vmfault 中直接用 `csrwr` 写 TLBELO + `tlbfill` 填 TLB，绕过 handler。但 `userret` 中的 `invtlb 0, $r0, $r0` 会冲掉刚填入的条目。去掉 invtlb 后系统不再崩溃但也无后续输出。

### 关键发现

1. **DMW0 配置正确**: VSEG=0, PLV0=1, PLV3=0, MAT=CC，标准 QEMU virt 机器
2. **内核通过 DMW0 访问物理页正确**: fork-chk 验证内核可读/写子进程物理页
3. **PTE 格式正确**: 与手册 Figure 8 一致
4. **HPTW 可用**: 设 TLBRENTRY=0 时系统不崩溃(说明硬件页表walker可用)
5. **PWCL/PWCH 需显式设置**: QEMU 默认值可能与4级页表不匹配

## 三、快速了解项目

### 必读文件（按顺序）

1. **`CLAUDE.md`** — 项目总体规划、HAL 接口设计、RISC-V vs LoongArch 对照表、当前进度
2. **`plan.md`** — 八周开发计划、文件清单、HAL 搬家策略

### 技术文档

| 文件 | 内容 |
|---|---|
| `../LoongArch-技术文档-EN.md` | LoongArch 官方手册：CSR、PTE格式、TLB、DMW、异常码 |
| `../LoongArch-ABI-说明.md` | LoongArch ELF ABI：寄存器约定、调用约定 |
| `../qemu-loongarch调试.md` | QEMU LoongArch 调试方法和参数 |

### TLB 参考文档

| 文件 | 内容 |
|---|---|
| `tlb-referance1.md` | TLB 指令详细说明(TLBSRCH/TLBRD/TLBWR/TLBFILL/INVTLB/LDDIR/LDPTE) |
| `tlb-referance2.md` | LoongArch 地址管理模式、DMW、存储访问类型 |
| `tlb-referance3.md` | 多级页表结构说明 |

### 参考项目

| 路径 | 说明 |
|---|---|
| `../xv6-loongarch-exp-main/` | 竞赛参考实现（使用 DMW VSEG=0x9 非标准QEMU配置，不可直接套用） |

## 四、项目结构速览

```
xv6-riscv-xv6-riscv-rev5/
├── kernel/                    # 通用内核代码(平台无关)
│   ├── vm.c                  # walk(), mappages(), vmfault(), copyinstr()
│   ├── trap.c                # usertrap(), devintr()
│   ├── proc.c                # fork(), forkret(), scheduler()
│   ├── exec.c                # kexec()
│   └── ...
├── hal/
│   ├── hal.h                 # HAL 统一入口
│   ├── hal_arch.h, hal_vm.h, hal_intr.h, hal_timer.h, hal_console.h, hal_ctx.h
│   └── loongarch/
│       ├── arch.h            # CSR定义、PTE格式(PAMASK/PA2PTE/PTE2PA/PX宏)
│       ├── memlayout.h       # QEMU virt 物理地址布局(UART0/EIOINTC/PHYSTOP等)
│       ├── hal_tlbrefill.S   # TLB refill handler(当前阻塞点)
│       ├── hal_tramp.S       # uservec/userret(用户-内核陷入/返回)
│       ├── hal_kvec.S        # kernelvec(内核态中断)
│       ├── hal_swtch.S       # 上下文切换
│       ├── hal_entry.S       # 启动入口
│       ├── hal_start.c       # M态初始化
│       ├── hal_intr.c        # EIOINTC 中断控制器
│       ├── hal_uart.c        # 16550a 串口
│       ├── hal_virtio.c      # Virtio 磁盘
│       └── kernel.ld         # 链接脚本(入口0x1C000000)
├── user/
│   ├── init.c                # 第一个用户进程
│   ├── usertests.c           # 测试用例(目标: ALL TESTS PASSED)
│   └── ...
└── Makefile                  # ARCH=loongarch 分支
```

## 五、当前代码修改状态

当前工作区修改（基于 commit `765c76f`）：

| 文件 | 修改内容 | 必要性 |
|---|---|---|
| `hal/loongarch/hal_tlbrefill.S` | 3级→4级walk + P/W mask + BADV→TLBRBADV→BADV | 核心修改 |
| `kernel/vm.c` | PWCL/PWCH 4级配置 + tlb_fill_from_pte() + vmfault已映射页TLB填充 | 核心修改 |
| `kernel/trap.c` | vmfault 增加 scause=12(指令缺页)处理 | 必要 |
| `hal/loongarch/hal_tramp.S` | 去掉所有 invtlb(阻止vmfault填的TLB被冲掉) | 调试用 |
| `kernel/proc.c` | 去掉 forkret 中的 printf debug | 可回退 |

## 六、调试方法与环境

```bash
# 进入 Docker 环境
docker run -it --rm -v $(pwd):/xv6 xv6-loongarch /bin/bash

# 编译
cd /xv6 && make ARCH=loongarch

# 运行(QEMU 8.2.2, la464, 1 core, 1G RAM)
make ARCH=loongarch qemu CPUS=1

# QEMU 调试日志(输出极多!)
qemu-system-loongarch64 -machine virt -cpu la464 -m 1G -smp 1 \
  -nographic -nic none -bios kernel/kernel.bin \
  -d in_asm,cpu -D /tmp/qemu.log

# 干净测试
git stash && make ARCH=loongarch clean && make ARCH=loongarch qemu CPUS=1
```

## 七、建议的下一步方向

1. **对齐 walk() 和 TLB handler 的页表级数**: 确认 PWCL/PWCH 实际设置了4级，handler 走了4级
2. **研究为何去掉 invtlb 后系统不崩溃却无输出**: init 是否在做磁盘I/O？timer interrupt 是否触发？
3. **直接手工填充 TLB 条目并阻止被冲掉**: vmfault 填完后设标志跳过 userret 的 invtlb
4. **考虑使用 ld.d 替换 handler 中的 lddir/ldpte**: 纯软件 walk 与内核 walkaddr 完全一致
5. **验证 QEMU la464 的 HPTW 行为**: TLBRENTRY=0 时 HPTW 是否真正填对了条目？
