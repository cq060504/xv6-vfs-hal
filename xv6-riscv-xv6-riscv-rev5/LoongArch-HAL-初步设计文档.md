# LoongArch HAL 初步设计文档

## 1. LoongArch HAL 整体设计思路

本项目的目标是在保留 xv6 原有内核主体逻辑的基础上，将体系结构相关代码下沉到 HAL 层，使同一套 kernel、user、fs、proc 等上层代码能够分别运行在 qemu-riscv 和 qemu-loongarch 环境中。LoongArch HAL 的设计重点是屏蔽启动方式、地址空间、异常中断、上下文切换、UART、定时器和块设备差异，让上层内核尽量只依赖 `hal/` 中定义的统一接口。

当前工程采用 `ARCH` 编译变量选择平台：

- `ARCH=riscv` 使用 `hal/riscv/` 下的实现。
- `ARCH=loongarch` 使用 `hal/loongarch/` 下的实现。

公共头文件位于 `hal/` 目录，例如 `hal/hal.h`、`hal/hal_arch.h`、`hal/hal_memlayout.h` 等。它们根据 `ARCH_$(ARCH)` 宏包含对应平台的实现头文件。这样做的好处是：上层代码可以继续调用类似 `hal_timer_init()`、`hal_intr_on()`、`virtio_disk_rw()` 这样的统一入口，而不需要在 kernel 主体中到处写架构条件分支。

LoongArch 的实现没有简单复制 RISC-V 的硬件模型。RISC-V qemu virt 使用 virtio-mmio 块设备，而 LoongArch qemu virt 上 virtio 通常走 PCI 设备模型。为了优先跑通 HAL、文件系统和后续 VFS/ext2 集成，LoongArch 当前没有实现完整 PCI 枚举和 virtio-over-PCI 驱动，而是采用内嵌 RAM disk 方案模拟块设备：

- 构建时将 `fs.img` 通过 `objcopy -I binary` 转为 ELF object。
- 链接时将镜像对象并入内核。
- 启动时 HAL 将只读镜像内容复制到可写内存页。
- 上层 `bio.c` 仍通过 `bread(dev, blockno)` / `bwrite()` 访问块设备。

这使 LoongArch 能保持和 RISC-V 一致的块设备语义。后续队友的 VFS/ext2 可以把第二个设备号作为 ext2 镜像来源，而无需先实现真实 PCI virtio。

## 2. 关键实现描述

### 2.1 编译和平台选择

`Makefile` 中通过 `ARCH ?= riscv` 指定默认架构。选择 LoongArch 时：

- 工具链切换为 `loongarch64-linux-gnu-`。
- QEMU 切换为 `qemu-system-loongarch64`。
- 用户程序使用 `hal/loongarch/user.ld` 链接脚本。
- 平台对象从 `hal/loongarch/` 编译，包括 `hal_entry.o`、`hal_start.o`、`hal_intr.o`、`hal_uart.o`、`hal_virtio.o`、`hal_tlbrefill.o` 等。

LoongArch 内核启动使用 QEMU 的 `-bios kernel/kernel.bin` 方式，而不是 RISC-V 的 `-kernel kernel/kernel`。因此 `make qemu` 会先将 ELF kernel 转成 raw binary。

### 2.2 地址空间和页表

LoongArch 使用自己的地址布局文件 `hal/loongarch/memlayout.h` 和架构定义 `hal/loongarch/arch.h`。实现中保留 xv6 上层对 `MAXVA`、`TRAMPOLINE`、`TRAPFRAME`、`USER_TOP` 等概念的依赖，同时在底层用 LoongArch 的直接映射窗口和页表格式实现。

当前内核通用 `vm.c` 中通过 `PT_LEVELS` 区分页表层数：

- RISC-V Sv39 使用 3 级页表，`PT_LEVELS = 3`。
- LoongArch 当前实现使用 4 级页表，`PT_LEVELS = 4`。

`PTE_V_CACHE` 用于统一“有效且可缓存”的 PTE 标志：

- LoongArch 中为 `PTE_V | PTE_MAT | PTE_P`。
- RISC-V 中退化为 `PTE_V`。

