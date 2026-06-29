
echo "========================================"
echo "  xv6 VFS/ext2 功能演示"
echo "  RISC-V + LoongArch | HAL 抽象层"
echo "========================================"

echo ""
echo "[Phase 1] 准备环境"
echo "  创建 /tests 目录..."
mkdir /tests

echo "  复制 hello.c 到 /tests ..."
cat hello.c > /tests/hello.c

echo "  挂载 ext2 文件系统..."
mkdir /mnt
mount /mnt 1 ext2
cat hello.c > /mnt/hello.c
echo ""
echo "========================================"
echo "[Phase 2] 官方测试 — 跨文件系统拷贝"
echo "========================================"
echo ""
echo "  [test1] 双向拷贝 xv6fs <-> ext2 (4组)"
test1
echo ""
echo "  [test2] 跨FS拷贝 + 内容验证"
test2

echo ""
echo "========================================"
echo "[Phase 3] ext2 综合功能测试"
echo "========================================"
echo ""
echo "  覆盖:"
echo "    L1: mount/umount 体系"
echo "    L2: create/lookup/read/write/readdir/mkdir/unlink/link/mknod/stat/truncate"
echo "    L3: namei 路径遍历 (含 .. 跨 mount 点)"
echo "    L4: 并发写入 / unlink-while-open / 大文件 indirect block"
echo ""
ext2test

echo ""
echo "========================================"
echo "  演示完毕"
echo "========================================"
