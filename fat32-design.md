# FAT32 文件系统设计文档

## 架构设计

FAT32 作为第三个 VFS 后端注册，与 xv6fs、ext2 并存，通过 `vfs_register(fat32_mount, "fat32")` 注册。

**调用路径**：用户程序 → VFS (`vfs_*`) → `fat32_vnops` → `bio.c` (`bread`/`bwrite`) → virtio 磁盘驱动

## 文件结构

| 文件 | 说明 |
|------|------|
| `kernel/fat32.h` | 数据结构：`fat32_bpb`(BPB)、`fat32_dirent`(32-byte 目录项)、FAT 常量 |
| `kernel/fat32.c` | VFS 后端实现：mount、lookup、read、write、create、unlink、mkdir、readdir、stat、truncate |

## 数据结构

### BPB (BIOS Parameter Block)

位于 FAT32 镜像的第 0 扇区。关键字段：

| 字段 | 偏移 | 大小 | 说明 |
|------|------|------|------|
| `BPB_BytsPerSec` | 11 | 2 | 每扇区字节数，通常 512 |
| `BPB_SecPerClus` | 13 | 1 | 每簇扇区数 |
| `BPB_RsvdSecCnt` | 14 | 2 | 保留扇区数，FAT32 典型值 32 |
| `BPB_NumFATs` | 16 | 1 | FAT 表份数，通常 2 |
| `BPB_TotSec16/32` | 19/32 | 2/4 | 总扇区数 |
| `BPB_FATSz32` | 36 | 4 | FAT32 特有：每个 FAT 表的扇区数 |
| `BPB_RootClus` | 44 | 4 | 根目录起始簇号，通常为 2 |

### 目录项 (32 bytes)

```
Offset  Size  Field
0       11    DIR_Name (8.3 短文件名，空格填充)
11      1     DIR_Attr (ATTR_DIRECTORY=0x10, ATTR_ARCHIVE=0x20)
20      2     DIR_FstClusHI (起始簇号高 16 位)
26      2     DIR_FstClusLO (起始簇号低 16 位)
28      4     DIR_FileSize (文件大小)
```

### FAT 表

FAT32 每个表项 32-bit（有效位 28-bit，高 4 位保留）。关键值：

| 值 | 含义 |
|----|------|
| `0x00000000` | 空闲簇 |
| `0x0FFFFFF7` | 坏簇 |
| `>= 0x0FFFFFF8` | 簇链结束 (EOC) |

## 核心计算公式

```
FirstDataSector = BPB_RsvdSecCnt + (BPB_NumFATs × FATSz)
FirstSectorOfCluster(N) = ((N - 2) × BPB_SecPerClus) + FirstDataSector
FAT 中簇 N 的偏移 = N × 4 （每项 4 字节）
FAT 所在扇区 = BPB_RsvdSecCnt + (偏移 / BPB_BytsPerSec)
```

## 磁盘布局

```
[ 扇区 0: BPB + 启动代码 (512B) ]
[ 扇区 1-31: 保留区          ]
[ FAT 表 1: BPB_FATSz32 扇区 ]
[ FAT 表 2: BPB_FATSz32 扇区 ]
[ 数据区: 簇 2 = 根目录开始  ]
```

---

## mount 失败问题与解决方案

### 问题 1: 512-byte 扇区 vs 1024-byte 块

**现象**：`mount /fat 3 fat32` 失败，`fat32_mount` 中 `bread(dev, 0)` 成功但读取的 BPB 数据不对。

**根因**：xv6 的 `bread()`/`bwrite()` 使用固定块大小 `BSIZE=1024`。FAT32 标准使用 512-byte 扇区。一个 xv6 块包含 2 个 FAT32 扇区。

```
xv6 块 0: [ FAT32 扇区 0 (512B) ][ FAT32 扇区 1 (512B) ]
xv6 块 1: [ FAT32 扇区 2        ][ FAT32 扇区 3        ]
```

所有 FAT32 的扇区号访问都必须转换为 xv6 的块号 + 块内偏移：

```c
// FAT32 扇区号 sec → xv6 块号 + 偏移
blkno = sec / (BSIZE / bytes_per_sec);   // bytes_per_sec = 512, BSIZE = 1024
offset = (sec % 2) * 512;
```

**解决方案**：引入 `sread()`/`swrite()` 辅助函数，封装扇区→块的转换。所有 FAT32 磁盘 I/O 通过这两个函数完成。

### 问题 2: BPB_TotSec16 不为 0

**现象**：验证 `BPB_TotSec16 != 0` 导致 mount 被拒绝。

**根因**：FAT32 规范要求 `BPB_TotSec16` 必须为 0（使用 `BPB_TotSec32` 存储总扇区数）。但 Linux 的 `mkfs.fat` 对小镜像（≤32MB）会将总扇区数填入 `TotSec16`（值 ≤ 65535，16 位够用），而 `TotSec32` 保持为 0。

**解决方案**：使用 `TotSec16 ? TotSec16 : TotSec32` 作为总扇区数，不做 `TotSec16==0` 的强制检查。FAT32 类型判定仅依赖 `BPB_FATSz32 != 0`。

### 问题 3: xv6fs inode 耗尽

**现象**：`mkdir /fat` 在 shell 中手动执行失败。

**根因**：xv6fs 使用固定大小的 inode 表（13 blocks × 16 inodes/block = 208 inodes），用户程序（~26 个）消耗大量 inode。

**解决方案**：在 `init.c` 启动时预先创建 `/fat` 目录（与 `/mnt` 一样由 init 处理）。

## VFS 操作实现