这样可以让 lazy allocation 等通用内存管理代码同时兼容两个架构。

### 2.3 异常、中断和 TLB

LoongArch HAL 提供平台专用的异常入口、trap vector、上下文切换和 TLB refill 代码。通用 trap 处理仍在 `kernel/trap.c` 中完成，但底层入口、寄存器保存、异常返回等架构相关部分放入 `hal/loongarch/`。

开发过程中发现 `vm.c` 中曾包含 LoongArch 专用的 TLB 写入汇编。为了让同一份 `vm.c` 能在 RISC-V 下编译，最终将这部分逻辑用 `ARCH_loongarch` 条件包裹；RISC-V 下该函数为空操作。这样避免了 RISC-V 编译器解析 LoongArch CSR 指令导致的错误。

### 2.4 LoongArch 块设备设计

LoongArch 的 `hal/loongarch/hal_virtio.c` 当前是 RAM-disk 后端。它对上层仍暴露 `virtio_disk_init()`、`virtio_disk_rw()`、`virtio_disk_intr()`，但内部不访问真实 virtio-mmio 或 PCI 设备。

RAM disk 初始化流程：

1. 链接器提供 `_binary_fs_img_start` 和 `_binary_fs_img_end`。
2. `virtio_disk_init()` 调用 `ramdisk_init()`。
3. `ramdisk_init()` 根据镜像大小分配若干页。
4. 将链接进内核的镜像内容复制到可写页中。
5. 后续读写都在内存页上完成。

块读写流程：

1. `bio.c` 设置 `struct buf` 中的 `dev` 和 `blockno`。
2. `virtio_disk_rw()` 根据 `b->dev` 找到对应 `struct ramdisk`。
3. 通过 `blockno * BSIZE` 计算镜像内偏移。
4. 读操作将 RAM disk 内容复制到 `b->data`。
5. 写操作将 `b->data` 复制回 RAM disk。
6. RAM disk 是同步设备，因此完成后直接设置 `b->disk = 0`。

### 2.5 LoongArch 双磁盘支持

为了配合队友后续 VFS/ext2 集成，本次在 LoongArch 上实现了双 RAM-disk 设备。这里没有使用 RISC-V 的 `VIRTIO1 = 0x10002000` 和 `VIRTIO1_IRQ = 2`，因为这些地址和中断号只适用于 RISC-V virtio-mmio，不适用于当前 LoongArch 后端。

LoongArch 双磁盘启用方式：

```sh
make ARCH=loongarch VIRTIO_NDISK=2 qemu
```

设备号约定：

- `dev = 1`：第一块 RAM disk，对应 `fs.img`。
- `dev = 2`：第二块 RAM disk，对应 `fs1.img`。

`Makefile` 在 `ARCH=loongarch VIRTIO_NDISK=2` 时会额外生成并链接：

- `kernel/fs_img.o`
- `kernel/fs1_img.o`

`hal/loongarch/hal_virtio.c` 中新增 `struct ramdisk ramdisk[NRAMDISK]`，其中 `NRAMDISK = VIRTIO_NDISK`。当 `VIRTIO_NDISK=2` 时，`virtio_disk_init()` 会初始化两个 RAM disk。启动时可以看到类似输出：

```text
ramdisk1: 2048000 bytes, 500 pages
ramdisk2: 2048000 bytes, 500 pages
```

这说明第二块块设备已经在 LoongArch HAL 中生效。后续 VFS/ext2 可以使用 `bread(2, blockno)` 访问第二块设备。

## 3. 代码说明

### 3.1 `Makefile`

关键作用：

- 通过 `ARCH` 选择 RISC-V 或 LoongArch。
- 通过 `VIRTIO_NDISK` 选择块设备数量。
- LoongArch 下将 `fs.img` 和可选的 `fs1.img` 转为 ELF object 并链接进内核。

关键变量和规则：

```make
VIRTIO_NDISK ?= 1
CFLAGS += -DVIRTIO_NDISK=$(VIRTIO_NDISK)
LOONGARCH_FS_OBJS = $(if $(filter loongarch,$(ARCH)),$K/fs_img.o $(if $(filter 2,$(VIRTIO_NDISK)),$K/fs1_img.o))
```

