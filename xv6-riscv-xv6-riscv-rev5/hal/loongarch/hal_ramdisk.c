// LoongArch RAM disk driver.
//
// QEMU's loader places three writable images in reserved low RAM before
// boot. I/O completes synchronously (no interrupts needed).

#include "types.h"
#include "hal/hal.h"
#include "defs.h"
#include "spinlock.h"
#include "fs.h"
#include "buf.h"

#ifndef VIRTIO_NDISK
#define VIRTIO_NDISK 3
#endif

#if VIRTIO_NDISK != 3
#error "LoongArch requires three loader-backed RAM disks"
#endif

#define NRAMDISK VIRTIO_NDISK

struct ramdisk {
  uchar *data;
  struct spinlock lock;
};

static struct ramdisk ramdisk[NRAMDISK];

static void
ramdisk_init(uint idx)
{
  struct ramdisk *rd = &ramdisk[idx];
  initlock(&rd->lock, "ramdisk");
  rd->data = (uchar *)(RAMDISK_BASE + idx * RAMDISK_STRIDE);
}

static struct ramdisk*
ramdisk_for_dev(uint dev)
{
  if (dev < 1 || dev > NRAMDISK)
    panic("ramdisk_for_dev");
  return &ramdisk[dev - 1];
}

void
hal_disk_init(void)
{
  for (uint i = 0; i < NRAMDISK; i++)
    ramdisk_init(i);
}

void
hal_disk_rw(struct buf *b, int write)
{
  struct ramdisk *rd = ramdisk_for_dev(b->dev);
  if (b->blockno >= RAMDISK_STRIDE / BSIZE)
    panic("hal_disk_rw: beyond ramdisk");
  uint64 offset = b->blockno * BSIZE;

  acquire(&rd->lock);

  if (write)
    memmove(rd->data + offset, b->data, BSIZE);
  else
    memmove(b->data, rd->data + offset, BSIZE);

  // Synchronous completion.
  b->disk = 0;

  release(&rd->lock);
}

void
hal_disk_intr(void)
{
  // RAM disk completes I/O synchronously.
}
