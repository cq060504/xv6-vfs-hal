# qemu -bios loads the kernel at 0x1c000000
# and causes each CPU core to jump there.
# kernel.ld places .text.entry at 0x1c000000.
.section .text.entry
.global _entry
_entry:
        # Set up a stack for C.
        # sp = stack0 + ((cpuid + 1) * 4096)
        la.abs  $sp, stack0
        li.d    $t0, 4096
        csrrd   $t1, 0x20               # CPUID CSR
        addi.d  $t1, $t1, 1
        mul.d   $t0, $t0, $t1
        add.d   $sp, $sp, $t0
        # jump to start() in hal_start.c
        bl      start
spin:
        b       spin
