#include "types.h"
#include "param.h"
#include "hal/hal.h"
#include "defs.h"

volatile static int started = 0;

void
main()
{
  if(cpuid() == 0){
    consoleinit();
    printfinit();
    printf("\n");
    printf("xv6 kernel is booting\n");
    printf("\n");
    kinit();         // physical page allocator
    kvminit();       // create kernel page table
    kvminithart();   // turn on paging
    procinit();      // process table
    trapinit();      // trap vectors
    trapinithart();  // install kernel trap vector
    hal_irq_init();      // set up interrupt controller
    hal_irq_hart_init();  // ask PLIC for device interrupts
    binit();         // buffer cache
    iinit();         // inode table
    fileinit();      // file table
    virtio_disk_init(); // emulated hard disk
    hal_timer_init();   // start timer after all init
    userinit();      // first user process
    printf("main: userinit done, starting scheduler\n");
    __sync_synchronize();
    started = 1;
  } else {
    while(started == 0)
      ;
    __sync_synchronize();
    printf("hart %d starting\n", cpuid());
    kvminithart();
    trapinithart();
    hal_irq_hart_init();
  }

  scheduler();
}
