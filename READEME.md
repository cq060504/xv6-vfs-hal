# xv6虚拟文件系统和硬件抽象层的设计与实现

> **2026年全国大学生计算机系统能力大赛 · 操作系统设计赛(全国) · OS功能挑战赛道**
>
> 队伍名称：**tjuer** ｜ 学校：**天津大学** ｜ 指导教师：**李罡**
>
> 成员：陈权，杨子游

---

## 项目简介

本项目基于 **xv6-riscv rev5**（MIT 教学操作系统），扩充了 **虚拟文件系统（VFS）** 和 **硬件抽象层（HAL）**，使其以精简的代码同时支持 **3 种文件系统** 和 **2 种 CPU 架构**，打造一个设计精巧、适合教学的小型操作系统。

### 核心成果

| 维度         | 原 xv6-riscv rev5 | 本项目                     |
| ------------ | ----------------- | -------------------------- |
| 文件系统     | xv6fs (单一)      | xv6fs + ext2 + FAT32       |
| CPU 架构     | RISC-V (单一)     | RISC-V + LoongArch         |
| 抽象层       | 无                | VFS (~600行) + HAL（3034） |
| 核心新增代码 | —                | ~4000 行                  |

---

## 项目特性

- **VFS 虚拟文件系统层** — 统一 vnode 抽象，13 个操作接口，挂载点自动穿越，".." 跨文件系统返回，引用计数生命周期管理
- **ext2 文件系统完整实现** — 约 1200 行，覆盖文件/目录/设备节点创建、读写、删除、硬链接、时间戳维护，接入 VFS
- **xv6fs 零修改胶水层** — 约 320 行，将原生 inode 包装为 vnode，`kernel/fs.c` 和 `kernel/log.c` 一行不改
- **FAT32 文件系统支持** — 目录项管理、FAT 链遍历、簇分配（仅 RISC-V）
- **HAL 硬件抽象层** — 统一接口（启动、页表、中断、定时器、串口、上下文切换），RISC-V + LoongArch 双平台实现
- **完整测例通过** — xv6 原生 usertests + 赛方测试用例 (ext2test1/2) + 自定义综合测试 (ext2test3) + FAT32 测试
- **Docker 一键开发环境** — RISC-V 和 LoongArch 两个隔离容器，统一挂载源码目录

---

## 项目分支

| 分支                 | 说明                                          |
| -------------------- | --------------------------------------------- |
| `master`           | 项目主分支，VFS + HAL + ext2 + FAT32 完整实现 |
| `final-submission` | 最终提交版本（已合并到 master）               |
| `HAL`              | HAL 硬件抽象层早期开发分支                    |
| `vfs`              | VFS 虚拟文件系统早期开发分支                  |
| `merge-vfs-hal`    | VFS 与 HAL 合并集成历史分支                   |

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

# FAT32 测试（仅 RISC-V）
fat32test
```

| 测试                | 预期结果             | 说明                                                   |
| ------------------- | -------------------- | ------------------------------------------------------ |
| `usertests -q`    | `ALL TESTS PASSED` | 覆盖 fork/exec/pipe/file/fs 等                         |
| ext2 测试 (demo.sh) | 全部通过             | test1 (跨FS拷贝) + test2 (内容验证) + ext2test (L1~L4) |
| `fat32test`       | `ALL PASSED`       | 挂载/创建/查找/mkdir/嵌套/unlink，共 6 项              |

---

## 架构概览

```
用户态应用程序（sh, cat, ls, ext2test, ...）
    │ 系统调用 (sys_open, sys_read, sys_write, sys_mount, ...)
    ▼
┌─────────────────────────────────────────────┐
│          VFS 虚拟文件系统层                  │
│  vfs.c/h  (~600行)                          │
│                                             │
│  · vnode 统一文件抽象                        │
│  · vnode_ops 操作接口 (13个函数指针)          │
│  · mount 挂载管理 (mounttable[NMOUNT])        │
│  · namei 路径遍历 + 挂载点自动穿越             │
│  · 引用计数生命周期管理                        │
├────────────┬────────────┬────────────────────┤
│  xv6fs胶水  │  ext2实现  │  FAT32实现         │
│ xv6fs.c   │ ext2.c/h  │  fat32.c/h          │
│  ~320行    │ ~1200行   │                    │
│            │            │                    │
│ 包装原生   │ 完整ext2   │ FAT32文件系统      │
│ inode为    │ 文件系统    │ 读写支持           │
│ vnode      │ 读写支持    │                    │
├────────────┴────────────┴────────────────────┤
│            HAL 硬件抽象层                      │
│  hal/                                       │
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

**核心思想**：VFS 通过统一 vnode 抽象隔离上层系统调用与具体文件系统；HAL 通过统一接口头文件隔离内核核心逻辑与 CPU 架构。两层抽象互为正交——添加新文件系统不需要懂 CPU，移植新架构不需要懂文件系统。

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

---

## 目录结构

```
xv6-vfs-hal/
├── README.md                              ← 本文件
├── xv6-riscv-xv6-riscv-rev5/              ← 主代码目录
│   ├── DESIGN_DOC.md                      ← 详细设计文档（9章）
│   ├── 操作指南.md                        ← Docker 环境搭建与测试流程
│   ├── doc/
│   │   ├── 演示视频.mp4                   ← 项目演示视频
│   │   └── 决赛幻灯片.pptx                ← 决赛答辩 PPT
│   ├── kernel/                            ← 内核源码
│   │   ├── vfs.c / vfs.h                  ← VFS 核心 (~600行)
│   │   ├── ext2.c / ext2.h                ← ext2 实现 (~1200行)
│   │   ├── xv6fs.c                        ← xv6fs 胶水层 (~320行)
│   │   ├── fat32.c / fat32.h              ← FAT32 实现
│   │   ├── file.c / file.h                ← 文件描述符层
│   │   ├── sysfile.c                      ← 系统调用适配
│   │   └── ...
│   ├── hal/                               ← 硬件抽象层
│   │   ├── riscv/                         ← RISC-V 平台实现
│   │   └── loongarch/                     ← LoongArch 平台实现
│   ├── user/                              ← 用户态程序
│   │   ├── ext2test.c                     ← ext2 综合测试
│   │   ├── fat32test.c                    ← FAT32 测试
│   │   ├── test1.c / test2.c              ← 跨文件系统拷贝测试
│   │   └── ...
│   ├── Makefile                           ← 构建系统
│   ├── demo.sh                            ← 一键测试脚本
│   └── Dockerfile.loongarch               ← LoongArch 容器镜像
```

---

## 代码量统计

| 模块               | 文件                                    | 行数                |
| ------------------ | --------------------------------------- | ------------------- |
| VFS 核心           | `kernel/vfs.c` + `kernel/vfs.h`     | ~600 行             |
| ext2 实现          | `kernel/ext2.c` + `kernel/ext2.h`   | ~1200 行            |
| xv6fs 胶水         | `kernel/xv6fs.c`                      | ~320 行             |
| FAT32              | `kernel/fat32.c` + `kernel/fat32.h` | ~ 370行             |
| HAL                | `hal/`                                | ~3000 行            |
| 用户测试           | `user/ext2test.c` 等                  | ~700 行             |
| **合计新增** |                                         | **~6200 行** |
| xv6-rev5 基线      |                                         | ~10000 行           |

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

*最后更新：2026-06-30 ｜ 分支：master*