# xv6虚拟文件系统和硬件抽象层的设计与实现

> 赛题：2026年全国大学生计算机系统能力大赛-操作系统设计赛(全国)-OS功能挑战赛道
> 团队：tjuer
> 成员：陈权，杨子游
> 日期：2026年_6_月__30__日

---

## 第一章  项目概述

### 1.1 项目背景

xv6-riscv是MIT开发的教学操作系统，运行于RISC-V平台，被广泛用于高校操作系统课程。其标准第5版存在两个核心局限：

- **单一文件系统**：仅支持自带的xv6文件系统（类Unix V6），无法与ext2、FAT32等主流文件系统交互
- **单一CPU架构**：内核代码与RISC-V紧耦合，移植到其他架构需大量重写

这使得xv6在教学中难以展示"Linux是如何同时支持ext4/xfs/btrfs的"以及"操作系统如何跨平台运行"等关键概念。

本项目为xv6-riscv第5版扩充**虚拟文件系统（VFS）** 和**硬件抽象层（HAL）**，使其以最精简的代码同时支持三种文件系统和两种CPU架构，打造一个设计精巧的小型教学操作系统。

### 1.2 项目目标

| 编号 | 目标                | 说明                                                   |
| ---- | ------------------- | ------------------------------------------------------ |
| G1   | 同时支持3种文件系统 | xv6fs（原生）、ext2（Linux早期标准）、FAT32（U盘通用） |
| G2   | 同时支持2种CPU架构  | RISC-V（已验证）、LoongArch（国产自主架构）            |
| G3   | 通过所有测例        | xv6原生usertests + 赛方测试用例 + 自定义综合测试       |
| G4   | 代码尽量少          | 对比原xv6-rev5，核心新增控制在~2000行以内              |
| G5   | 运行稳定            | 无panic、无内存泄漏、并发安全                          |

### 1.3 文档结构导览

| 章节   | 内容                      | 读者对象                                   |
| ------ | ------------------------- | ------------------------------------------ |
| 第一章 | 项目概述（本文）          | 所有读者                                   |
| 第二章 | VFS设计详解 [★重点★]    | 想理解虚拟文件系统核心算法的读者           |
| 第三章 | ext2适配详解 [★重点★]   | 想理解具体文件系统如何接入VFS的读者        |
| 第四章 | xv6fs胶水层               | 想理解"最小侵入"策略的读者                 |
| 第五章 | HAL硬件抽象层             | 想理解跨平台抽象设计的读者                 |
| 第六章 | FAT32适配                 | 想理解FAT32实现要点的读者                  |
| 第七章 | 测试与验证 [★评审必看★] | 评审老师、想复现结果的读者                 |
| 第八章 | 设计精巧度分析            | 评审老师（评分要点"设计精巧度"的直接支撑） |
| 第九章 | 挑战与展望                | 所有读者                                   |

### 1.4 总体架构

```
用户态应用程序（sh, cat, ls, ext2test*, ...）
    │ 系统调用 (sys_open, sys_read, sys_write, sys_mount, ...)
    ▼
┌─────────────────────────────────────────────┐
│          VFS 虚拟文件系统层                  │
│  vfs.c/h  (~500行)                          │
│                                             │
│  · vnode 统一文件抽象                        │
│  · vnode_ops 操作接口 (13个函数指针)          │
│  · mount 挂载管理 (mounttable[NMOUNT])        │
│  · namei 路径遍历 + 挂载点自动穿越             │
│  · 引用计数生命周期管理                        │
├────────────┬────────────┬────────────────────┤
│  xv6fs胶水  │  ext2实现  │  FAT32实现         │
│ xv6fs.c   │ ext2.c/h  │  fat32.c/h          │
│  ~320行    │ ~1000行   │  (待实现)           │
│            │            │                    │
│ 包装原生   │ 完整ext2   │ FAT32文件系统      │
│ inode为    │ 文件系统    │ 读写支持           │
│ vnode      │ 读写支持    │                    │
├────────────┴────────────┴────────────────────┤
│            HAL 硬件抽象层                      │
│  hal/    (~______行)                          │
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

**架构核心思想**：VFS层通过统一vnode抽象隔离上层系统调用与具体文件系统；HAL层通过统一接口头文件隔离内核核心逻辑与CPU架构。两层抽象互为正交——添加新文件系统不需要懂CPU，移植新架构不需要懂文件系统。

### 1.5 团队分工

| 成员         | 负责模块         | 具体内容                                                    |
| ------------ | ---------------- | ----------------------------------------------------------- |
| （你的姓名） | VFS + 多文件系统 | VFS核心设计、xv6fs胶水层、ext2完整实现、FAT32实现、综合测试 |
| （队友姓名） | HAL + 多架构     | HAL接口设计、RISC-V平台实现、LoongArch平台移植              |

### 1.6 功能模块一览

| 功能模块      | 说明                                                     |
| ------------- | -------------------------------------------------------- |
| VFS核心层     | mount/umount、namei路径遍历、挂载点穿越、".."跨FS返回    |
| xv6fs胶水层   | 原生inode零修改包装为vnode                               |
| ext2文件系统  | 完整vnode_ops（13个函数），支持文件/目录/硬链接/设备节点 |
| FAT32文件系统 | （待实现）                                               |
| RISC-V HAL    | 启动、页表、中断/定时器、串口、virtio磁盘驱动            |
| LoongArch HAL | （待实现）                                               |

### 1.7 代码量统计

| 类别               | 文件                                              | 行数                          |
| ------------------ | ------------------------------------------------- | ----------------------------- |
| VFS核心            | kernel/vfs.c + kernel/vfs.h                       | ~600行                        |
| ext2实现           | kernel/ext2.c + kernel/ext2.h                     | ~1000行                       |
| xv6fs胶水          | kernel/xv6fs.c                                    | ~320行                        |
| FAT32              | kernel/fat32.c + kernel/fat32.h                   | （待实现）                    |
| HAL层              | hal/                                              | ~（待统计）行                 |
| 用户测试           | user/ext2test1.c + ext2test2.c + ext2test3.c      | ~700行（含赛方500+自定义200） |
| 内核其他修改       | kernel/sysfile.c, kernel/exec.c, kernel/file.c 等 | ~（待统计）行                 |
| **合计新增** |                                                   | **~（待统计）行**       |
| 原xv6-rev5基线     |                                                   | ~10000行                      |
| **增幅**     |                                                   | **~（待统计）%**        |

---

## 第二章  VFS虚拟文件系统设计 [★重点章节★]

### 2.1 设计动机与目标

#### 2.1.1 原xv6的局限

xv6-riscv-rev5 的文件系统代码（`kernel/fs.c`、`kernel/log.c`）存在三个固有限制：

1. **硬编码文件系统类型**：所有文件操作直接操作 `struct inode`，inode 管理（`ialloc`、`iget`、`iput`）、目录逻辑（`dirlookup`、`dirlink`）、块映射（`bmap`）全部假设底层是 xv6 原生 FS 格式。要支持 ext2，必须重写整个文件操作栈。
2. **无挂载点概念**：只有一个根文件系统，无法像 Linux 那样在 `/mnt` 上挂载第二块磁盘。根 inode 从超级块直接读取，不存在 "不同设备上不同文件系统" 的抽象。
3. **路径遍历与 FS 实现耦合**：`namei` / `nameiparent` 内联调用 `dirlookup`（xv6fs 特有），无法遍历 ext2 目录项。

#### 2.1.2 VFS 的设计目标

| 目标                 | 具体含义                                                                                  |
| -------------------- | ----------------------------------------------------------------------------------------- |
| **统一接口**   | 所有文件操作通过`vnode_ops` 虚拟表调用，系统调用不感知底层 FS 类型                      |
| **多 FS 支持** | 可同时挂载 xv6fs（root）和 ext2（`/mnt`），路径遍历自动在挂载点间穿越                   |
| **最小侵入**   | `kernel/fs.c` 和 `kernel/log.c` 一行不改，xv6fs 作为独立胶水层包装原 inode            |
| **代码精简**   | `vfs.c` + `vfs.h` 合计约 650 行，符合教学操作系统定位                                 |
| **依赖倒转**   | VFS 层不依赖具体 FS 实现——FS 通过`vfs_register` 注册自身，VFS 只认 `vnode_ops` 接口 |

#### 2.1.3 设计原则

- **不随复杂度膨胀**：宁可一个 `vnode` 统一抽象，不要 dentry / inode / address_space 多层解体
- **引用计数驱动**：所有 vnode 生命周期由 `ref` 精确管理，`vput` 触发延迟销毁
- **错误路径可恢复**：mount 失败时完整回滚，不泄漏内存或锁

### 2.2 整体架构

VFS 层嵌于系统调用与具体文件系统之间，对上提供 14 个 `vfs_*` API（全部在 `kernel/vfs.h` 中声明），对下通过两套虚拟表 `vnode_ops`（文件级）和 `vfs_ops`（挂载级）驱动具体实现。横向通过全局 `mounttable[NMOUNT]` 管理最多 8 个挂载点，`namei` 路径遍历在挂载点间自动穿越。

```
sysfile.c: sys_open / sys_read / sys_write / sys_mount / sys_umount / ...
    │         ↓ 全部调用 vfs_* 函数，不再直接操作 inode
    ▼
