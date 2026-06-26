//
// driver for qemu's virtio disk device.
// uses qemu's mmio interface to virtio.
//
// qemu ... -drive file=fs.img,if=none,format=raw,id=x0 -device virtio-blk-device,drive=x0,bus=virtio-mmio-bus.0
//

#include "types.h"
#include "hal/hal.h"
#include "defs.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "fs.h"
#include "buf.h"
#include "virtio.h"

#ifndef VIRTIO_NDISK
#define VIRTIO_NDISK 1
#endif

#if VIRTIO_NDISK < 1 || VIRTIO_NDISK > 3
#error "VIRTIO_NDISK must be 1-3"
#endif

#define NVIRTIO VIRTIO_NDISK

static uint64 virtio_base[NVIRTIO] = {
  VIRTIO0,
#if VIRTIO_NDISK > 1
  VIRTIO1,
#endif
#if VIRTIO_NDISK > 2
  VIRTIO2,
#endif
};

// the address of virtio mmio register r for disk d.
#define R(d, r) ((volatile uint32 *)(virtio_base[(d)] + (r)))

static struct virtio_disk {
  // a set (not a ring) of DMA descriptors, with which the
  // driver tells the device where to read and write individual
  // disk operations. there are NUM descriptors.
  // most commands consist of a "chain" (a linked list) of a couple of
  // these descriptors.
  struct virtq_desc *desc;

  // a ring in which the driver writes descriptor numbers
  // that the driver would like the device to process.  it only
  // includes the head descriptor of each chain. the ring has
  // NUM elements.
  struct virtq_avail *avail;

  // a ring in which the device writes descriptor numbers that
  // the device has finished processing (just the head of each chain).
  // there are NUM used ring entries.
  struct virtq_used *used;

  // our own book-keeping.
  char free[NUM];  // is a descriptor free?
  uint16 used_idx; // we've looked this far in used[2..NUM].

  // track info about in-flight operations,
  // for use when completion interrupt arrives.
  // indexed by first descriptor index of chain.
  struct {
    struct buf *b;
    char status;
  } info[NUM];

  // disk command headers.
  // one-for-one with descriptors, for convenience.
  struct virtio_blk_req ops[NUM];

  struct spinlock vdisk_lock;

} disk[NVIRTIO];

static struct virtio_disk*
disk_for_dev(uint dev)
{
  if(dev < 1 || dev > NVIRTIO)
    panic("virtio disk dev");
  return &disk[dev - 1];
}