当 `VIRTIO_NDISK=2` 时，LoongArch 内核链接命令中会包含 `kernel/fs1_img.o`。

### 3.2 `hal/loongarch/hal_virtio.c`

该文件是 LoongArch 块设备 HAL 的核心。虽然文件名仍为 `hal_virtio.c`，但当前实现是 RAM-disk 替代方案，主要为了保持上层接口兼容。

核心结构：

```c
struct ramdisk {
  uchar *pages[RAMDISK_MAX_PAGES];
  uint npages;
  uint size;
  struct spinlock lock;
};
```

核心函数：

- `ramdisk_init()`：从链接进内核的镜像初始化一个可写 RAM disk。
- `ramdisk_for_dev()`：根据设备号选择 RAM disk。
- `virtio_disk_init()`：初始化一个或两个 RAM disk。
- `virtio_disk_rw()`：执行同步块读写。
- `virtio_disk_intr()`：RAM disk 无异步中断，因此为空。

### 3.3 `hal/loongarch/arch.h`

该文件定义 LoongArch 架构相关常量、寄存器操作、PTE 标志和页表层数。与本次 HAL 兼容性相关的关键定义包括：

```c
#define PTE_V_CACHE  (PTE_V | PTE_MAT | PTE_P)
#define PT_LEVELS    4
```

### 3.4 `kernel/vm.c`

`vm.c` 是通用内存管理代码。为了同时支持 RISC-V 和 LoongArch，需要避免写死页表层数，并抽象 PTE 有效位：

- `walk()` 使用 `PT_LEVELS - 1` 控制页表遍历层数。
- lazy allocation 使用 `PTE_V_CACHE` 创建 PTE。
- LoongArch 专用 TLB 填充逻辑只在 `ARCH_loongarch` 下编译。

### 3.5 与 RISC-V 双磁盘设计的关系

RISC-V 的双磁盘实现使用真实 virtio-mmio：

- `VIRTIO0 = 0x10001000`
- `VIRTIO1 = 0x10002000`
- `VIRTIO1_IRQ = 2`

LoongArch 没有复用这些 MMIO 常量，而是复用了上层块设备语义：

- RISC-V：`dev=2` -> 第二个 virtio-mmio 磁盘。
- LoongArch：`dev=2` -> 第二个 RAM disk。

这保证了后续 VFS/ext2 层可以用统一设备号访问第二块设备，而 HAL 内部根据架构选择不同后端。

## 4. 研发过程遇到的问题和解决方法

### 4.1 RISC-V 编译错误：`PTE_V_CACHE` 未定义

问题现象：

```text
kernel/vm.c:253:32: error: 'PTE_V_CACHE' undeclared
```

原因分析：

`vm.c` 为了兼容 LoongArch 页表属性，使用了 `PTE_V_CACHE`。LoongArch 定义了该宏，但 RISC-V 没有定义，导致 RISC-V 编译失败。

解决方法：

在 RISC-V 架构头文件中加入：

```c
#define PTE_V_CACHE PTE_V
```

这样 RISC-V 仍使用原有有效位语义，LoongArch 则保留可缓存和 present 属性。

### 4.2 RISC-V 启动失败：页表层数写死

问题现象：

RISC-V 能编译但启动过程中卡住或异常。

原因分析：

通用 `walk()` 曾按 LoongArch 的 4 级页表写死遍历层数。RISC-V Sv39 实际是 3 级页表，错误的层数会生成错误页表结构。

解决方法：

引入平台相关宏 `PT_LEVELS`：

```c
for(int level = PT_LEVELS - 1; level > 0; level--)
```

其中 RISC-V 定义为 3，LoongArch 定义为 4。

### 4.3 RISC-V 编译 LoongArch TLB 汇编失败

问题现象：

RISC-V 编译器无法识别 LoongArch 专用 CSR/TLB 指令。

原因分析：

通用 `vm.c` 中包含了 LoongArch 专用汇编，缺少架构条件编译保护。

解决方法：

将 LoongArch TLB 填充代码放入 `#ifdef ARCH_loongarch`，RISC-V 下保留空操作。这样保持通用代码可编译，同时不影响 LoongArch 的 TLB 行为。

