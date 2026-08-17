# xv6虚拟文件系统和硬件抽象层的设计与实现

> **2026年全国大学生计算机系统能力大赛 · 操作系统设计赛(全国) · OS功能挑战赛道**
>
> 队伍名称：**tjuer** ｜ 学校：**天津大学** ｜ 指导教师：**李罡**
>
> 成员：陈权，杨子游

---

## 项目简介

本项目基于 **xv6-riscv rev5**（MIT教学操作系统），扩充了**虚拟文件系统（VFS）**和**硬件抽象层（HAL）**，支持xv6fs、ext2、FAT32三种文件系统以及RISC-V、LoongArch两种CPU架构。实现保留xv6教学内核的模块结构，并将平台差异集中在HAL目录。

### 核心成果

| 维度               | 原xv6-riscv rev5              | 本项目                                |
| ------------------ | ----------------------------- | ------------------------------------- |
| 文件系统           | xv6fs                         | xv6fs + ext2 + FAT32                  |
| CPU架构            | RISC-V                        | RISC-V + LoongArch                    |
| VFS                | 无                            | 669行核心实现，3个文件系统后端        |
| HAL                | RISC-V平台代码位于`kernel/` | 8个公共头文件 + 2套平台实现，共3551行 |
| 通用内核架构条件块 | RISC-V专用                    | `kernel/`中为0                      |

---

## 项目特性

- **VFS虚拟文件系统层** — 统一vnode抽象，13个操作接口，挂载点自动穿越，`..`跨文件系统返回，引用计数生命周期管理
- **ext2文件系统实现** — 1156行，覆盖文件、目录、设备节点、读写、删除、硬链接、时间戳和单间接块
- **xv6fs胶水层** — 497行，将原生inode包装为vnode；`kernel/fs.c`和`kernel/log.c`保持原实现
- **FAT32文件系统** — 1076行，支持8.3短名、VFAT LFN、簇链读写、O_APPEND和O_TRUNC，双架构运行
- **HAL硬件抽象层** — 241行，公共接口覆盖启动、页表、中断、定时器、串口、上下文切换和块设备；RISC-V与LoongArch分别实现
- **LoongArch稳定性机制** — 软件TLB重填、NR/NX权限转换、高地址PGDH内核栈guard、UART定时轮询后备
- **三块独立设备** — dev=1/2/3分别承载xv6fs、ext2和FAT32；RISC-V使用virtio-mmio，LoongArch使用loader-backed RAM disk
- **测试结果** — 双架构`usertests -q`通过，ext2综合测试15/15通过，FAT32测试42/42通过
- **Docker开发环境** — RISC-V 和 LoongArch 两个隔离容器，统一挂载源码目录

---

## 项目分支

| 分支                 | 说明                                                   |
| -------------------- | ------------------------------------------------------ |
| `master`           | 项目主分支，包含当前 VFS + HAL + ext2 + FAT32 集成实现 |
| `final-submission` | 最终提交版本（已合并到 master）                        |
| `HAL`              | HAL 硬件抽象层早期开发分支                             |
| `vfs`              | VFS 虚拟文件系统早期开发分支                           |
| `merge-vfs-hal`    | VFS 与 HAL 合并集成历史分支                            |

### 仓库远程

| 远程       | 地址            | 用途                                |
| ---------- | --------------- | ----------------------------------- |
| `origin` | GitHub          | 主仓库，所有分支最新                |
| `educg`  | GitLab 教育平台 | 比赛提交仓库，与 origin master 同步 |

---

## 快速开始

### 环境准备（Docker）

RISC-V 和 LoongArch 使用两个独立的 Docker 容器，共用同一份源码目录。

**RISC-V 容器**

```bash
sudo docker run -it --rm -v $(pwd):/xv6 debian:bookworm bash

# 容器内首次安装工具链
apt-get update && apt-get install -y --no-install-recommends \
    gcc-riscv64-linux-gnu binutils-riscv64-linux-gnu \
    qemu-system-misc qemu-system-data \
    make python3 perl git build-essential dosfstools \
    && rm -rf /var/lib/apt/lists/*
```

**LoongArch 容器**