┌─────────────────────────────────────────────────┐
│         VFS 层  (~650 行, vfs.c + vfs.h)          │
│                                                 │
│  · vfs_namei      路径遍历 + 挂载点穿越           │
│  · vfs_mount       挂载管理 + 错误恢复            │
│  · vfs_umount      卸载 + 回写超块                │
│  · alloc_vnode / vget / vput  引用计数生命周期     │
│  · vfs_open/read/write/...   14 个薄包装 API      │
│  · vfs_register    文件系统类型注册表              │
├─────────────────────────────────────────────────┤
│  vnode_ops (13 个函数指针)                       │
│  vfs_ops  (2 个函数指针: root + unmount)         │
│  fstypes[]  注册表 (mountfn + 名称)              │
│  mounttable[NMOUNT]  全局挂载表                   │
└─────────────────────────────────────────────────┘
         │                    │
    ┌────▼────┐         ┌─────▼──────┐
    │ xv6fs   │         │   ext2     │
    │xv6fs.c  │         │  ext2.c    │
    │  ~320行  │         │  ~1000行    │
    │ 包装inode│         │ 完整实现    │
    └─────────┘         └────────────┘
```

关键交互路径：

- **上对 sysfile.c**：所有系统调用通过 `vfs_open` / `vfs_read` / `vfs_write` / `vfs_close` / `vfs_mount` / `vfs_umount` 等薄包装进入 VFS
- **下对具体 FS**：VFS 不持有 FS 实现的任何头文件引用——仅通过 `fstypes[]` 注册表在 mount 时调用 `mountfn(dev)`，之后通过 `vnode_ops` 回调
- **横向挂载管理**：`namei` 中每步 `lookup` 后调用 `cross_mount` 检查是否为挂载点，自动穿越到下级文件系统

### 2.3 核心数据结构

#### 2.3.1 vnode — 统一文件抽象

```c
struct vnode {
  uint type;          // V_FILE(1), V_DIR(2), V_DEV(3)
  uint dev;           // 所属设备的设备号，从 mp->dev 继承
  int major;          // V_DEV 的主设备号
  int minor;          // V_DEV 的次设备号
  struct vnode_ops *ops; // 操作虚拟表，由所属 FS 在创建时填充
  struct mount *mp;   // 所属挂载点；ref==0 时置 NULL
  int ref;            // 引用计数：vget 递增，vput 递减到 0 触发销毁
  struct sleeplock lock; // 保护 vnode 读写操作的睡眠锁
  uint inum;          // FS 内部 inode 号，仅在所属 mp 内有意义
  uint size;          // 缓存的文件大小，由 FS 后端维护（O_APPEND 等）
  void *priv;         // FS 私有数据指针（如 xv6fs 的 struct inode*）
};
```

**字段详解**：

| 字段     | 作用                                                           | 生命周期                                                        |
| -------- | -------------------------------------------------------------- | --------------------------------------------------------------- |
| `type` | 区分文件/目录/设备节点                                         | alloc_vnode 置 0，FS 的 create/lookup 设置为 V_FILE/V_DIR/V_DEV |
| `dev`  | 用于 mountpoint 匹配和 I/O 路由                                | 从`mp->dev` 继承，vnode 销毁时不变                            |
| `inum` | 配合 dev 在系统内唯一标识一个文件                              | FS 的 create/lookup 填入                                        |
| `ref`  | 防止正在使用的 vnode 被回收                                    | `alloc_vnode` 置 1，`vget` / `vput` 原子操作              |
| `ops`  | 解耦 VFS 与具体 FS：VFS 只调 ops，不关心实现                   | FS 的 create/lookup 填充，`vput` 销毁前置 NULL                |
| `mp`   | 反向指针，用于`vfs_find_mountpoint` 和 `vfs_link` 跨FS检查 | 由`vfs_mount` 或 FS create 设置                               |
| `priv` | 避免 vnode 膨胀——FS 自行分配私有结构挂载于此                 | FS create/lookup 分配，`ops->destroy` 释放                    |
| `lock` | 串行化对同一 vnode 的并发读写（如 file_read/file_write）       | 初始化时 initsleeplock，销毁时隐式释放                          |

#### 2.3.2 vnode_ops — 文件操作接口（14 个函数指针）

vnode_ops 是 VFS 设计的核心抽象，每个具体文件系统实现这 14 个函数，VFS 通过 `VOP_*` 宏间接调用。

| 序号 | 函数指针        | 签名                                   | 功能                                                           |
| ---- | --------------- | -------------------------------------- | -------------------------------------------------------------- |
| 1    | `lookup`      | `int(vnode*, char*, vnode**)`        | 在目录中查找 name，返回目标 vnode（ref 已持有）。失败返回非0   |
| 2    | `mknod`       | `int(vnode*, char*, int, int)`       | 在目录中创建设备节点（major, minor）                           |
| 3    | `read`        | `int(vnode*, uint64, int, uint)`     | 从 off 偏移读 n 字节到用户地址 buf；返回实际读取字节数         |
| 4    | `write`       | `int(vnode*, uint64, int, uint)`     | 将用户地址 buf 的 n 字节写入 off 偏移；返回实际写入字节数      |
| 5    | `stat`        | `int(vnode*, uint64)`                | 填充`struct stat` 到用户地址 addr                            |
| 6    | `readdir`     | `int(vnode*, uint64, uint)`          | 从 off 偏移读一个目录项到用户地址 buf；返回下一偏移            |
| 7    | `create`      | `int(vnode*, char*, short, vnode**)` | 在 dir 中创建 type 类型的 inode；失败返回非0                   |
| 8    | `unlink`      | `int(vnode*, char*)`                 | 从 dir 移除 name；若 nlink==0 则释放 inode（可能延迟到 vput）  |
| 9    | `mkdir`       | `int(vnode*, char*)`                 | 在 dir 中创建子目录（含 "." 和 ".."）                          |
| 10   | `link`        | `int(vnode*, char*, vnode*)`         | 将 old vnode 硬链接到 dir 下名为 name                          |
| 11   | `read_kernel` | `int(vnode*, uint64, int, uint)`     | 内核态读——直接写入内核地址 buf，避免 copyout 开销（exec 用） |
| 12   | `truncate`    | `int(vnode*)`                        | 释放所有数据块，设置 size 为 0；仅 V_FILE                      |
| 13   | `destroy`     | `void(void*)`                        | 释放`vnode->priv` 指向的 FS 私有数据，在 vput 的锁外延迟调用 |

VFS 层通过 `VOP_*` 宏简化调用：

```c
#define VOP_LOOKUP(v,n,r)    (v)->ops->lookup(v,n,r)
#define VOP_READ(v,a,n,o)    (v)->ops->read(v,a,n,o)
#define VOP_WRITE(v,a,n,o)   (v)->ops->write(v,a,n,o)
// ... 共 13 个宏
```

**接口设计原则**：

- `read` / `write` 使用 `uint64` 作为用户地址（兼容 64 位指针，内核态通过 `copyin` / `copyout` 访问）
- `read_kernel` 以直接内存访问替代 `readi`，消除 exec 加载 ELF 时的 double-copy 开销
- 所有创建操作（`create` / `mkdir` / `mknod`）在持有 `vn_lock` 下调用，保证目录完整性

#### 2.3.3 vfs_ops — 挂载级操作接口

```c
struct vfs_ops {
  struct vnode* (*root)(struct mount*);   // 返回本 FS 的根 vnode（ref 已持有）
  void (*unmount)(struct mount*);          // 回写超块 + 释放 FS 私有挂载数据
};
```

vfs_ops 只定义了两个函数指针，比 vnode_ops 简单得多。`root` 在 mount 完成时调用一次，`unmount` 在 umount 时调用。

#### 2.3.4 mount 与 mounttable

```c
struct mount {
  int valid;                // 1 表示已成功注册到 mounttable（原子状态标记）
  struct vfs_ops *ops;      // root() + unmount()
  struct vnode *root;       // 本 FS 的根 vnode（ref 永久持有）
  struct vnode *mountpoint; // 挂载点 vnode （父 FS 的目录 vnode）；根挂载为 NULL
  uint dev;                 // 本挂载涉及的所有 I/O 的目标设备号
  char path[128];           // 挂载路径字符串（调试用；实际遍历用 mountpoint 指针）
  void *priv;               // FS 私有挂载数据（如 ext2 的超块缓冲区）
};
```

挂载表是全局数组：

```c
struct mount *mounttable[NMOUNT];  // NMOUNT=8
int nmount;                        // 当前活跃挂载数
```

**示例布局**（同时挂载 xv6fs 和 ext2）：

```
mounttable[0] = { valid=1, dev=0, root=vA, mountpoint=NULL, path="/" }     // xv6fs 根挂载
mounttable[1] = { valid=1, dev=1, root=vB, mountpoint=vC, path="/mnt" }    // ext2 挂载在 /mnt
```

其中 vA 是 xv6fs 根 inode 的 vnode，vB 是 ext2 根 inode 的 vnode，vC 是 xv6fs 中 `/mnt` 目录的 vnode。

### 2.4 路径遍历算法 [★核心算法★]

#### 2.4.1 vfs_namei 完整流程

`vfs_namei` 是 VFS 的"心脏"——它将路径字符串解析为目标 vnode，并在每次遍历目录项后自动穿越挂载点。函数约 45 行，逻辑如下：

```
vfs_namei(path):
  1. path_start(path, &rest)          // '/' → vget(mounttable[0]->root)
                                      // 否则 → vget(p->cwd)
  2. while (rest = skipelem(rest, name)) != 0:
       vn_lock(vp)
       if vp->type != V_DIR → 失败，返回 NULL
     
       // ".." 跨 FS 特殊处理（见 2.4.3）
       if name==".." && vp 是其 FS 的 root:
           mpoint = vget(vp->mp->mountpoint)
           vn_unlock(vp); vput(vp)
           vn_lock(mpoint); mpoint->ops->lookup(mpoint, "..", &next)
           vn_unlock(mpoint); vput(mpoint)
           vp = next
           continue
     
       // 正常 lookup
       vp->ops->lookup(vp, name, &next)  // FS 后端查找目录项，返回 vnode
       vn_unlock(vp); vput(vp)
     
       vp = cross_mount(next)           // 检查下一跳是否为挂载点，若是则穿越
  3. return vp                           // ref 已持有，调用方负责 vput
