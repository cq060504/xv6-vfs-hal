// Compatibility header — includes the architecture-specific HAL headers.
// User programs that still #include "kernel/riscv.h" will get the appropriate
// definitions (PGSIZE, KERNBASE, MAXVA, CSR inlines, etc.) for the target arch.
#include "hal/hal.h"