### 4.4 RISC-V 运行时 illegal instruction

问题现象：

RISC-V 启动后触发非法指令异常，定位到 S-mode 中访问 M-mode CSR。

原因分析：

`hal_timer_init()` 调用了只能在 M-mode 初始化阶段使用的 `timerinit()`。内核进入 S-mode 后再次访问 M-mode CSR 会触发非法指令。

解决方法：

RISC-V 的 `hal_timer_init()` 改为只设置 S-mode timer compare，不再调用 M-mode 初始化函数。

### 4.5 LoongArch 原本无法验证双磁盘

问题现象：

RISC-V 可以通过 `VIRTIO_NDISK=2` 挂第二块 QEMU virtio-mmio 磁盘，但 LoongArch 的 `make ARCH=loongarch VIRTIO_NDISK=2 qemu` 不会真实接入第二块磁盘。

原因分析：

LoongArch 当前块设备后端不是 virtio-mmio，而是单个内嵌 `fs.img` RAM disk。`VIRTIO1` MMIO 地址不适用于 LoongArch。

解决方法：

在 LoongArch HAL 中实现第二个 RAM disk：

- `fs.img` -> `dev=1`
- `fs1.img` -> `dev=2`

这样后续 VFS/ext2 可以在 LoongArch 上通过 `dev=2` 验证第二块块设备。

### 4.6 LoongArch 编译被旧依赖文件干扰

问题现象：

从 RISC-V 切换到 LoongArch 编译时，Make 可能报旧的 RISC-V 头文件依赖错误。

原因分析：

同一工作区切换架构时，旧 `.d` 依赖文件和 `.o` 文件可能残留。

解决方法：

切换架构前执行：

```sh
make ARCH=loongarch clean
```

或：

```sh
make clean
```

确保重新生成对应架构的依赖和对象文件。

### 4.7 Docker 生成文件权限问题

问题现象：

LoongArch QEMU 生成的 `kernel/kernel.bin` 在宿主上显示为 `nobody:nogroup`，宿主用户无法直接覆盖。

原因分析：

Docker 容器内用户和宿主用户 UID/GID 不一致，生成文件所有权发生变化。

解决方法：

使用容器内命令或重新设置属主恢复文件权限。报告和提交时应避免把 `kernel.bin` 这类构建产物混入源码改动。

## 5. AI 工具使用声明和交互记录

### 5.1 使用声明

本题目允许使用 AI 工具。本项目 HAL 初步实现和文档整理过程中使用了 OpenAI Codex 作为辅助工具。

AI 工具主要用于：

- 阅读和梳理 xv6 HAL 代码结构。
- 分析 RISC-V 与 LoongArch 块设备差异。
- 辅助定位编译和启动错误。
- 生成和修改局部代码补丁。
- 运行 Docker 内编译与 QEMU 测试命令。
- 总结研发过程并生成本设计文档初稿。

AI 工具的成果包括：

- RISC-V 双 virtio-mmio 磁盘 HAL 支持方案。
- LoongArch 双 RAM-disk 设备方案。
- `PTE_V_CACHE`、`PT_LEVELS` 等跨架构兼容性修复。
- LoongArch 双磁盘 `usertests -q` 验证流程。
- 本文档的初步文字整理。

最终代码含义、方案取舍和测试结果由开发者确认。AI 输出不直接代替人工验收，后续还需要与队友的 VFS/ext2 代码整合后继续测试。

### 5.2 交互记录节选

以下为本次任务中与 AI 工具的关键交互摘要，用于说明 AI 使用场景。

交互 1：确认 HAL 是否支持多磁盘

用户提问：

```text
请问当前HAL实现了多设备支持吗？尤其是多磁盘。
我的队友对我的HAL层进行了修改 (hal/riscv/memlayout.h):
新增 VIRTIO1 (0x10002000) 和 VIRTIO1_IRQ (2) 定义，用于第二块 virtio 磁盘。
如果实现了，请兼容队友的设计；如果目前没实现，可以参考队友的设计。
```

AI 处理：

