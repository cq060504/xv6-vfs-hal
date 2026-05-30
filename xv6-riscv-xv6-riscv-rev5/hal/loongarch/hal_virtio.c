// LoongArch RAM-disk virtio replacement.
//
// LoongArch QEMU virt uses PCI-based virtio (not MMIO like RISC-V).
// Instead of implementing a full PCI enumeration + virtio-over-PCI
// driver, we embed the filesystem image directly in the kernel binary
// and serve block I/O from a writable in-memory copy.
//
// The fs.img is converted to an ELF object via objcopy and linked at
// build time.  Symbols: _binary_fs_img_start, _binary_fs_img_end.

#include "types.h"
#include "hal/hal.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"

// Embedded filesystem image boundaries (from objcopy).
extern char _binary_fs_img_start[];
extern char _binary_fs_img_end[];

// The RAM disk is stored as an array of page pointers, each holding
// PGSIZE bytes.  blockno * BSIZE maps to linear offset in this array.
static uchar *ramdisk_pages[512];  // up to 2MB (512 * 4KB)
static uint   ramdisk_npages;
static uint   ramdisk_size;       // total size in bytes
static struct spinlock ramdisk_lock;

void
virtio_disk_init(void)
{
  initlock(&ramdisk_lock, "ramdisk");

  uint64 size = (uint64)_binary_fs_img_end - (uint64)_binary_fs_img_start;
  if (size == 0)
    panic("virtio_disk_init: empty fs.img");
  if (size > sizeof(ramdisk_pages) / sizeof(ramdisk_pages[0]) * PGSIZE)
    panic("virtio_disk_init: fs.img too large");

  ramdisk_npages = (size + PGSIZE - 1) / PGSIZE;
  ramdisk_size   = size;

  // Allocate writable pages and copy from the embedded image.
  for (uint i = 0; i < ramdisk_npages; i++) {
    ramdisk_pages[i] = (uchar *)kalloc();
    if (ramdisk_pages[i] == 0)
      panic("virtio_disk_init: kalloc");
    memset(ramdisk_pages[i], 0, PGSIZE);
  }

  uint64 remain = size;
  for (uint i = 0; i < ramdisk_npages; i++) {
    uint64 n = remain;
    if (n > PGSIZE) n = PGSIZE;
    memmove(ramdisk_pages[i], _binary_fs_img_start + i * PGSIZE, n);
    remain -= n;
  }

  printf("virtio_disk_init: %d bytes, %d pages\n", ramdisk_size, ramdisk_npages);
}

void
virtio_disk_rw(struct buf *b, int write)
{
  uint64 offset = b->blockno * BSIZE;

  if (offset + BSIZE > ramdisk_size)
    panic("virtio_disk_rw: beyond ramdisk");

  acquire(&ramdisk_lock);

  uchar *base = ramdisk_pages[offset / PGSIZE];
  uint   off  = offset % PGSIZE;

  if (write) {
    memmove(base + off, b->data, BSIZE);
  } else {
    memmove(b->data, base + off, BSIZE);
  }

  // Synchronous completion.
  b->disk = 0;

  release(&ramdisk_lock);
}

void
virtio_disk_intr(void)
{
  // RAM disk completes I/O synchronously.
}