| 操作 | 实现 | 说明 |
|------|------|------|
| `fat32_mount` | 读 BPB，验证，初始化 mount_priv | 返回 root vnode |
| `fat32_lookup` | 扫描目录项，按 8.3 名匹配 | 返回子 vnode |
| `fat32_read` | 遍历 FAT 簇链读取 | 通过 `sread()` |
| `fat32_write` | 写入并自动扩展簇链 | 通过 `swrite()` |
| `fat32_create` | 分配目录项 + 可选簇分配 | 目录创建 `.` 和 `..` |
| `fat32_unlink` | 标记目录项为 0xE5，释放簇链 | 检查目录为空 |
| `fat32_readdir` | 遍历目录，返回 xv6 dirent | 跳过已删除和长文件名项 |

---

## 测试样例（`user/fat32test.c`）

测试程序在启动后在 shell 中输入 `fat32test` 运行。所有测试遵循 fail-fast 原则：任何一项失败立即退出并打印 `FAIL`，全部通过则打印 `ALL PASSED`。

### 测试 1: mount — 挂载 FAT32 文件系统

```c
mount("/fat", 3, "fat32")
```

| 属性 | 值 |
|------|-----|
| **测试的 VFS 操作** | `fat32_mount` |
| **调用路径** | `mount()` → `sys_mount()` → `vfs_mount()` → `fat32_mount(3)` |
| **验证点** | `fat32_mount` 返回非 NULL mount 结构；BPB 合法（`BPB_BytsPerSec=512`、`BPB_FATSz32≠0`）；根目录 vnode 创建成功 |

### 测试 2: create + write — 创建文件并写入数据

```c
fd = open("/fat/hello.txt", O_WRONLY | O_CREATE)
write(fd, "hello from fat32", 16)
```

| 属性 | 值 |
|------|-----|
| **测试的 VFS 操作** | `fat32_create`、`fat32_write` |
| **调用路径** | `open()` → `vfs_open()` → `vfs_namei()` → `fat32_lookup()`（未找到）→ `vfs_create()` → `fat32_create()` → 分配目录项 + 分配 FAT 簇 → `fat32_write()` → 遍历簇链 + 写入数据 |
| **验证点** | 文件创建成功（fd ≥ 0）；16 字节全部写入；FAT32 目录项正确定位；簇分配成功；数据通过 `swrite()` 写入正确的扇区 |

### 测试 3: lookup + stat — 重新打开文件并获取属性

```c
fd = open("/fat/hello.txt", O_RDONLY)
fstat(fd, &st)
```

| 属性 | 值 |
|------|-----|
| **测试的 VFS 操作** | `fat32_lookup`、`fat32_stat` |
| **调用路径** | `open()` → `vfs_namei()` → `fat32_lookup()` → 扫描根目录目录项 → 按 8.3 名匹配 `"HELLO   TXT"` → 读 `DIR_FstClusLO/HI` → 创建 vnode；`fstat()` → `fat32_stat()` → 返回文件类型和属性 |
| **验证点** | 文件可重新打开（fd ≥ 0）；文件类型为 `T_FILE`（普通文件，非目录）；目录项匹配逻辑正确（大小写转换 + 空格 padding） |

### 测试 4: mkdir — 创建子目录

```c
mkdir("/fat/subdir")
```

| 属性 | 值 |
|------|-----|
| **测试的 VFS 操作** | `fat32_mkdir` → `fat32_create` |
| **调用路径** | `mkdir()` → `vfs_mkdir()` → `fat32_mkdir()` → `fat32_create(V_DIR)` → 分配簇 → 写入 `"."` 和 `".."` 目录项 → 在 FAT 表中标记 EOC |
| **验证点** | 目录创建成功；`"."` 指向自身簇号；`".."` 指向父目录簇号；FAT 表正确标记 |

### 测试 5: create nested — 在子目录中创建文件

```c
fd = open("/fat/subdir/nested.txt", O_WRONLY | O_CREATE)
write(fd, "nested", 6)
```

| 属性 | 值 |
|------|-----|
| **测试的 VFS 操作** | `fat32_lookup`（路径遍历）、`fat32_create`、`fat32_write` |
| **调用路径** | `vfs_namei("/fat/subdir/nested.txt")` → 跨目录查找 → 在 `subdir` 中查找 `"nested.txt"` → 未找到 → `fat32_create` → 写入 6 字节 |
| **验证点** | 跨目录路径解析正确；子目录中可创建文件；嵌套路径的 VFS 挂载点穿越正常 |

### 测试 6: unlink — 删除文件并验证不存在

```c
unlink("/fat/hello.txt")
open("/fat/hello.txt", O_RDONLY)  // 期望失败
```

| 属性 | 值 |
|------|-----|
| **测试的 VFS 操作** | `fat32_unlink` |
| **调用路径** | `unlink()` → `vfs_unlink()` → `fat32_unlink()` → 在目录中找到目录项 → 标记 `DIR_Name[0]=0xE5`（已删除）→ 释放 FAT 簇链 → 重新 `open()` → `fat32_lookup()` → 跳过标记为 `0xE5` 的项 → 返回 -1 |
| **验证点** | 删除后文件不可再打开；目录项被正确标记为已删除；FAT 簇链被正确释放 |

### 测试调用顺序的设计意图

测试顺序从简到繁：

```
mount → create+write → lookup+stat → mkdir → create nested → unlink
  ①         ②             ③           ④          ⑤            ⑥
```

1. ① 验证最基本的挂载能否成功
2. ② 在干净的文件系统上测试创建和写入
3. ③ 验证持久化——刚写入的文件能否被再次找到
4. ④ 测试目录创建（包括 `"."` / `".."` 元数据）
5. ⑤ 验证跨目录路径遍历
6. ⑥ 最后测试删除（被后续测试依赖的文件不能先删除）