```bash
cd xv6-riscv-xv6-riscv-rev5
sudo docker build -t xv6-loongarch -f Dockerfile.loongarch .
sudo docker run -it --rm -v $(pwd):/xv6 xv6-loongarch bash
```

### 构建与运行

**RISC-V**

```bash
cd xv6-riscv-xv6-riscv-rev5
make clean && make qemu
```

**LoongArch**

```bash
cd xv6-riscv-xv6-riscv-rev5
make ARCH=loongarch clean && make ARCH=loongarch qemu
```

> 退出 QEMU：`Ctrl-a` 然后按 `x`

### 运行测试

在 xv6 shell 中执行：

```bash
# xv6 原生测例
usertests -q

# ext2 综合测试（自动挂载 + 跨文件系统拷贝 + 功能测试）
cat demo.sh | sh

# FAT32测试（RISC-V和LoongArch）
fat32test
```

| 测试                                 | 结果                       | 说明                                                         |
| ------------------------------------ | -------------------------- | ------------------------------------------------------------ |
| `usertests -q`                     | 双架构`ALL TESTS PASSED` | fork、exec、pipe、file、fs等原生回归                         |
| ext2测试（`demo.sh`/`ext2test`） | 双架构15/15通过            | 挂载、vnode操作、路径、并发、unlink-while-open、单间接大文件 |
| `fat32test`                        | 双架构42/42通过            | 挂载、读写、LFN、O_APPEND、O_TRUNC、多簇、目录和删除         |

---

## 架构概览

```
用户态应用程序（sh, cat, ls, ext2test, ...）
    │ 系统调用 (sys_open, sys_read, sys_write, sys_mount, ...)
    ▼
┌─────────────────────────────────────────────┐
│          VFS 虚拟文件系统层                  │
│  vfs.c/h  (669行)                           │
│                                             │
│  · vnode 统一文件抽象                        │
│  · vnode_ops 操作接口 (13个函数指针)          │
│  · mount 挂载管理 (mounttable[NMOUNT])        │
│  · namei 路径遍历 + 挂载点自动穿越             │
│  · 引用计数生命周期管理                        │
├────────────┬────────────┬────────────────────┤
│  xv6fs胶水  │  ext2实现  │  FAT32实现         │
│ xv6fs.c   │ ext2.c/h  │  fat32.c/h          │
│  497行     │ 1156行    │ 1076行             │
│            │            │                    │
│ 包装原生   │ 完整ext2   │ FAT32文件系统      │
│ inode为    │ 文件系统    │ 读写支持           │
│ vnode      │ 读写支持    │                    │
├────────────┴────────────┴────────────────────┤
│            HAL 硬件抽象层                      │
│  hal/  (3399行，含公共接口和双平台实现)        │
│                                               │
│  hal_arch.h    CPU/CSR寄存器                   │
│  hal_vm.h      页表管理                        │
│  hal_intr.h    中断控制器                      │
│  hal_timer.h   定时器                          │
│  hal_ctx.h     上下文切换                      │
│  hal_console.h 串口                            │
├──────────────────┬────────────────────────────┤
│   RISC-V 实现     │    LoongArch 实现          │
│   hal/riscv/     │    hal/loongarch/          │
└──────────────────┴────────────────────────────┘
```

VFS通过统一vnode接口连接系统调用与具体文件系统；HAL通过公共接口连接通用内核与平台实现。文件系统扩展依赖VFS接口，CPU移植依赖HAL接口，两条扩展路径保持正交。

---

## HAL设计与平台适配

### HAL设计概述

HAL采用编译期静态绑定。Makefile根据`ARCH`选择工具链、头文件搜索路径、平台对象、链接脚本和QEMU参数：

```make
CFLAGS += -DARCH_$(ARCH)
CFLAGS += -Ihal/$(ARCH)
OBJS = $(K_OBJS) $(ARCH_OBJS)
```

`K_OBJS`在两个架构构建中保持一致，`ARCH_OBJS`分别选择PLIC/virtio或EIOINTC/RAM disk等实现。当前`kernel/`中不存在`ARCH_riscv`或`ARCH_loongarch`条件块。