```

**关键细节**：

- `path_start` 返回的 vnode 已持有 ref（通过 `vget`），确保遍历期间不被回收
- 每步 `lookup` 返回的 vnode 的 ref 由 FS 后端在 lookup 内部持有（对应 `alloc_vnode` 语义）
- 逐级 `vput(vp)` 释放前一级 vnode，保证引用计数精确平衡
- `cross_mount` 消费传入的 ref，返回新 ref，无需额外引用计数操作

#### 2.4.2 挂载点穿越（cross_mount）

```c
static struct vnode*
cross_mount(struct vnode *vp)
{
  struct mount *submp;
  submp = vfs_lookup_mount(vp);
  if(submp){
    vput(vp);
    return vget(submp->root);   // 返回挂载在 vp 上的 FS 的根 vnode
  }
  return vp;                    // 非挂载点，原样返回
}
```

`vfs_lookup_mount` 遍历 mounttable[1..nmount-1]（跳过根挂载），用 **dev+inum** 匹配：

```c
if(mounttable[i]->mountpoint &&
   mounttable[i]->mountpoint->dev == vp->dev &&
   mounttable[i]->mountpoint->inum == vp->inum)
  return mounttable[i];
```

**为什么用 dev+inum 而非指针比较？**

不同 FS 对同一个 inode 调用 `vget`（或 xv6fs 的 `iget`）可能返回**不同地址的 vnode 对象**（vnode_table 中的不同槽位），但 `dev` 和 `inum` 在系统内是全局唯一的（dev 标识设备，inum 是 FS 内 inode 号）。用 dev+inum 匹配保证了正确性，避免了因缓存复用导致的指针失效问题。

#### 2.4.3 ".." 跨文件系统处理 [★创新点★]

**问题场景**：ext2 挂载在 xv6fs 的 `/mnt` 上。当用户在 ext2 内执行 `cd ..` 时，应该从 ext2 回到 xv6fs 的 `/mnt` 目录，而非 ext2 自己的根目录。

**实现逻辑**（嵌入在 namei 的循环中）：

```c
if(strncmp(name, "..", 15) == 0 &&
   vp->mp && vp->mp->mountpoint &&
   vp->dev == vp->mp->root->dev &&
   vp->inum == vp->mp->root->inum)
{
  // vp 就是其所属挂载的 root vnode
  // 应该穿越回父文件系统的挂载点
  struct vnode *mpoint = vget(vp->mp->mountpoint);
  vn_unlock(vp);
  vput(vp);
  vn_lock(mpoint);
  mpoint->ops->lookup(mpoint, "..", &next);
  vn_unlock(mpoint);
  vput(mpoint);
  // next 现在是父 FS 中 mountpoint 的父目录
  // (父 FS 眼中的 "..")
  vp = next;
  continue;
}
```

**图解**：

```
xv6fs:
    / ──→ /mnt (vnode C)  ── ".." ──→ / (vnode A)
                │
ext2:           │ 挂载于此
    /ext2root (vnode B) ── ".." ──→ ?
  
处理流程:
  cd .. 在 ext2 根目录执行
  → 检测 vB.dev==mp->root->dev && vB.inum==mp->root->inum
  → 是 root → 穿越到 mp->mountpoint (vC, xv6fs 的 /mnt)
  → 在 xv6fs 中 lookup(mpoint, "..") → 返回 / (vA)
  → 最终结果: vA (xv6fs 根目录) ✓
```

**与 Linux 的对比**：Linux 通过 `dentry` 的 `d_parent` 指针和 `follow_up` 实现相同语义。本 vnode 实现比 Linux 简化——不维护双向父子链接，仅在 `".."` 查询时动态穿越。

#### 2.4.4 vfs_nameiparent

`vfs_nameiparent` 是 `vfs_namei` 的变体，用于 `O_CREATE` 路径：

```
vfs_nameiparent("/mnt/newfile", name):
  → 逐级遍历到 /mnt 的 vnode
  → 保存最终分量 "newfile" 到 name
  → 返回 /mnt 的 vnode（ref 持有）
```

调用方拿到父目录 vnode 后，在其上调用 `VOP_CREATE(dir, name, ...)` 创建新文件。这避免了先 `namei` 再解出父目录的双重遍历。

### 2.5 文件系统注册与挂载

#### 2.5.1 vfs_register — 注册文件系统类型

VFS 维护一个静态注册表：

```c
struct {
  struct mount* (*mountfn)(uint);   // mount 入口函数：接收 dev，返回 mount 结构
  char name[16];                    // FS 类型名称（"xv6fs", "ext2"）
} fstypes[8];
int nfstype;   // 已注册数量，上限 8
```

注册接口极其简洁：

```c
void vfs_register(struct mount* (*fn)(uint), char *name)
{
  if(nfstype >= 8) panic("vfs_register: too many fstypes");
  fstypes[nfstype].mountfn = fn;
  safestrcpy(fstypes[nfstype].name, name, 16);
  nfstype++;
}
```

`vfs_init` 在启动时调用两次注册：

```c
vfs_register(xv6fs_mount, "xv6fs");
vfs_register(ext2_mount, "ext2");
```

添加新 FS（如 FAT32）只需一行 `vfs_register(fat32_mount, "fat32")`，无需修改任何 VFS 内部逻辑。

#### 2.5.2 vfs_mount — 挂载完整流程

`vfs_mount` 实现 7 个步骤，每步失败均有对应回滚：

```
vfs_mount(path, dev, fstype):
  1. 重复挂载检查
     for i in 0..nmount-1:
       if mounttable[i]->path == path →  return 0  (同名路径已挂载)

  2. 查找 FS 类型
     for i in 0..nfstype-1:
       if fstypes[i].name == fstype:
         mp = fstypes[i].mountfn(dev)  // 调用 xv6fs_mount / ext2_mount
     if mp == 0 → return 0

  3. 解析挂载点 vnode
     if path == "/":
       mp->mountpoint = NULL
     else:
       mpoint = vfs_namei(path)
       if mpoint==NULL || mpoint->type!=V_DIR:
         → 回滚: vput(mpoint); mp->ops->unmount(mp); kfree(mp); return 0

  4. 重复挂载检查（同一 vnode）
     for i in 1..nmount-1:  // 跳过 mounttable[0]
       if mounttable[i]->mountpoint->dev==mpoint->dev &&
          mounttable[i]->mountpoint->inum==mpoint->inum:
         → 回滚: vput(mpoint); mp->ops->unmount(mp); kfree(mp); return 0

  5. 填充 mount 结构
     mp->mountpoint = mpoint
     safestrcpy(mp->path, path, 128)
     mp->dev = dev

  6. 容量检查
     if nmount >= NMOUNT:
       → 回滚: vput(mpoint); mp->ops->unmount(mp); kfree(mp); return 0

  7. 注册到 mounttable
     mounttable[nmount] = mp
     nmount++
     mp->valid = 1
     return mp
```

**错误恢复路径的关键**：

- 步骤 3~6 的失败回滚都执行 `mp->ops->unmount(mp)` + `kfree(mp)`——因为 `mountfn` 已在步骤 2 中分配了 mount 结构和 FS 私有数据
- `vput(mpoint)` 释放步骤 3 获取的挂载点 vnode
- 不回滚 `fstypes[]`——FS 类型注册是全局的，一次注册永久有效

#### 2.5.3 vfs_umount — 卸载流程

```c
int vfs_umount(char *path)
{
  // 1. 在 mounttable 中按 path 字符串查找
  for(idx=0; idx<nmount; idx++)
    if(strncmp(mounttable[idx]->path, path, 128)==0)
      mp = mounttable[idx];

  // 2. 安全检查
  if(mp==0 || !mp->valid) return -1;
  if(idx == 0) return -1;   // 根挂载不可卸载

  // 3. 调用 FS 的 unmount 回调（回写超块、释放私有数据）
  if(mp->ops && mp->ops->unmount)
    mp->ops->unmount(mp);

  mp->valid = 0;

  // 4. mounttable 前移压缩（O(n) 删除，保留顺序）
  for(i=idx; i<nmount-1; i++)
    mounttable[i] = mounttable[i+1];
  mounttable[nmount-1] = 0;
  nmount--;

  // 5. 释放挂载点 vnode
  if(mp->mountpoint) vput(mp->mountpoint);

  // 6. 释放 mount 结构体
  kfree((void*)mp);
  return 0;
}
```

**注意事项**：

- 查找用 `path` 字符串而非 `dev`——两个设备可能有相同路径名称冲突
- 卸载不以 root vnode 的 ref 计数为条件——xv6 中假定调用 umount 时没有打开此 FS 上的文件（POSIX umount 的 EBUSY 检查省略）
- mounttable 删除通过数组前移实现，O(n) 但 NMOUNT=8 时微不足道

### 2.6 vnode 生命周期管理

#### 2.6.1 vnode 存储

vnode 使用**静态数组 + 线性扫描**分配，而非 Linux 的 slab 缓存：

```c
struct vnode vnode_table[NVNODE];  // NVNODE=200, 约 200×80B ≈ 16KB
struct spinlock vnode_lock;        // 保护 ref 字段和分配状态
```

`alloc_vnode` 遍历 vnode_table 查找 ref==0 的空闲槽位，置 ref=1 并清零其他字段：

```c
struct vnode* alloc_vnode(void)
{
  acquire(&vnode_lock);
  for(i = 0; i < NVNODE; i++){
    if(vnode_table[i].ref == 0){
      vnode_table[i].ref = 1;
      vnode_table[i].type = 0;
      vnode_table[i].ops = 0;
      vnode_table[i].mp = 0;
      vnode_table[i].priv = 0;
      vnode_table[i].inum = 0;
      vnode_table[i].major = 0;
      vnode_table[i].minor = 0;
      release(&vnode_lock);
      return &vnode_table[i];
    }
  }
  release(&vnode_lock);
  return 0;  // 返回 NULL 表示 vnode 池耗尽
}
```

#### 2.6.2 引用计数状态机

```
               alloc_vnode
    [free] ─────────────────→ [ref=1, 活跃]
                                  │
                        vget      │      vput
                        ref++     │      ref--
                                  │
                                  ▼
                              ref==0?
                              /     \
                            否       是
                            │         │
                            ▼         ▼
                         保持活跃   [延迟销毁]
                                    │
                                    ▼
                              锁外调用 destroy(priv)
                              槽位标记 ref=0 [free]
