// LoongArch RAM-disk virtio replacement.
//
// LoongArch QEMU virt uses PCI-based virtio (not MMIO like RISC-V).
// Instead of implementing a full PCI enumeration + virtio-over-PCI
// driver, we embed the filesystem image directly in the kernel binary
// and serve block I/O from a writable in-memory copy.
//
// Each image is converted to an ELF object via objcopy and linked at build
// time.  Device 1 is fs.img; device 2 is fs1.img when VIRTIO_NDISK=2.

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

#ifndef VIRTIO_NDISK
#define VIRTIO_NDISK 1
#endif

#if VIRTIO_NDISK < 1 || VIRTIO_NDISK > 2
#error "VIRTIO_NDISK must be 1 or 2"
#endif

#if VIRTIO_NDISK > 1
extern char _binary_fs1_img_start[];
extern char _binary_fs1_img_end[];
#endif

#define NRAMDISK VIRTIO_NDISK
#define RAMDISK_MAX_PAGES 2048

struct ramdisk {
  uchar *pages[RAMDISK_MAX_PAGES];
  uint npages;
  uint size;
  struct spinlock lock;
};

static struct ramdisk ramdisk[NRAMDISK];

static void
ramdisk_init(uint idx, char *start, char *end)
{
  struct ramdisk *rd = &ramdisk[idx];
  initlock(&rd->lock, "ramdisk");

  uint64 size = (uint64)end - (uint64)start;
  if (size == 0)
    panic("ramdisk_init: empty image");
  if (size > RAMDISK_MAX_PAGES * PGSIZE)
    panic("ramdisk_init: image too large");

  rd->npages = (size + PGSIZE - 1) / PGSIZE;
  rd->size = size;

  // Allocate writable pages and copy from the embedded image.
  for (uint i = 0; i < rd->npages; i++) {
    rd->pages[i] = (uchar *)kalloc();
    if (rd->pages[i] == 0)
      panic("ramdisk_init: kalloc");
    memset(rd->pages[i], 0, PGSIZE);
  }

  uint64 remain = size;
  for (uint i = 0; i < rd->npages; i++) {
    uint64 n = remain;
    if (n > PGSIZE) n = PGSIZE;
    memmove(rd->pages[i], start + i * PGSIZE, n);
    remain -= n;
  }

  printf("ramdisk%d: %d bytes, %d pages\n", idx + 1, rd->size, rd->npages);
}

static struct ramdisk*
ramdisk_for_dev(uint dev)
{
  if (dev < 1 || dev > NRAMDISK)
    panic("ramdisk_for_dev");
  return &ramdisk[dev - 1];
}

void
virtio_disk_init(void)
{
  ramdisk_init(0, _binary_fs_img_start, _binary_fs_img_end);
#if VIRTIO_NDISK > 1
  ramdisk_init(1, _binary_fs1_img_start, _binary_fs1_img_end);
#endif
}

void
virtio_disk_rw(struct buf *b, int write)
{
  struct ramdisk *rd = ramdisk_for_dev(b->dev);
  uint64 offset = b->blockno * BSIZE;

  if (offset + BSIZE > rd->size)
    panic("virtio_disk_rw: beyond ramdisk");

  acquire(&rd->lock);

  uchar *base = rd->pages[offset / PGSIZE];
  uint   off  = offset % PGSIZE;

  if (write) {
    memmove(base + off, b->data, BSIZE);
  } else {
    memmove(b->data, base + off, BSIZE);
  }

  // Synchronous completion.
  b->disk = 0;

  release(&rd->lock);
}

void
virtio_disk_intr(void)
{
  // RAM disk completes I/O synchronously.
}