static void
virtio_disk_init_one(int d)
{
  uint32 status = 0;
  struct virtio_disk *vdisk = &disk[d];

  initlock(&vdisk->vdisk_lock, "virtio_disk");

  if(*R(d, VIRTIO_MMIO_MAGIC_VALUE) != 0x74726976 ||
     *R(d, VIRTIO_MMIO_VERSION) != 2 ||
     *R(d, VIRTIO_MMIO_DEVICE_ID) != 2 ||
     *R(d, VIRTIO_MMIO_VENDOR_ID) != 0x554d4551){
    panic("could not find virtio disk");
  }

  // reset device
  *R(d, VIRTIO_MMIO_STATUS) = status;

  // set ACKNOWLEDGE status bit
  status |= VIRTIO_CONFIG_S_ACKNOWLEDGE;
  *R(d, VIRTIO_MMIO_STATUS) = status;

  // set DRIVER status bit
  status |= VIRTIO_CONFIG_S_DRIVER;
  *R(d, VIRTIO_MMIO_STATUS) = status;

  // negotiate features
  uint64 features = *R(d, VIRTIO_MMIO_DEVICE_FEATURES);
  features &= ~(1 << VIRTIO_BLK_F_RO);
  features &= ~(1 << VIRTIO_BLK_F_SCSI);
  features &= ~(1 << VIRTIO_BLK_F_CONFIG_WCE);
  features &= ~(1 << VIRTIO_BLK_F_MQ);
  features &= ~(1 << VIRTIO_F_ANY_LAYOUT);
  features &= ~(1 << VIRTIO_RING_F_EVENT_IDX);
  features &= ~(1 << VIRTIO_RING_F_INDIRECT_DESC);
  *R(d, VIRTIO_MMIO_DRIVER_FEATURES) = features;

  // tell device that feature negotiation is complete.
  status |= VIRTIO_CONFIG_S_FEATURES_OK;
  *R(d, VIRTIO_MMIO_STATUS) = status;

  // re-read status to ensure FEATURES_OK is set.
  status = *R(d, VIRTIO_MMIO_STATUS);
  if(!(status & VIRTIO_CONFIG_S_FEATURES_OK))
    panic("virtio disk FEATURES_OK unset");

  // initialize queue 0.
  *R(d, VIRTIO_MMIO_QUEUE_SEL) = 0;

  // ensure queue 0 is not in use.
  if(*R(d, VIRTIO_MMIO_QUEUE_READY))
    panic("virtio disk should not be ready");

  // check maximum queue size.
  uint32 max = *R(d, VIRTIO_MMIO_QUEUE_NUM_MAX);
  if(max == 0)
    panic("virtio disk has no queue 0");
  if(max < NUM)
    panic("virtio disk max queue too short");

  // allocate and zero queue memory.
  vdisk->desc = kalloc();
  vdisk->avail = kalloc();
  vdisk->used = kalloc();
  if(!vdisk->desc || !vdisk->avail || !vdisk->used)
    panic("virtio disk kalloc");
  memset(vdisk->desc, 0, PGSIZE);
  memset(vdisk->avail, 0, PGSIZE);
  memset(vdisk->used, 0, PGSIZE);

  // set queue size.
  *R(d, VIRTIO_MMIO_QUEUE_NUM) = NUM;

  // write physical addresses.
  *R(d, VIRTIO_MMIO_QUEUE_DESC_LOW) = (uint64)vdisk->desc;
  *R(d, VIRTIO_MMIO_QUEUE_DESC_HIGH) = (uint64)vdisk->desc >> 32;
  *R(d, VIRTIO_MMIO_DRIVER_DESC_LOW) = (uint64)vdisk->avail;
  *R(d, VIRTIO_MMIO_DRIVER_DESC_HIGH) = (uint64)vdisk->avail >> 32;
  *R(d, VIRTIO_MMIO_DEVICE_DESC_LOW) = (uint64)vdisk->used;
  *R(d, VIRTIO_MMIO_DEVICE_DESC_HIGH) = (uint64)vdisk->used >> 32;

  // queue is ready.
  *R(d, VIRTIO_MMIO_QUEUE_READY) = 0x1;

  // all NUM descriptors start out unused.
  for(int i = 0; i < NUM; i++)
    vdisk->free[i] = 1;

  // tell device we're completely ready.
  status |= VIRTIO_CONFIG_S_DRIVER_OK;
  *R(d, VIRTIO_MMIO_STATUS) = status;

  // plic.c and trap.c arrange for interrupts from VIRTIO0_IRQ.
}

void
virtio_disk_init(void)
{
  for(int d = 0; d < NVIRTIO; d++)
    virtio_disk_init_one(d);
}

// find a free descriptor, mark it non-free, return its index.
static int
alloc_desc(struct virtio_disk *vdisk)
{
  for(int i = 0; i < NUM; i++){
    if(vdisk->free[i]){
      vdisk->free[i] = 0;
      return i;
    }
  }
  return -1;
}

// mark a descriptor as free.
static void
free_desc(struct virtio_disk *vdisk, int i)
{
  if(i >= NUM)
    panic("free_desc 1");
  if(vdisk->free[i])
    panic("free_desc 2");
  vdisk->desc[i].addr = 0;
  vdisk->desc[i].len = 0;
  vdisk->desc[i].flags = 0;
  vdisk->desc[i].next = 0;
  vdisk->free[i] = 1;
  wakeup(&vdisk->free[0]);
}

// free a chain of descriptors.
static void
free_chain(struct virtio_disk *vdisk, int i)
{
  while(1){
    int flag = vdisk->desc[i].flags;
    int nxt = vdisk->desc[i].next;
    free_desc(vdisk, i);
    if(flag & VRING_DESC_F_NEXT)
      i = nxt;
    else
      break;
  }
}

// allocate three descriptors (they need not be contiguous).
// disk transfers always use three descriptors.
static int
alloc3_desc(struct virtio_disk *vdisk, int *idx)
{
  for(int i = 0; i < 3; i++){
    idx[i] = alloc_desc(vdisk);
    if(idx[i] < 0){
      for(int j = 0; j < i; j++)
        free_desc(vdisk, idx[j]);
      return -1;
    }
  }
  return 0;
}