```

- **vget**：`acquire(&vnode_lock); vp->ref++; release(&vnode_lock);` — 原子递增，调用者确保 vp 当前合法
- **vput**：`acquire(&vnode_lock); vp->ref--; if(vp->ref==0) need_free=1; release(&vnode_lock);` — ref 降到 0 时不立即销毁，而是在**释放自旋锁之后**异步调用 destroy

#### 2.6.3 延迟销毁机制 [★关键设计★]

```c
void vput(struct vnode *vp)
{
  void *priv;
  void (*destroy)(void*);
  int need_free = 0;

  acquire(&vnode_lock);
  vp->ref--;
  if(vp->ref == 0){
    need_free = 1;
    priv = vp->priv;                    // 快照私有数据
    destroy = (vp->ops) ? vp->ops->destroy : 0;  // 快照 destroy 函数
    vp->priv = 0;                       // 清空槽位字段
    vp->ops = 0;
    vp->mp = 0;
    vp->type = 0;
  }
  release(&vnode_lock);                 // ★ 先释放锁 ★

  if(need_free && destroy)
    destroy(priv);                      // ★ 再调用 destroy ★
}
```

**为什么不能在线内调用 destroy？**

`destroy(priv)` 最终会调用具体 FS 的清理函数（如 xv6fs 的 `iput` → `iunlockput`），后者可能涉及块分配/释放、日志写入、缓冲区操作——这些操作内部会获取其他锁。如果 `vput` 持有 `vnode_lock` 自旋锁时调用 destroy，会导致：

1. **死锁**：destroy → begin_op → log.lock，同时另一线程持有 log.lock 等待 vnode_lock
2. **自旋锁下睡眠**：块 I/O 可能触发 sleep，而在自旋锁下睡眠是违规的

延迟销毁通过在锁外调用 destroy 完美解决了这两个问题。这也是本设计中引用计数管理的核心诀窍。

### 2.7 跨文件系统操作限制

VFS 对跨挂载点的操作实施限制，保证 FS 内部数据结构的完整性。

#### 2.7.1 硬链接限制（vfs_link）

```c
int vfs_link(struct vnode *old, struct vnode *newdir, char *name)
{
  if(old->mp != newdir->mp) return -1;  // ★ 跨 FS 硬链接拒绝 ★
  // ...
  return VOP_LINK(newdir, name, old);
}
```

硬链接的本质是同一个 FS 内部的 inode 别名——两个目录项指向同一个 inode 号。跨 FS 的硬链接无意义（ext2 的 inode 对 xv6fs 不可见）。Linux 对此返回 EXDEV 错误。

#### 2.7.2 rename 的简化策略

xv6 VFS 不实现 `vfs_rename`——采用 `link` + `unlink` 的简化语义。这一方面是因为 xv6 原生的 `sys_rename` 不存在，另一方面 `rename` 的跨 FS 原子迁移非常复杂（涉及源 FS 的 orphan 和目的 FS 的数据拷贝），对教学操作系统而言代价过高。

#### 2.7.3 其他跨 FS 操作

| 操作                         | 跨 FS 行为                   | 理由                           |
| ---------------------------- | ---------------------------- | ------------------------------ |
| `vfs_read` / `vfs_write` | 允许（自动跟随 vnode->ops）  | 只操作已打开的文件             |
| `vfs_open`                 | 允许（namei 自动穿越挂载点） | 路径遍历透明                   |
| `vfs_create`               | 不允许（必须在同一 dir 内）  | create 在单个 dir vnode 上操作 |
| `vfs_unlink`               | 不允许（删除在 dir 内完成）  | 同上                           |

### 2.8 设计决策记录

| 决策                          | 原因                                                                                        |
| ----------------------------- | ------------------------------------------------------------------------------------------- |
| cwd 用 vnode* 而非路径字符串  | POSIX语义：目录 rename 后 cwd 仍指向同一 vnode，路径字符串会失效                            |
| priv 用 void* 而非联合体      | 解耦 VFS 与具体 FS——添加新 FS 不动 vnode 结构                                             |
| NVNODE=200                    | 原 xv6 NINODE=50；vnode 需更多：open file (NFILE=100) + cwd + namei 中间节点 + 跨 FS 挂载点 |
| mountpoint 用 dev+inum 匹配   | 不同 iget 可能返回不同地址的 vnode 对象；dev+inum 在系统内全局唯一                          |
| vput 延迟销毁（锁外 destroy） | 避免 destroy 中的块分配/日志操作导致死锁或自旋锁下睡眠                                      |
| 线性扫描 vnode 缓存（非哈希） | 代码精简；NVNODE=200 时 O(N) 扫描开销可接受（~0.2μs）                                      |
| namei 不使用 dentry 缓存      | 避免缓存失效的复杂性；路径遍历频率在 xv6 中很低                                             |
| mounttable 固定数组           | NMOUNT=8 足够（根 + 7 个额外 FS）；动态链表增加复杂而无实际收益                             |
| umount 不检查 busy            | xv6 中调用 umount 前应确保无打开文件（简化实现，未来可加 EBUSY 检查）                       |

### 2.9 涉及文件与代码量

| 文件                 | 行数   | 角色                                                              |
| -------------------- | ------ | ----------------------------------------------------------------- |
| `kernel/vfs.h`     | ______ | vnode / vnode_ops / mount 类型定义, VOP_* 宏, API 声明            |
| `kernel/vfs.c`     | ______ | namei 路径遍历, mount/umount, vnode 生命周期, 14个薄包装          |
| `kernel/file.h`    | ______ | `struct file` 中 `f->ip` 改为 `struct vnode*`               |
| `kernel/file.c`    | ______ | file_read/file_write 调用 vfs_read/vfs_write；fileclose 调用 vput |
| `kernel/sysfile.c` | ______ | 所有 sys_* 调用 VFS API（sys_open, sys_mount, sys_umount 等）     |
| `kernel/proc.h`    | ______ | `cwd` 字段改为 `struct vnode*`                                |
| `kernel/exec.c`    | ______ | exec 调用 vfs_namei + vfs_read_kernel 加载 ELF                    |
| `kernel/fcntl.h`   | ______ | O_CREATE / O_APPEND / O_TRUNC 等标志位定义                        |

**核心文件**: `vfs.h` + `vfs.c`（~650行，包含全部VFS架构和逻辑）
**关联文件**: 5个上层调用方（`sysfile.c`, `file.c`, `file.h`, `exec.c`, `proc.h`, `fcntl.h`）

---

## 第三章  ext2文件系统适配 [★重点章节★]

### 3.1 ext2简介

ext2（Second Extended Filesystem）是Linux早期标准的文件系统，由Rémy Card于1993年设计。它采用经典的Unix文件系统布局——超级块、块组描述符、inode位图、块位图、inode表和 data blocks——结构清晰、文档丰富，非常适合作为教学操作系统的"第二文件系统"范例。

本实现以**约1000行C代码**完整实现了ext2的基础读写能力（含文件/目录/设备节点的创建、读写、删除、硬链接和时间戳维护），并通过vnode_ops接口无缝接入VFS层，使得上层应用程序可以用与xv6fs完全相同的系统调用操作ext2文件系统。

### 3.2 适配策略

| 策略                        | 说明                                                                                    |
| --------------------------- | --------------------------------------------------------------------------------------- |
| **仅支持1024字节块**  | `s_log_block_size == 0`，与xv6 BSIZE对齐，直接复用buf-cache层（`bread`/`bwrite`） |
| **单块组**            | 仅实现block group 0，镜像上限约10MB，满足所有测试场景                                   |
| **合并镜像布局**      | ext2分区紧接xv6 fs.img之后，块地址偏移`FSSIZE`：`EBLK(mp, b) = b + mp->fs_offset`   |
| **128字节基础inode**  | 支持rev=0的128字节inode，同时通过`s_inode_size`动态检测256字节inode                   |
| **直接块 + 单间接块** | `i_block[0..11]` + `i_block[12]`单向间接，最大文件268KB                             |
| **不支持**            | 扩展属性、ACL、日志、extent格式、双重/三重间接块、HTREE索引目录                         |

**合并镜像布局图**：

```
┌─────────────────────────────────────────────────────────────┐
│                     磁盘镜像 (fs.img + ext2)                   │
├──────────────────────────┬──────────────────────────────────┤
│    xv6 文件系统区域       │       ext2 文件系统区域            │
│    (块 0 ~ FSSIZE-1)     │    (块 FSSIZE ~ FSSIZE+10239)    │
│    约 1000 块 / 1MB      │    约 10240 块 / 10MB              │
├──────────────────────────┼──────────────────────────────────┤
│ boot | super | log | ... │ SB | BG desc | ibitmap | bbitmap │
│                          │ 块1   块2      块3       块4      │
│                          │ inode_table | data blocks ...     │
└──────────────────────────┴──────────────────────────────────┘
                                ↑
                          FSSIZE = 1000 (偏移量)