| 公共接口          | 功能                                                      | 主要调用位置                            |
| ----------------- | --------------------------------------------------------- | --------------------------------------- |
| `hal_arch.h`    | hart ID、中断总开关、异常状态、异常向量、页表根、CPU idle | `proc.c`、`trap.c`                  |
| `hal_vm.h`      | 固定映射、MMU使能、TLB、叶PTE、用户特殊映射和低地址保护   | `vm.c`、`proc.c`、`exec.c`        |
| `hal_intr.h`    | 中断控制器初始化、claim和complete                         | `main.c`、`trap.c`                  |
| `hal_timer.h`   | 读取时间、设置/确认定时器                                 | `main.c`、`trap.c`                  |
| `hal_console.h` | UART初始化、批量输出、同步输出、中断/轮询接收             | `console.c`、`printf.c`、`trap.c` |
| `hal_ctx.h`     | 平台ABI上下文和`hal_switch()`                           | `proc.c`                              |
| `hal_disk_*`    | 块设备初始化、读写和中断完成                              | `main.c`、`bio.c`、`trap.c`       |

公共初始化链为：

```text
consoleinit() → kinit() → kvminit() → hal_vm_map_kernel()
  → kvminithart() → hal_vm_enable()
  → procinit() → trapinit()/trapinithart()
  → hal_irq_init()/hal_irq_hart_init()
  → binit()/iinit()/vfs_init()/fileinit()
  → hal_disk_init() → hal_timer_init() → userinit() → scheduler()
```

原有10个LoongArch条件块收敛为7组平台职责：用户低地址预留、用户trapframe绑定、UART轮询、trampoline映射/解除、内核地址空间建立/使能和叶PTE判定。

### RISC-V平台适配

RISC-V实现位于`hal/riscv/`，共12个文件、1526行，面向QEMU `virt`机器。

| 子系统     | 实现                                                                                                                                 |
| ---------- | ------------------------------------------------------------------------------------------------------------------------------------ |
| 启动       | `hal_entry.S`按`mhartid`选择每核栈；`hal_start.c`在M态配置MPP、异常委托、PMP和SSTC后通过`mret`进入S态                        |
| 虚拟内存   | Sv39三级页表；`hal_vm_map_kernel()`映射UART、PLIC、3个virtio窗口、内核代码和RAM；`hal_vm_enable()`写`satp`并执行`sfence.vma` |
| 用户trap   | `sscratch`暂存用户`a0`；TRAMPOLINE和TRAPFRAME映射在每张用户页表；`userret`切回用户`satp`并执行`sret`                       |
| 内核trap   | `kernelvec`在当前内核栈保存caller-saved寄存器并调用`kerneltrap()`                                                                |
| 上下文切换 | 保存`ra`、`sp`、`s0-s11`共14个ABI callee-saved寄存器                                                                           |
| 外部中断   | PLIC配置priority、per-hart enable、threshold、claim和complete；UART IRQ 10，磁盘IRQ 1/2/3                                            |
| 定时器     | SSTC；M态授权`time/stimecmp`，S态设置下一次比较值                                                                                  |
| UART       | 16550中断驱动RX/TX；`hal_console_write()`使用睡眠/唤醒，`hal_putchar()`轮询LSR bit 5                                             |
| 磁盘       | 三个virtio-mmio设备；每个请求使用header、data、status三个描述符并通过used ring完成                                                   |

RISC-V块请求执行链为：

```text
bread(dev, blockno) → hal_disk_rw()
  → descriptor chain → QueueNotify → sleep
  → PLIC IRQ → hal_disk_intr() → wakeup
```

### LoongArch平台适配

LoongArch实现位于`hal/loongarch/`，共14个文件、1673行，面向QEMU 8.2.2 `virt`机器和`la464` CPU。

