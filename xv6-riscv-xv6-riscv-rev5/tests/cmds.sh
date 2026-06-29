# xv6 VFS/ext2 测试脚本
# 兼容赛事官方测试流程
# 启动后执行: cat cmds.sh | sh

# ====== 1. 准备环境 ======
echo "=== Step 1: setup ==="
mkdir /mnt
cat hello.c > /hello.c
cat hello.c > /mnt/hello.c

# ====== 2. 挂载 ext2 文件系统 ======
echo "=== Step 2: mount ext2 ==="
mount /mnt 1 ext2

# ====== 3. 运行 test1 (跨文件系统拷贝 4 组合) ======
echo "=== Step 3: test1 (cross-FS copy) ==="
test1

# ====== 4. 运行 test2 (跨 FS 拷贝 + 内容验证) ======
echo "=== Step 4: test2 (cross-FS copy verify) ==="
test2

# ====== 5. 运行 ext2test3 (L1~L4 综合测试 15 项) ======
echo "=== Step 5: ext2test3 (15 tests) ==="
ext2test3

# ====== 6. 运行 xv6 原生 usertests ======
echo "=== Step 6: usertests ==="
usertests -q

echo ""
echo "========== ALL TESTS COMPLETED =========="