```

### 3.3 数据流设计

用户态 `read()` 到ext2磁盘操作的完整链路：

```
用户程序: read(fd, buf, 100)
  │
  ▼
sys_read() [sysfile.c]
  → fileread(f, addr, n) [file.c]
    → vn_lock(vp)                        // 获取vnode睡眠锁，串行化并发I/O
    → VOP_READ(vp, addr, n, off)         // 宏展开 → vp->ops->read(vp, addr, n, off)
      → ext2_read(vp, buf, n, off) [ext2.c]
        ├─ 检查 off >= i_size → 返回0（EOF）
        ├─ 调整 n 不超过文件末尾
        ├─ while(total < n):
        │   ├─ ext2_bmap(priv, off/BSIZE, 0)  // 逻辑块号→物理块号（不分配，alloc=0）
        │   │   ├─ lbn < 12 → 直接块，读取 ino->i_block[lbn]
        │   │   └─ lbn 12..267 → 读间接块 → 查表 → 返回物理块号
        │   ├─ blk == 0 → 稀疏空洞，填充零到用户buf
        │   ├─ bread(mp->dev, EBLK(mp, blk))  // 通过buf-cache读取磁盘块
        │   └─ copyout(p->pagetable, buf+total, bp->data+boff, len)
        └─ ext2_update_time(priv, 1, 0)  // 更新atime
    → vn_unlock(vp)
  → 返回实际读取字节数
```

**关键设计点**：

- `ext2_bmap` 两次出镜：read时 `alloc=0`（只读不分配），write时 `alloc=1`（按需分配）
- `bread`/`bwrite` 复用xv6的块缓存，无需实现独立的磁盘I/O层
- `EBLK` 宏自动加上 `fs_offset`（FSSIZE），实现合并镜像的透明偏移
- vnode锁在 `fileread`/`filewrite` 中获取，保证ext2层内的操作是串行化的

### 3.4 核心数据结构

#### 3.4.1 ext2_mount_priv — 挂载级私有数据

每次 `ext2_mount(dev)` 在堆上分配一个 `ext2_mount_priv` 结构体，挂在 `mount->priv` 上：

```c
struct ext2_mount_priv {
  struct ext2_superblock sb;        // 内存中的超块副本（从块1读取）
  struct ext2_bgdesc     bg;        // 缓存唯一的块组描述符（从块2读取）
  uint                   dev;       // 设备号，传给 bread()/bwrite()
  uint                   inode_size; // 磁盘inode大小（128或256字节）
  uint                   fs_offset;  // 合并镜像偏移量 = FSSIZE
  struct sleeplock       lock;       // 串行化 balloc/ialloc 位图操作
  struct mount          *mnt;        // 反向指针，vnode → mp → mnt
};
```

**各字段职责**：

| 字段           | 来源                                  | 用途                                                                         |
| -------------- | ------------------------------------- | ---------------------------------------------------------------------------- |
| `sb`         | `bread(dev, 1+FSSIZE)`              | 全局用：`s_free_blocks_count`、`s_free_inodes_count`、`s_blocks_count` |
| `bg`         | `bread(dev, 2+FSSIZE)`              | 定位：`bg_inode_table`、`bg_block_bitmap`、`bg_inode_bitmap`           |
| `inode_size` | `sb.s_inode_size`（若为0则默认128） | `ino_block`/`ino_offset` 自适应计算                                      |
| `fs_offset`  | 常量`FSSIZE`                        | 所有`bread`/`bwrite` 的块号偏移                                          |
| `lock`       | 初始化睡眠锁                          | 保证`bitmap_alloc`/`bitmap_free` 的原子性                                |
| `mnt`        | `ext2_mount` 填充                   | vnode通过`priv->mp->mnt` 回找挂载结构                                      |

#### 3.4.2 ext2_vnode_priv — vnode级私有数据

每个ext2 vnode附着一个 `ext2_vnode_priv`，挂载在 `vnode->priv` 上：

```c
struct ext2_vnode_priv {
  struct ext2_inode_disk ino;       // 磁盘inode的内存缓存
  struct ext2_mount_priv *mp;       // 所属挂载的私有数据（用于bread/balloc等）
  uint  inum;                       // 本inode的inode号
  int   dirty;                      // 1 = ino已修改，需要在destroy时回写
  int   orphaned;                   // 1 = unlink已将nlink降为0，destroy时真正释放
};
```

**dirty标记的维护规则**：

- `ext2_update_time` 更新 atime/mtime/ctime 时置1
- `ext2_bmap` 分配新块（间接块或数据块）时置1
- `ext2_i_truncate` 清空所有块指针和 `i_blocks` / `i_size` 后置1
- `ext2_create` 中 `links_count` 变化时置1
- ext2_write 里 `i_size` 扩展时置1
- `ext2_destroy` 中若非 orphaned 且 dirty，则 `write_inode` 回写磁盘

**orphaned标记**（见3.7.5节详解）：unlink将 `links_count` 降为0后置1，`ext2_destroy` 检测到此标记时执行块截断和inode释放。

### 3.5 块分配与回收（balloc/bfree/ialloc/ifree）

#### 3.5.1 位图管理：bitmap_alloc / bitmap_free

块位图和inode位图均为一个1024字节块，通过以下两个通用函数管理：

```c
// 在位图块 bb 中分配bit [1..nbits-1]（bit 0保留）
static int bitmap_alloc(mp, bb, nbits)
  1. bread(mp->dev, EBLK(mp, bb))
  2. acquiresleep(&mp->lock)                // 串行化并发分配
  3. for i in 1 .. min(nbits, BSIZE*8):
       if data[i/8] 的第(i%8)位 == 0:
         data[i/8] |= (1 << (i%8))          // 置位
         bwrite(bp)
         releasesleep(&mp->lock)
         return i
  4. releasesleep(&mp->lock); brelse(bp); return -1  // 位图满

// 在位图块 bb 中释放bit n
static void bitmap_free(mp, bb, n)
  1. bread → acquiresleep → 清零目标位 → bwrite → releasesleep
```

**为什么要用睡眠锁？** 位图更新可能涉及 `bwrite`（触发磁盘I/O），而xv6的自旋锁不允许在持有期间睡眠。使用 `sleeplock` 既能保证互斥，又允许 `bwrite` 内部的可能等待。

#### 3.5.2 块分配流程（ext2_balloc）

```
ext2_balloc(mp):
  1. b = bitmap_alloc(mp, bg.bg_block_bitmap, sb.s_blocks_count)
  2. acquiresleep(&mp->lock)
  3. mp->sb.s_free_blocks_count--          // 超块空闲块计数 -1
  4. ext2_sb_write(mp)                     // 回写超块（bread → memmove → bwrite）
  5. releasesleep(&mp->lock)
  6. return b
```

#### 3.5.3 块释放流程（ext2_bfree）

```
ext2_bfree(mp, blockno):
  1. bitmap_free(mp, bg.bg_block_bitmap, blockno)
  2. acquiresleep → sb.s_free_blocks_count++ → ext2_sb_write → releasesleep
```

#### 3.5.4 inode分配（ext2_ialloc）

```
ext2_ialloc(mp, mode):
  1. inum = bitmap_alloc(mp, bg.bg_inode_bitmap, sb.s_inodes_count)
  2. bread 该 inode 所在的块
  3. memset(ino, 0, sizeof(ext2_inode_disk))   // 清零所有字段
  4. ino->i_mode = mode                        // 0x4000(目录) / 0x8000(文件) / 0x2000(设备)
  5. ino->i_atime = ino->i_mtime = ino->i_ctime = ticks
  6. bwrite + brelse
  7. acquiresleep → sb.s_free_inodes_count-- → ext2_sb_write → releasesleep
  8. return inum
```

**失败回滚**：步骤2 bread失败 → 回滚 `bitmap_free(inode_bitmap, inum)`

#### 3.5.5 inode释放（ext2_ifree）

```
ext2_ifree(mp, inum):
  1. bitmap_free(mp, bg.bg_inode_bitmap, inum)
  2. acquiresleep → sb.s_free_inodes_count++ → ext2_sb_write → releasesleep
```

### 3.6 bmap：逻辑块→物理块映射

bmap是ext2所有I/O操作的必经之路，它将文件相对于块偏移量（`lbn`）转换为磁盘物理块号。

```c
static uint ext2_bmap(struct ext2_vnode_priv *priv, uint lbn, int alloc)
```

| 参数      | 含义                                                                |
| --------- | ------------------------------------------------------------------- |
| `priv`  | vnode私有数据，含inode缓存                                          |
| `lbn`   | 文件内逻辑块号（0 = 第0块）                                         |
| `alloc` | 0 = 只读查找（read、stat、lookup用）；1 = 缺失时自动分配（write用） |

**寻址层次**：

```
直接块区 (lbn 0..11):
  ino->i_block[lbn] → 直接返回物理块号
  若==0且alloc==1 → ext2_balloc → 写入i_block[lbn]
                     i_blocks += 2 (一个1024字节块 = 两个512字节扇区)

单间接块区 (lbn 12..267):
  1. 若 i_block[12]==0 且 alloc==1:
     a. ext2_balloc 分配间接块本身
     b. bread + memset(0, BSIZE) 清零整个间接块
     c. i_blocks += 2
  2. bread(indirect_block) 读取间接块
  3. indirect[lbn-12] → 目标数据块的物理块号
  4. 若==0且alloc==1 → ext2_balloc → 写入indirect[lbn-12]
                        i_blocks += 2
  5. 返回物理块号