| 子系统       | 实现                                                                                                              |
| ------------ | ----------------------------------------------------------------------------------------------------------------- |
| 启动和链接   | `-bios`装载`0x1c000000`固件；CPU 0复制`.data`到`0x00400000`并清零`.bss`；其他CPU等待`BOOT_DONE_MAGIC` |
| 地址转换     | 低地址PLV0对象通过DMW0恒等映射，用户低地址通过PGDL，高地址内核栈通过固定PGDH                                      |
| 页表         | 四级`9-9-9-9-12`；用户`MAXVA=1<<38`；`user.ld`从`0x2000`链接，低两页映射为PLV0保护页                      |
| PTE权限      | 公共R/X请求由`hal_pte_encode_perm()`转换为LA64 NR/NX原生位；P位用于叶PTE判定                                    |
| TLB          | `hal_tlbrefill.S`使用`lddir/ldpte/tlbfill`完成软件遍历；无效PTE填入V=0条目并转入PIL/PIS/PIF                   |
| 用户trap     | KSave0保存用户`a0`，KSave1保存trapframe内核地址；PGDL在用户和内核页表之间切换；`ertn`返回PLV3                 |
| 内核栈       | 每进程两页有效栈加一页无PTE guard；高地址栈由PGDH映射；guard异常先切换到每CPU紧急栈                               |
| 中断和定时器 | PCH-PIC连接EIOINTC；定时器使用周期TCFG和TICLR，不经过外部中断控制器                                               |
| UART         | TX轮询LSR bit 5；当前QEMU版本的RX外部IRQ未到达CPU，使用约10ms定时轮询后备                                         |
| 磁盘         | QEMU loader把三幅镜像放入`0x09000000/0x0a000000/0x0b000000`；`hal_ramdisk.c`执行同步BSIZE复制                 |

LoongArch的三设备布局为：

| 设备  | 镜像          | 地址           | 镜像大小 | 驱动窗口 |
| ----- | ------------- | -------------- | -------- | -------- |
| dev=1 | `fs.img`    | `0x09000000` | 约2MiB   | 16MiB    |
| dev=2 | `ext2.img`  | `0x0a000000` | 8MiB     | 16MiB    |
| dev=3 | `fat32.img` | `0x0b000000` | 10MiB    | 16MiB    |

`-bios`仅装载约120KiB内核，文件系统镜像通过generic loader进入保留RAM。`PHYSTOP`以下内存由`kalloc()`管理，三个RAM disk窗口位于`PHYSTOP`以上。驱动写入在当前QEMU进程中可见，退出后未回写宿主镜像。

完整代码说明见[`决赛实验报告.md`](xv6-riscv-xv6-riscv-rev5/决赛实验报告.md)第五至第七章。

---

## 关键技术点

| 技术点                           | 说明                                                                                           |
| -------------------------------- | ---------------------------------------------------------------------------------------------- |
| **VFS 路径遍历**           | 逐级 lookup → cross_mount 挂载点穿越 → vput 释放，约 45 行核心逻辑                           |
| **".." 跨 FS 返回**        | 检测 root vnode → 穿越回父 FS 的 mountpoint → 继续 look ".."                                 |
| **orphaned 延迟释放**      | unlink 将 links_count 降为 0 后标记 orphaned，vput 时真正释放，保证 open-after-unlink 并发安全 |
| **ext2 目录插入**          | 三策略：复用已删除条目 → 拆分 padding → 扩展新块                                             |
| **引用计数管理**           | vnode ref 通过 vget/vput 精确管理，延迟销毁在锁外调用 destroy 避免死锁                         |
| **Zero-modification 包装** | xv6fs 胶水层对`kernel/fs.c` 和 `kernel/log.c` 一行不改                                     |
| **HAL边界**                | `kernel/`中的10个LoongArch条件块收敛为7组接口职责，当前通用内核架构条件块为0                 |
| **软件TLB重填**            | LoongArch通过四级`lddir/ldpte`遍历、偶奇页装入和V=0无效项转发处理TLB miss                    |
| **内核栈guard**            | LoongArch使用高地址PGDH映射两页栈和一页guard，异常入口切换紧急栈后输出诊断                     |
| **三设备布局**             | 两个平台均使用dev=1/2/3；RISC-V使用virtio-mmio，LoongArch使用loader-backed RAM disk            |

---

## 目录结构