void
virtio_disk_rw(struct buf *b, int write)
{
  struct virtio_disk *vdisk = disk_for_dev(b->dev);
  int d = b->dev - 1;
  uint64 sector = b->blockno * (BSIZE / 512);

  acquire(&vdisk->vdisk_lock);

  // the spec's Section 5.2 says that legacy block operations use
  // three descriptors: one for type/reserved/sector, one for the
  // data, one for a 1-byte status result.

  // allocate the three descriptors.
  int idx[3];
  while(1){
    if(alloc3_desc(vdisk, idx) == 0) {
      break;
    }
    sleep(&vdisk->free[0], &vdisk->vdisk_lock);
  }

  // format the three descriptors.
  // qemu's virtio-blk.c reads them.

  struct virtio_blk_req *buf0 = &vdisk->ops[idx[0]];

  if(write)
    buf0->type = VIRTIO_BLK_T_OUT; // write the disk
  else
    buf0->type = VIRTIO_BLK_T_IN; // read the disk
  buf0->reserved = 0;
  buf0->sector = sector;

  vdisk->desc[idx[0]].addr = (uint64) buf0;
  vdisk->desc[idx[0]].len = sizeof(struct virtio_blk_req);
  vdisk->desc[idx[0]].flags = VRING_DESC_F_NEXT;
  vdisk->desc[idx[0]].next = idx[1];

  vdisk->desc[idx[1]].addr = (uint64) b->data;
  vdisk->desc[idx[1]].len = BSIZE;
  if(write)
    vdisk->desc[idx[1]].flags = 0; // device reads b->data
  else
    vdisk->desc[idx[1]].flags = VRING_DESC_F_WRITE; // device writes b->data
  vdisk->desc[idx[1]].flags |= VRING_DESC_F_NEXT;
  vdisk->desc[idx[1]].next = idx[2];

  vdisk->info[idx[0]].status = 0xff; // device writes 0 on success
  vdisk->desc[idx[2]].addr = (uint64) &vdisk->info[idx[0]].status;
  vdisk->desc[idx[2]].len = 1;
  vdisk->desc[idx[2]].flags = VRING_DESC_F_WRITE; // device writes the status
  vdisk->desc[idx[2]].next = 0;

  // record struct buf for virtio_disk_intr().
  b->disk = 1;
  vdisk->info[idx[0]].b = b;

  // tell the device the first index in our chain of descriptors.
  vdisk->avail->ring[vdisk->avail->idx % NUM] = idx[0];

  __sync_synchronize();

  // tell the device another avail ring entry is available.
  vdisk->avail->idx += 1; // not % NUM ...

  __sync_synchronize();

  *R(d, VIRTIO_MMIO_QUEUE_NOTIFY) = 0; // value is queue number

  // Wait for virtio_disk_intr() to say request has finished.
  while(b->disk == 1) {
    sleep(b, &vdisk->vdisk_lock);
  }

  vdisk->info[idx[0]].b = 0;
  free_chain(vdisk, idx[0]);

  release(&vdisk->vdisk_lock);
}

static void
virtio_disk_intr_one(int d)
{
  struct virtio_disk *vdisk = &disk[d];

  acquire(&vdisk->vdisk_lock);

  // the device won't raise another interrupt until we tell it
  // we've seen this interrupt, which the following line does.
  // this may race with the device writing new entries to
  // the "used" ring, in which case we may process the new
  // completion entries in this interrupt, and have nothing to do
  // in the next interrupt, which is harmless.
  *R(d, VIRTIO_MMIO_INTERRUPT_ACK) = *R(d, VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3;

  __sync_synchronize();

  // the device increments vdisk->used->idx when it
  // adds an entry to the used ring.

  while(vdisk->used_idx != vdisk->used->idx){
    __sync_synchronize();
    int id = vdisk->used->ring[vdisk->used_idx % NUM].id;

    if(vdisk->info[id].status != 0)
      panic("virtio_disk_intr status");

    struct buf *b = vdisk->info[id].b;
    b->disk = 0;   // disk is done with buf
    wakeup(b);

    vdisk->used_idx += 1;
  }

  release(&vdisk->vdisk_lock);
}

void
virtio_disk_intr()
{
  for(int d = 0; d < NVIRTIO; d++){
    if(*R(d, VIRTIO_MMIO_INTERRUPT_STATUS) & 0x3)
      virtio_disk_intr_one(d);
  }
}