双重/三重间接(lbn 268+):
  return 0 (不支持)
```

| 范围             | 块数 | 容量  |
| ---------------- | ---- | ----- |
| 直接块(0-11)     | 12   | 12KB  |
| 单间接块(12-267) | 256  | 256KB |
| **合计**   | 268  | 268KB |

**bmap失败回滚**：

- 分配间接块后 `bread` 失败 → `ext2_bfree(间接块)` → `i_block[12]=0` → `i_blocks -= 2`
- 分配数据块后调用方 `bread` 失败 → bmap本身返回值非0，调用方检测

### 3.7 目录操作详解 [★最复杂模块★]

#### 3.7.1 目录项格式

ext2目录是一个由变长 `ext2_dir_entry` 组成的线性列表，散布在目录的数据块中：

```c
struct ext2_dir_entry {
  uint   inode;       // inode号（0 = 已删除的空位）
  ushort rec_len;     // 本条目总长度（含padding），必须被4整除
  uchar  name_len;    // 真正名字长度（不含NUL）
  uchar  file_type;   // EXT2_FT_* 类型提示（避免额外读inode）
  char   name[];      // 变长名字（长度 = name_len），对齐到rec_len
};
```

**关键规则**：

- `rec_len` 包含名字后的空余字节（padding），不允许条目跨块
- 第一个块的第一个条目以完整的 `rec_len = BSIZE` 开始
- 一个块末尾处若剩余空间不够最小条目（8字节），前一条目的 `rec_len` 会延伸到块尾
- `file_type` 为快速类型判断提供提示：`EXT2_FT_REG=1`、`EXT2_FT_DIR=2`、`EXT2_FT_CHRDEV=3`

#### 3.7.2 ext2_dir_lookup 查找

```c
static int ext2_dir_lookup(struct vnode *dir, char *name, struct vnode **result)
```

逐块扫描目录内容，对每个活跃条目（`inode != 0`）比较名字：

```
while(off < i_size):
  blk = ext2_bmap(dp, off/BSIZE, 0)      // 不分配
  bread(blk) → 逐条目:
    if name匹配:
      inum = de->inode
      *result = ext2_iget(mp, inum)      // 读inode → 创建vnode（持有ref）
      return 0
    off += de->rec_len
return -1                                // 未找到
```

**性能特点**：O(N)线性扫描——对于教学操作系统的目录规模（通常＜100个条目）足够快。

#### 3.7.3 ext2_dir_add 插入 [★核心算法★]

```c
static int ext2_dir_add(struct vnode *dir, char *name, uint inum, uchar ftype)
```

**需求长度计算**：`needlen = (sizeof(de) + strlen(name) + 3) & ~3`（4字节对齐）

**三种插入策略**（按优先级）：

```
情况1: 复用已删除条目
  ┌────────┬──────────────────────────────────┐
  │ de(v0) │  (空闲空间, reclen >= needlen)     │
  │inode=0 │                                  │
  └────────┴──────────────────────────────────┘
  处理: 直接填充 inode、name_len、file_type、name → bwrite
  效果: 原条目的 rec_len 不变

情况2: 拆分条目padding
  ┌─────────────────┬─────────────────────────┐
  │  de (活跃)       │  (padding, ≥ needlen)    │
  │  real_len        │  reclen - real_len      │
  └─────────────────┴─────────────────────────┘
  处理:
    1. 原条目 rec_len 缩小为 real_len
    2. 在 real_len 偏移处创建新条目
    3. 新条目 rec_len = 原reclen - real_len
    4. 填充新条目 → bwrite
  效果: 原活跃条目不变，其padding变为新活跃条目

情况3: 扩展目录（新建一个块）
  在目录末尾 expand one data block:
  ext2_bmap(dp, off/BSIZE, 1) → new block
  memset(bp->data, 0, BSIZE)
  de = first entry; de->rec_len = BSIZE  (单个条目占满整块)
  填充 inode / name → bwrite
  ino->i_size = off + BSIZE               (扩展目录大小)
```

**为什么"情况2"必须存在？** ext2的 `rec_len` 可以大于实际占用空间。当一条目的名字简短（如 `a.c` 占16字节但 `rec_len=32`），多出的16字节成为padding，可以被新条目回收利用。这种"拆分"策略是ext2目录空间管理的精髓。

#### 3.7.4 ext2_create 创建文件

```c
static int ext2_create(struct vnode *dir, char *name, short type, struct vnode **new)
```

**完整流程**：

```
1. ext2_update_time(dp, 0, 1)           // 目录mtime/ctime

2. ext2_dir_lookup(dir, name, &exist)   // 不可重名
   if 找到 → vput(exist); return -1

3. inum = ext2_ialloc(mp, mode)         // 分配inode，mode = 0x8000(文件)或0x4000(目录)
   if 失败 → return -1

4. read_inode → ino.i_links_count = 1 → write_inode

5. ext2_dir_add(dir, name, inum, ftype) // 在父目录插入条目
   if 失败 → ext2_ifree → return -1

6. 若 type == V_DIR（目录）:
   a. newdir = ext2_iget(mp, inum)    // 获取新目录vnode
   b. ext2_dir_add(newdir, ".", inum, FT_DIR)    // 自引用
   c. ext2_dir_add(newdir, "..", dp->inum, FT_DIR) // 父目录引用
   d. ndp->ino.i_links_count = 2       // "."和".."各计1个链接
   e. ndp->dirty = 1; vput(newdir)
   f. dp->ino.i_links_count++          // 父目录因".."获得一个额外链接

7. if new != NULL:
     *new = ext2_iget(mp, inum)         // 返回新vnode（ref持有）
```

**失败回滚**：步骤6中 "." 或 ".." 创建失败 → `ext2_unlink(dir, name)` 回滚整个创建（删除目录条目 + 释放inode）

#### 3.7.5 ext2_unlink 删除与orphaned延迟释放 [★创新点★]

```c
static int ext2_unlink(struct vnode *dir, char *name)
```

**流程**：

```
1. ext2_update_time(dp, 0, 1)           // 目录mtime/ctime

2. 扫描目录找到目标条目 → 获取 inum

3. read_inode → 判断 is_dir = (i_mode & 0xF000) == 0x4000

4. 若 is_dir:
   a. ext2_iget → 扫描目录内容，确认只有 "." 和 ".."
   b. 若非空 → return -1 (不能删除非空目录)
   c. dp->ino.i_links_count--         // 父目录失去".."的链接

5. de->inode = 0 → bwrite              // 标记目录条目为空

6. ino.i_links_count--
   若 links_count == 0:
     // ★ L4.2 orphaned延迟释放 ★
     victim = ext2_iget(mp, inum)
     vp->orphaned = 1                  // 标记为孤立
     write_inode(mp, inum, &ino)       // 回写更新后的links_count
     vput(victim)                      // 此时 ref >= 1（至少这里和可能的打开fd）
     // → 若ref降为0，destroy()中检测到 orphaned，执行真正释放
   否则:
     ino.i_ctime = ticks; write_inode  // 仅更新时间戳
```

**orphaned延迟释放的设计思想**：

```
场景: 文件被进程A打开，同时进程B unlink它

  时间轴:
  T1: open(fd, "/mnt/f")  → vnode ref=2 (fd表 + namei临时)
  T2: namei 退出          → vnode ref=1 (仅fd表)
  T3: unlink("/mnt/f")    → links_count降为0, orphaned=1
                           → 此时不能立即释放块，因为fd表还持有vnode引用
  T4: close(fd)           → vput → ref=0
                           → destroy() 检测到 orphaned==1
                           → ext2_i_truncate(priv)  // 释放所有数据块
                           → ext2_ifree(mp, inum)   // 释放inode
```

**关键优势**：

- 安全的并发语义：open的文件在close前始终可读写，即使已被unlink
- 避免悬垂指针：`links_count` 降为0但vnode存在期间，`inum` 仍在inode位图中标记为已用
- 与Linux语义一致：符合POSIX "删除最后目录项时释放空间"的要求

### 3.8 时间戳管理

ext2维护三个POSIX时间戳（以 `ticks` 为单位，xv6中1 tick ≈ 10ms）：

```c
static void ext2_update_time(struct ext2_vnode_priv *priv, int access, int mod)
```

| 参数                | 更新字段                              | 调用场合                        |
| ------------------- | ------------------------------------- | ------------------------------- |
| `access=1, mod=0` | `i_atime`                           | read、lookup、read_kernel       |
| `access=0, mod=1` | `i_mtime`, `i_ctime`              | write、create、unlink、truncate |
| `access=1, mod=1` | `i_atime`, `i_mtime`, `i_ctime` | （未使用）                      |

调用方分布在10个vnode_ops函数中：

- `ext2_read` / `ext2_read_kernel` → `ext2_update_time(priv, 1, 0)`（访问时间）
- `ext2_write` → `ext2_update_time(priv, 0, 1)`（修改时间）
- `ext2_lookup` → `ext2_update_time(dp, 1, 0)`（目录atime）
- `ext2_create` / `ext2_unlink` / `ext2_link` / `ext2_mknod` → `ext2_update_time(dp, 0, 1)`（目录mtime）

### 3.9 设备节点支持

设备节点的 `major`/`minor` 号编码存储在 `i_block[0]` 中：

```c
// 写入（ext2_mknod）:
ino->i_block[0] = (major << 8) | (minor & 0xFF);

