# Minimal LoongArch boot test — outputs "LA" to UART, then loops
# UART0 = 0x1FE001E0 on QEMU LoongArch virt

.section .text.boot
.globl _start
_start:
    # Load UART0 base address
    li.d    $t0, 0x1FE001E0

    # Output 'L' (0x4C)
    li.d    $t1, 0x4C
    st.b    $t1, $t0, 0

    # Output 'A' (0x41)
    li.d    $t1, 0x41
    st.b    $t1, $t0, 0

    # Output '\n' (0x0A)
    li.d    $t1, 0x0A
    st.b    $t1, $t0, 0

    # Infinite loop
1:  b       1b
