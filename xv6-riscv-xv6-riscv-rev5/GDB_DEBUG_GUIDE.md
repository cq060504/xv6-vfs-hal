# GDB 调试 mount 失败指南

## 关键常量

- `FSSIZE = 2000` (定义在 `kernel/param.h:12`)
- ext2 超级块在 `blkno = 1 + FSSIZE = 2001` 处读取

## 磁盘布局

```
fs.img (一块 virtio 磁盘，只有一个 disk，只有一个 virtio_blk_device):
┌────────────────────────────────────────────────┐
│ 块 0 ~ 1999                  │ 块 2000 ~ ...    │
│ xv6fs (FSSIZE=2000 blocks)  │ ext2 (10MB)      │
│  xv6fs 超级块在块 1         │ ext2 超级块在    │
│                             │ 块 2001          │
└────────────────────────────────────────────────┘
```

- VFS 的 `dev` 号是**逻辑设备号**，用于区分同一块物理盘上的不同文件系统实例
- `dev=1` 对应 xv6fs (mounttable[0])
- `dev=2` 对应 ext2

## GDB 分步调试方案

### 终端 1: 启动 QEMU
```bash
make qemu-gdb
```

### 终端 2: 启动 GDB
```bash
 gdb-multiarch
(gdb) file kernel/kernel
(gdb) target remote localhost:26000
(gdb) b ext2_mount
(gdb) c
```

### 断点命中后，逐步执行：

```
# 进入函数
(gdb) n
# 停在 bp = bread(dev, 1 + FSSIZE);

# 看 dev 参数是多少
(gdb) p dev

# 执行 bread
(gdb) n
# 现在 bp 已经有值了

# 检查 bp 是否为空 — 这是最关键的一步！
(gdb) p bp
```

### 根据 bp 的值分情况处理：

#### 情况 A: bp == 0 (bread 返回空指针)
说明 `bread(dev, 2001)` 失败了 — 块 2001 读取失败。

继续调试：
```
(gdb) b bread
(gdb) c
# bread 断点会命中多次，忽略前面的（xv6fs 的读取），
# 只关注 blkno >= 2000 的调用
(gdb) p b->blockno         # 看请求的块号
(gdb) p b->dev             # 看设备号
(gdb) p b->disk            # 看是否等待磁盘 I/O
```

如果是 virtio 驱动层面出错，检查：
```
(gdb) b virtio_disk_rw
(gdb) c
(gdb) p b->blockno
(gdb) p sector             # sector = b->blockno * 2
```

可能的根本原因：
1. `fs.img` 太小，块 2001 超出了磁盘范围
2. `bio.c` 中的块号超出缓存范围
3. virtio 磁盘的扇区计算溢出

#### 情况 B: bp != 0 (bread 成功) 
```
# 检查超级块 magic
(gdb) p/x priv
# priv 还没分配，需要用 bp->data 看
(gdb) p/x *(uint32*)(bp->data + 56)   # s_magic 在 ext2_superblock 中的偏移
# EXT2_MAGIC = 0xEF53
```

如果 magic 不匹配，说明 ext2.img 没有正确拼接到 fs.img 后面。

检查：
```bash
# 在主机上
xxd -s 2049024 -l 4 fs.img  # 偏移 2001*1024 = 2049024，看 2 字节 magic
# 或者
dd if=fs.img bs=1024 skip=2001 count=1 | xxd
```

#### 情况 C: 超级块读取成功但后续失败

```
# 下断点在 magic 检查之后
(gdb) b ext2_mount if priv != 0
# 或直接
(gdb) n   # 继续步进到 if(priv->sb.s_magic != EXT2_MAGIC)
(gdb) p/x priv->sb.s_magic
(gdb) n   # 到 s_log_block_size 检查
(gdb) p priv->sb.s_log_block_size

# 再到 bg descriptor 读取
(gdb) n
(gdb) p bp
# 再到根 inode 获取
(gdb) n
(gdb) p mp->root
```

## 快速排查清单

1. `bread(dev, 2001)` 返回 NULL？→ 磁盘读取失败，检查 `fs.img` 大小
2. `s_magic != 0xEF53`？→ ext2.img 没有正确合并
3. `s_log_block_size != 0`？→ ext2 使用了 2048/4096 块大小
4. `mp->root == 0`？→ ext2 根 inode (inum=2) 无法读取






## 修复完成

**根因**：`vfs_nameiparent` 的 while 循环中调用 `vp->ops->lookup(vp, name, &next)` 时，`name` 是未初始化的输出参数（栈上垃圾数据），导致底层 `dirlookup`/`strncmp` 访问无效地址触发缺页异常 → `usertrap` → 进程被 killed。

**修复**：`kernel/vfs.c` 第355行，将 `name` 改为 `buf`（正确的当前路径元素）。

**代码量**：1行，1个字符变更。

---

## GDB 验证步骤

### 1. 启动 GDB（两个终端）

终端A：
```bash
cd /home/cq/xv6-vfs-hal/xv6-riscv-xv6-riscv-rev5
make qemu-gdb
```

终端B：
```bash
cd /home/cq/xv6-vfs-hal/xv6-riscv-xv6-riscv-rev5
gdb-multiarch kernel/kernel
```
```
target remote localhost:26000
```

### 2. 设置断点

```
break vfs_nameiparent
break vfs_namei
info break
```

### 3. 运行测试

```
c
```
xv6 启动后在 xv6 shell 中执行 `mount` 相关测试（如 `mount /a/d ext2 /dev/vda2` 或 `ext2test3`）。

### 4. 观察 `vfs_nameiparent`

当断点命中后：
```
# 查看传入的 path
p (char*)$a0

# 单步到 while 循环内 lookup 调用附近
n
n
...

# 查看 buf 值（应为正确的路径元素如 "a"，而不是乱码）
p buf
```

### 预期结果对比

| 项目 | 修复前 | 修复后 |
|------|--------|--------|
| `p buf` | 乱码 `"\300o\377\377?\000..."` | `"a"` / `"d"` 等正确路径元素 |
| lookup 调用 | 触发 usertrap（缺页） | 正常返回，不触发异常 |
| 进程状态 | killed | 正常运行 |
| mount 结果 | FAIL | PASS |