// 读出（ext2_vnode_alloc）:
if(vp->type == V_DEV){
  vp->major = (ino->i_block[0] >> 8) & 0xFF;
  vp->minor = ino->i_block[0] & 0xFF;
}
```

设备节点类型为 `EXT2_INODE_CHRDEV`（字符设备，`i_mode` 高4位 = 0x2000），`i_links_count = 1`，无数据块。创建时通过 `ext2_ialloc(mp, 0x2000)` 分配inode。

### 3.10 限制与兼容性

| 限制类别                | 具体限制                          | 原因                                          |
| ----------------------- | --------------------------------- | --------------------------------------------- |
| **寻址**          | 最大文件 268KB（直接+单间接）     | 未实现双重/三重间接块                         |
| **文件系统大小**  | ≤10MB（1个块组）                 | 仅实现单块组，`s_blocks_per_group` 上限8192 |
| **块大小**        | 仅1024字节                        | 与xv6 BSIZE对齐，不支持2048/4096字节块        |
| **inode大小**     | 128或256字节                      | 动态检测`s_inode_size`，但仅操作前128字节   |
| **目录索引**      | 线性扫描 O(N)                     | 未实现HTREE索引目录                           |
| **日志**          | 无ext3/ext4日志                   | ext2本身无日志，崩溃恢复由上层负责            |
| **扩展属性**      | 不支持                            | `i_file_acl` 忽略                           |
| **ACL**           | 不支持                            | `i_dir_acl` 忽略                            |
| **符号链接**      | 以普通文件处理                    | `EXT2_INODE_SYMLNK` 映射为 `V_FILE`       |
| **其他inode类型** | FIFO、SOCK、BLKDEV 以普通文件处理 | 测试套件未覆盖                                |

### 3.11 代码量统计

| 文件              | 行数               | 说明                                                                             |
| ----------------- | ------------------ | -------------------------------------------------------------------------------- |
| `kernel/ext2.h` | ~150 行            | 磁盘结构定义（superblock、bgdesc、inode_disk、dir_entry）+ 常量                  |
| `kernel/ext2.c` | ~1050 行           | 全部逻辑实现（mount/umount + 13个vnode_ops + bmap + 分配器 + 目录操作 + 时间戳） |
| **合计**    | **~1200 行** |                                                                                  |

各模块代码量分解：

| 模块                | 函数                                                                                                                   | 行数 |
| ------------------- | ---------------------------------------------------------------------------------------------------------------------- | ---- |
| mount/umount        | `ext2_mount`, `ext2_unmount`, `ext2_root`                                                                        | ~80  |
| vnode管理           | `ext2_vnode_alloc`, `ext2_destroy`, `ext2_iget`                                                                  | ~80  |
| inode I/O           | `ino_block`, `ino_offset`, `read_inode`, `write_inode`                                                         | ~40  |
| 位图分配器          | `bitmap_alloc`, `bitmap_free`                                                                                      | ~30  |
| 块/inode分配回收    | `ext2_balloc`, `ext2_bfree`, `ext2_ialloc`, `ext2_ifree`                                                       | ~70  |
| bmap                | `ext2_bmap`                                                                                                          | ~60  |
| 目录操作            | `ext2_dir_lookup`, `ext2_dir_add`, `ext2_create`, `ext2_unlink`, `ext2_mkdir`, `ext2_link`, `ext2_mknod` | ~280 |
| 读写                | `ext2_read`, `ext2_write`, `ext2_read_kernel`                                                                    | ~90  |
| truncate            | `ext2_i_truncate`, `ext2_truncate`                                                                                 | ~35  |
| stat/readdir/时间戳 | `ext2_stat`, `ext2_readdir`, `ext2_update_time`                                                                  | ~70  |
| vtable              | `ext2_vnops`, `ext2_vfsops`                                                                                        | ~30  |

---

## 第四章  xv6fs胶水层

### 4.1 设计原则：最小侵入

xv6fs胶水层的核心哲学是**零修改包装**——将xv6原生的 `struct inode` 及其操作函数（`fs.c` 中的 `iget`/`iput`/`readi`/`writei`/`dirlookup`/`dirlink`/`ialloc`/`itrunc`/`stati`）包装为VFS标准接口 `vnode_ops`，而不修改原生文件系统的任何一行代码。

| 原则                    | 具体体现                                                                                  |
| ----------------------- | ----------------------------------------------------------------------------------------- |
| **fs.c 一行不改** | 原生文件系统代码（`kernel/fs.c`、`kernel/log.c`、`kernel/fs.h`）完全不动            |
| **薄适配层**      | xv6fs.c仅约320行，每个ops函数平均约25行                                                   |
| **日志语义保持**  | `begin_op`/`end_op`包裹所有修改操作，与原始sysfile.c行为一致                          |
| **引用计数透传**  | xv6的`iget`/`iput` 引用计数语义不变，vnode ref通过 `xv6fs_vnode_priv` 间接持有inode |
| **不加新锁**      | 继续使用xv6的`ilock`/`iunlock`，vnode锁由VFS层在进入ops之前获取                       |

### 4.2 inode→vnode 映射

#### 4.2.1 私有数据结构

```c
struct xv6fs_vnode_priv {
  struct inode *ip;  // 指向xv6原生inode（已持有引用）
};
```

与ext2的 `ext2_vnode_priv` 相比极其简洁——因为xv6的原生inode已经包含所有元数据（type、major、minor、inum、size），无需缓存磁盘inode。

#### 4.2.2 vnode工厂函数

```c
static struct vnode* xv6fs_vnode_from_inode(struct inode *ip, uint dev, struct mount *mp)
```

**字段映射表**：

| vnode字段 | 来源                         | 说明                                                          |
| --------- | ---------------------------- | ------------------------------------------------------------- |
| `type`  | `ip->type`                 | `T_DIR → V_DIR`, `T_DEVICE → V_DEV`, 其他 → `V_FILE` |
| `major` | `ip->major`                | 设备主设备号                                                  |
| `minor` | `ip->minor`                | 设备次设备号                                                  |
| `dev`   | 参数`dev`                  | 从上级vnode或挂载结构继承                                     |
| `mp`    | 参数`mp`                   | 所属挂载结构                                                  |
| `inum`  | `ip->inum`                 | xv6 inode号（1 = ROOTINO）                                    |
| `size`  | `ip->size`                 | 文件大小缓存                                                  |
| `priv`  | 新分配的`xv6fs_vnode_priv` | 持有`ip` 的引用                                             |
| `ops`   | `&xv6fs_vnops`             | 指向xv6fs的vnode_ops虚表                                      |

**调用前必须持有 `ilock(ip)`**：因为 `ip->type`、`ip->major`、`ip->minor`、`ip->size` 的读取需要锁保护。工厂函数在读取完这些字段后调用方释放锁。

#### 4.2.3 引用计数协议

```
          ┌─ iget(dev,inum) → ref(ip)=1
          │  ilock(ip)
          │  xv6fs_vnode_from_inode(ip,dev,mp) → vnode ref=1, priv->ip=ip
          │  iunlock(ip)
          │
          │  ... vnode 活跃期间 ...
          │  ip 的引用计数通过 priv->ip 间接持有
          │
          ▼
        vput(vp) → ref(vp)==0 → xv6fs_destroy(priv):
          ├─ begin_op()
          ├─ iput(priv->ip)   → ref(ip)--, 若0则释放inode+trunc块
          ├─ end_op()
          └─ kfree(priv)