```
xv6-vfs-hal/
├── README.md                              ← 本文件
├── xv6-riscv-xv6-riscv-rev5/              ← 主代码目录
│   ├── xv6-vfs-hal-code.patch             ← 完整变更补丁（与原始 xv6-riscv 的 diff）
│   ├── 决赛实验报告.md                     ← 详细设计文档（11章）
│   ├── DESIGN_DOC.pdf                     ← 设计文档PDF
│   ├── 操作指南.md                        ← Docker 环境搭建与测试流程
│   ├── 演示视频.mp4                       ← 项目演示视频
│   ├── kernel/                            ← 内核源码
│   │   ├── vfs.c / vfs.h                  ← VFS核心（669行）
│   │   ├── ext2.c / ext2.h                ← ext2实现（1156行）
│   │   ├── xv6fs.c                        ← xv6fs胶水层（497行）
│   │   ├── fat32.c / fat32.h              ← FAT32实现（1076行）
│   │   ├── file.c / file.h                ← 文件描述符层
│   │   ├── sysfile.c                      ← 系统调用适配
│   │   └── ...
│   ├── hal/                               ← 硬件抽象层
│   │   ├── hal.h / hal_*.h                ← 公共接口（8个头文件，200行）
│   │   ├── riscv/                         ← RISC-V平台实现（12个文件，1526行）
│   │   └── loongarch/                     ← LoongArch平台实现（14个文件，1673行）
│   ├── user/                              ← 用户态程序
│   │   ├── ext2test.c                     ← ext2综合测试（15项）
│   │   ├── fat32test.c                    ← FAT32测试（42项）
│   │   ├── test1.c / test2.c              ← 跨文件系统拷贝测试
│   │   └── ...
│   ├── Makefile                           ← 构建系统
│   ├── demo.sh                            ← 自动化测试脚本
│   └── Dockerfile.loongarch               ← LoongArch 容器镜像
```

---

## 代码量统计

| 模块          | 文件                                                      | 当前行数 |
| ------------- | --------------------------------------------------------- | -------- |
| VFS核心       | `kernel/vfs.c` + `kernel/vfs.h`                       | 669行    |
| ext2实现      | `kernel/ext2.c` + `kernel/ext2.h`                     | 1156行   |
| xv6fs胶水     | `kernel/xv6fs.c`                                        | 497行    |
| FAT32         | `kernel/fat32.c` + `kernel/fat32.h`                   | 1076行   |
| HAL公共接口   | `hal/hal.h` + `hal/hal_*.h`                           | 241行    |
| RISC-V HAL    | `hal/riscv/`                                            | 1622行   |
| LoongArch HAL | `hal/loongarch/`                                        | 1688行   |
| 用户测试      | `ext2test.c`、`fat32test.c`、`test1.c`、`test2.c` | 1072行   |

以上数据为当前文件规模。HAL统计包含从原xv6迁移的RISC-V平台代码，因此未作为净新增行数使用。

---

## 变更补丁

主代码目录下的`xv6-vfs-hal-code.patch`为早期整合阶段相对原始xv6-riscv rev5的差异快照，共9603行。当前实现以`master`分支源码和`决赛实验报告.md`为准。该快照包含以下内容：

- HAL公共接口、RISC-V平台代码迁移和LoongArch初版移植
- VFS核心、xv6fs胶水层和系统调用适配
- ext2与FAT32文件系统实现
- 双架构Makefile和链接脚本
- ext2、FAT32及跨文件系统用户态测试

---

## 参考项目与资料

| 项目/资料                      | 链接                                                                                         |
| ------------------------------ | -------------------------------------------------------------------------------------------- |
| xv6-riscv rev5 (MIT)           | https://github.com/mit-pdos/xv6-riscv                                                        |
| 静春山 (SpringOS) — 2025 赛季 | [GitLab 教育平台](https://gitlab.eduxiji.net/educg-group-36002-2710490/T202510558995330-264)  |
| RuOK — 2025 赛季              | [GitLab 教育平台](https://gitlab.eduxiji.net/educg-group-36002-2710490/T202510486995232-2402) |
| 赛题测试仓库                   | https://github.com/yanjun-wen/xv6-extend-vfs                                                 |
| The Second Extended Filesystem | [kernel.org ext2 文档](https://www.kernel.org/doc/html/latest/filesystems/ext2.html)          |
| RISC-V ISA Manual              | https://riscv.org/technical/specifications/                                                  |

---

## 许可证

本项目基于 MIT xv6-riscv，新增代码沿用 **MIT 许可证**。

---

*最后更新：2026-08-09 ｜ 分支：master*