- 检查 `hal/riscv/hal_virtio.c`、`bio.c`、`buf.h` 等代码。
- 判断原实现只有单个全局 virtio disk，尚未真正支持多磁盘。
- 提出并实现 `VIRTIO_NDISK` 方案，使 RISC-V 可选支持两个 virtio-mmio 设备。

交互 2：修复 RISC-V 编译问题

用户反馈：

```text
make qemu 编译。遇到问题：
kernel/vm.c:253:32: error: 'PTE_V_CACHE' undeclared
```

AI 处理：

- 分析 `PTE_V_CACHE` 是 LoongArch 页表兼容宏，但 RISC-V 未定义。
- 在 RISC-V 架构头文件中定义 `PTE_V_CACHE PTE_V`。
- 继续发现并修复页表层数、TLB 汇编条件编译、S-mode timer 初始化等问题。
- 验证 RISC-V `usertests -q` 通过。

交互 3：讨论 LoongArch 是否需要双磁盘

用户提问：

```text
我的队友实现了 ext2 文件系统，通过 VFS 层挂载...
目前双磁盘验证目前只在 RISC-V 上做，请问是否需要让 loongarch 的 hal 也支持双磁盘。
```

AI 处理：

- 分析 LoongArch qemu virt 当前不是 virtio-mmio，而是 RAM-disk 替代实现。
- 建议为了 qemu-loongarch 上验证 VFS/ext2，应支持第二块块设备。
- 建议采用第二 RAM disk，而不是复用 RISC-V 的 `VIRTIO1` MMIO 地址。

交互 4：实现 LoongArch 双 RAM-disk

用户指令：

```text
要支持 LoongArch 双磁盘，但用 RAM-disk 第二设备实现，不用 VIRTIO1 MMIO 地址。优先跑通测试
```

AI 处理：

- 修改 `Makefile`，在 `ARCH=loongarch VIRTIO_NDISK=2` 时链接 `fs1_img.o`。
- 修改 `hal/loongarch/hal_virtio.c`，将单 RAM disk 扩展为 `ramdisk[NRAMDISK]`。
- 约定 `dev=1` 对应 `fs.img`，`dev=2` 对应 `fs1.img`。
- 运行 LoongArch 双磁盘 QEMU 验证。

验证输出节选：

```text
ramdisk1: 2048000 bytes, 500 pages
ramdisk2: 2048000 bytes, 500 pages
init: starting usertests -q
...
ALL TESTS PASSED
```

## 6. 当前验证方法

### 6.1 LoongArch 单磁盘验证

```sh
xv6-la
cd /xv6/xv6-riscv-xv6-riscv-rev5
make ARCH=loongarch clean
make ARCH=loongarch qemu
```

期望输出：

```text
ALL TESTS PASSED
```

### 6.2 LoongArch 双磁盘验证

```sh
xv6-la
cd /xv6/xv6-riscv-xv6-riscv-rev5
make ARCH=loongarch clean
make ARCH=loongarch VIRTIO_NDISK=2 qemu
```

期望输出：

```text
ramdisk1: ...
ramdisk2: ...
ALL TESTS PASSED
```

### 6.3 RISC-V 双磁盘验证

```sh
sudo docker exec -it xv6 bash
cd /xv6/xv6-riscv-xv6-riscv-rev5
make clean
make VIRTIO_NDISK=2 qemu
```

期望输出：

```text
ALL TESTS PASSED
```

## 7. 后续与 VFS/ext2 集成计划

当前 HAL 已经为上层提供了两个块设备的基础能力。后续与队友 VFS/ext2 集成时，建议按以下顺序验证：

1. 保持 xv6 原文件系统仍挂在 `dev=1`。
2. 将 ext2 镜像放入第二设备，即 RISC-V 的第二 virtio 磁盘或 LoongArch 的 `fs1.img`。
3. VFS mount 时指定 `dev=2`。
4. 先验证只读 ext2 superblock、inode、目录读取。
5. 再验证写入路径、缓存一致性和错误处理。
6. 最后在 RISC-V 和 LoongArch 两个平台分别跑集成测试。

当前 LoongArch 的第二设备是内存盘，写入不会持久化回宿主 `fs1.img`。这对于功能测试足够，但如果后续需要持久化 ext2 修改，则需要实现镜像回写机制或真实 PCI virtio-blk。