```

### 4.3 各ops实现要点

| ops                   | 实现策略                                                                                           | 行数 | 关键细节                                                         |
| --------------------- | -------------------------------------------------------------------------------------------------- | ---- | ---------------------------------------------------------------- |
| **lookup**      | `ilock(dp) → dirlookup(dp, name, 0) → iunlock(dp) → xv6fs_vnode_from_inode`                   | ~12  | dirlookup返回的inode已持有引用，工厂函数消费                     |
| **read**        | `ilock(ip) → readi(ip, 1, buf, off, n) → iunlock(ip)`                                          | ~8   | `1` = 用户空间地址（readi内部调用copyout）                     |
| **write**       | 大写入拆分多个`begin_op/ilock/writei/iunlock/end_op` 迭代                                        | ~25  | 单次writei受`(MAXOPBLOCKS-1-1-2)/2*BSIZE` 日志配额限制，需循环 |
| **stat**        | `ilock → stati → iunlock → copyout`                                                           | ~8   | stati是xv6的串行化stat填充函数                                   |
| **readdir**     | `ilock → readi(ip, 0, &de, off, 14) → iunlock → 转换为vdirent → copyout`                     | ~25  | 跳过`inum==0` 的已删除条目；返回下一偏移                       |
| **create**      | `begin_op/ilock/dirlookup检查重名/ialloc/dirlink/建"."/".."/iunlock/end_op`                      | ~60  | 完整回滚逻辑：若"."或".."创建失败则清零父目录条目                |
| **unlink**      | `begin_op/ilock/dirlookup/isdirempty检查/ip->nlink--/iupdate/iunlockput/清零条目/iunlock/end_op` | ~45  | 目录删除要求`isdirempty` 通过；`iunlockput` 同时释放锁和引用 |
| **mkdir**       | 委托`xv6fs_create(dir, name, V_DIR, &new)`                                                       | ~5   | 创建后立即`vput(new)`（VFS层持有自己的ref）                    |
| **link**        | `begin_op/ilock(old)/ip->nlink++/iupdate/iunlock/ilock(dp)/dirlink/iunlock/end_op`               | ~25  | 失败时回滚`ip->nlink--`                                        |
| **mknod**       | `begin_op/ilock/dirlookup检查重名/ialloc(T_DEVICE)/设major+minor/iupdate/dirlink/iunlock/end_op` | ~30  | -                                                                |
| **read_kernel** | `ilock → readi(ip, 0, buf, off, n) → iunlock`                                                  | ~8   | `0` = 内核空间（直接memmove），exec加载ELF用                   |
| **truncate**    | `begin_op/ilock/itrunc/iunlock/end_op`                                                           | ~8   | itrunc释放所有数据块，`vp->size = 0`                           |
| **destroy**     | `begin_op/iput(ip)/end_op/kfree(priv)`                                                           | ~10  | iput含日志包裹，可能触发的itrunc/ifree也在日志内                 |

### 4.4 代码量

| 文件               | 行数    | 说明             |
| ------------------ | ------- | ---------------- |
| `kernel/xv6fs.c` | ~320 行 | 全部胶水逻辑实现 |

| 模块                  | 行数 |
| --------------------- | ---- |
| 数据结构与vnode工厂   | ~40  |
| mount/root            | ~35  |
| destroy（释放回调）   | ~12  |
| vnode_ops（13个函数） | ~210 |
| vtable（虚表填充）    | ~20  |

> 注：xv6fs.c 不需要独立的 `.h` 文件——其接口就是 VFS 标准接口 `vnode_ops + vfs_ops`，所有类型定义已在 `vfs.h` 中声明。

---

## 第四章  xv6fs胶水层

### 4.1 设计原则：最小侵入

<!-- TODO: 说明不改动fs.c/log.c原有逻辑 -->

### 4.2 inode→vnode 映射

<!-- TODO: xv6fs_vnode_from_inode 字段映射表 -->

### 4.3 各ops实现要点

| ops        | 实现策略                           |
| ---------- | ---------------------------------- |
| lookup     | 调用原有dirlookup                  |
| read/write | 调用readi/writei + begin_op/end_op |
| create     | ialloc + dirlink + 包装为vnode     |
| write      | 小批量拆分（MAXOPBLOCKS限制）      |

### 4.4 代码量

| 文件    | 行数   |
| ------- | ------ |
| xv6fs.c | ______ |

---

## 第五章  HAL硬件抽象层 [待队友填充]

### 5.1 HAL设计目标

### 5.2 接口规范

| 头文件          | 抽象内容          |
| --------------- | ----------------- |
| hal_arch.h      | CSR寄存器/CPU操作 |
| hal_vm.h        | 页表操作          |
| hal_intr.h      | 中断控制器        |
| hal_timer.h     | 定时器            |
| hal_memlayout.h | 内存布局          |
| hal_console.h   | 串口              |
| hal_ctx.h       | 上下文切换        |

### 5.3 RISC-V实现

<!-- TODO: 队友填充 -->

### 5.4 LoongArch移植

<!-- TODO: 队友填充 -->

### 5.5 代码量统计

<!-- TODO -->

---

## 第六章  FAT32文件系统适配 [待实现]

### 6.1 FAT32简介

### 6.2 适配策略

### 6.3 关键实现要点

- FAT链遍历
- 簇分配
- 目录项管理（8.3短名 + LFN长名）
- 文件大小维护

### 6.4 限制

### 6.5 代码量统计

---

## 第七章  测试与验证 [★评审必看★]

### 7.1 测试环境

| 项目             | 版本/配置 |
| ---------------- | --------- |
| QEMU (RISC-V)    | ________  |
| QEMU (LoongArch) | ________  |
| 编译器           | ________  |
| 操作系统         | ________  |

### 7.2 xv6原生测例

| 测试      | RISC-V | LoongArch |
| --------- | ------ | --------- |
| usertests | ⏸     | ⏸        |
| forktest  | ⏸     | ⏸        |
| grind     | ⏸     | ⏸        |

### 7.3 赛方测试用例

| 测试      | 来源 | 结果 |
| --------- | ---- | ---- |
| ext2test1 | 赛方 | ⏸   |
| ext2test2 | 赛方 | ⏸   |

### 7.4 自定义综合测试（ext2test3）

| 层次 | 测试项                      | 结果 |
| ---- | --------------------------- | ---- |
| L1.1 | mount→umount→remount 循环 | ⏸   |
| L1.2 | mount 错误路径              | ⏸   |
| L2.1 | create + lookup 嵌套        | ⏸   |
| L2.2 | 跨块读写 4096 字节          | ⏸   |
| L2.3 | readdir 验证                | ⏸   |
| L2.4 | mkdir + type 检查           | ⏸   |
| L2.5 | unlink 语义                 | ⏸   |
| L2.6 | 硬链接 nlink 验证           | ⏸   |
| L2.7 | mknod 设备节点              | ⏸   |
| L2.8 | stat 完整性                 | ⏸   |
| L2.9 | truncate (O_TRUNC)          | ⏸   |
| L3   | 路径遍历 (5 子项)           | ⏸   |
| L4.1 | 并发 write (fork)           | ⏸   |
| L4.2 | unlink-while-open           | ⏸   |
| L4.3 | 大文件 200 块 (间接块)      | ⏸   |

### 7.5 赛题参考测试结果

```
$ cd tests
$ cat cmds.sh | sh
--------- test1: begin... ---------------------{
test_copy_file(/mnt/hello.c,/hello.c): succeed
test_copy_file(/mnt/hello.c,/tests/hello.c): succeed
test_copy_file(/hello.c,/mnt/hello.c): succeed
test_copy_file(/tests/hello.c,/mnt/hello.c): succeed
--------- test1: finished ---------------------}

--------- test2: begin... ---------------------{
将从 xv6 文件系统复制文件到 EXT2 文件系统
✓ 文件复制成功！
验证复制结果:
✓ 复制成功！文件内容:
#include "kernel/types.h"
#include "user/user.h"
void main () { printf ("Hello, world!\\n"); }
test_cross_fs_copy(): succeed
--------- test2: finished ---------------------}
```

### 7.6 FAT32测试

<!-- TODO: 待fat32test编写后补充 -->

### 7.7 已知问题

<!-- TODO: 记录当前未通过的测试项及分析 -->

| 测试项                  | 症状                            | 初步分析 |
| ----------------------- | ------------------------------- | -------- |
| L1 — mount/umount 循环 | first mount failed              | ______   |
| L1 — mount 错误路径    | pre-mount for error test failed | ______   |
| L4 — concurrent write  | expected 256, got 128           | ______   |
| L4 — unlink while open | remount failed                  | ______   |

---

## 第八章  设计精巧度分析

### 8.1 与Linux VFS对比

| 维度       | Linux                                      | 本作品        |
| ---------- | ------------------------------------------ | ------------- |
| VFS代码量  | >10万行                                    | ~______行     |
| 支持FS类型 | 数十种                                     | 3种           |
| 核心抽象   | 多层继承（dentry/inode/address_space/...） | 统一vnode+ops |
| 设计目标   | 高性能通用                                 | 教学精简      |

### 8.2 整体代码量统计

| 类别               | 文件      | 行数               |
| ------------------ | --------- | ------------------ |
| VFS核心            | vfs.c/h   | ______             |
| ext2               | ext2.c/h  | ______             |
| xv6fs              | xv6fs.c   | ______             |
| FAT32              | fat32.c/h | ______             |
| HAL                | hal/      | ______             |
| **合计新增** |           | **______**   |
| 原xv6-rev5         |           | ~10000             |
| **增幅**     |           | **~______%** |

### 8.3 设计模式

| 模式       | 应用位置  | 效果                      |
| ---------- | --------- | ------------------------- |
| 策略模式   | vnode_ops | 每FS独立实现，VFS层不感知 |
| 注册表模式 | fstypes[] | 添加新FS仅需一行注册      |
| 句柄模式   | vnode     | 所有文件操作统一入口      |

### 8.4 与参考项目对比

<!-- TODO: 对比静春山、RuOK项目 -->

| 维度           | 本作品             | 静春山（SpringOS）     | RuOK |
| -------------- | ------------------ | ---------------------- | ---- |
| VFS代码行数    | ______             | ~200行（薄适配层）     | ?    |
| ext2/4代码行数 | ______ (ext2手写)  | ~4万行（集成lwext4库） | ?    |
| 支持FS数量     | 3                  | 1（仅ext4）            | ?    |
| 独立HAL        | 是                 | 是                     | ?    |
| 设计路线       | 从零手写，教学精简 | 集成成熟库，功能完整   | ?    |

---

## 第九章  挑战与展望

### 9.1 技术难点回顾

- ext2 orphaned延迟释放的并发安全
- ".."跨文件系统返回
- mount失败路径的完整资源清理
- VFS路径穿越的性能优化

### 9.2 未来工作

- FAT32完整支持
- LoongArch平台验证
- 更多文件系统（tmpfs、procfs）
- VFS写缓存与预读

### 9.3 收获与体会

<!-- TODO: 最后填写 -->

---

## 附录

### A. 编译与运行

```bash
# 编译
make

# 制作ext2镜像
make ext2.img

# 运行
make qemu

# 测试
mount /mnt 1 ext2
ext2test1
ext2test2
ext2test3
```

### B. O_APPEND 与 O_TRUNC 支持

<!-- TODO: 在 file.c/sys_open 中的处理
  - O_TRUNC: vfs_open 路径中，若标志位包含 O_TRUNC，调用 ops->truncate
  - O_APPEND: file_write 中，将偏移量设为文件末尾再写入
  - 标志位定义见 kernel/fcntl.h
-->

### C. 参考资料

- xv6-riscv-rev5: https://github.com/mit-pdos/xv6-riscv
- 赛题测试仓库: https://github.com/yanjun-wen/xv6-extend-vfs
- 参考项目1(静春山/SpringOS): https://gitlab.eduxiji.net/educg-group-36002-2710490/T202510558995330-264
- 参考项目2(RuOK): https://gitlab.eduxiji.net/educg-group-36002-2710490/T202510486995232-2402

### D. 测试截图

<!-- TODO: 附终端截图 -->
