// Block-device abstraction interface.

#ifndef _HAL_DISK_H_
#define _HAL_DISK_H_

#ifndef HAL_NDISK
#define HAL_NDISK 3
#endif

#if HAL_NDISK < 1 || HAL_NDISK > 3
#error "HAL_NDISK must be 1-3"
#endif

struct buf;

void hal_disk_init(void);
void hal_disk_rw(struct buf *, int);
void hal_disk_intr(void);

#endif
