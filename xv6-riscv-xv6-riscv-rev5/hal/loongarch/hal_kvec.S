# LoongArch64 kernel-mode trap vector.
#
# Interrupts and exceptions while in PLV0 (kernel mode) come here.
# Save caller-saved registers on the current kernel stack,
# call kerneltrap(), restore registers, and return via ertn.

.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
        # make room to save registers (17 regs × 8 bytes + padding)
        addi.d  $sp, $sp, -160

        # save caller-saved registers
        st.d    $ra, $sp, 0
        st.d    $tp, $sp, 8
        st.d    $a0, $sp, 16
        st.d    $a1, $sp, 24
        st.d    $a2, $sp, 32
        st.d    $a3, $sp, 40
        st.d    $a4, $sp, 48
        st.d    $a5, $sp, 56
        st.d    $a6, $sp, 64
        st.d    $a7, $sp, 72
        st.d    $t0, $sp, 80
        st.d    $t1, $sp, 88
        st.d    $t2, $sp, 96
        st.d    $t3, $sp, 104
        st.d    $t4, $sp, 112
        st.d    $t5, $sp, 120
        st.d    $t6, $sp, 128
        st.d    $t7, $sp, 136
        st.d    $t8, $sp, 144

        # call the C trap handler using long-range call
        la.abs  $t0, kerneltrap
        jirl    $ra, $t0, 0

        # restore registers
        ld.d    $ra, $sp, 0
        # tp may have changed if we moved CPUs
        ld.d    $a0, $sp, 16
        ld.d    $a1, $sp, 24
        ld.d    $a2, $sp, 32
        ld.d    $a3, $sp, 40
        ld.d    $a4, $sp, 48
        ld.d    $a5, $sp, 56
        ld.d    $a6, $sp, 64
        ld.d    $a7, $sp, 72
        ld.d    $t0, $sp, 80
        ld.d    $t1, $sp, 88
        ld.d    $t2, $sp, 96
        ld.d    $t3, $sp, 104
        ld.d    $t4, $sp, 112
        ld.d    $t5, $sp, 120
        ld.d    $t6, $sp, 128
        ld.d    $t7, $sp, 136
        ld.d    $t8, $sp, 144

        addi.d  $sp, $sp, 160

        # return from exception (ertn = LoongArch sret)
        ertn
