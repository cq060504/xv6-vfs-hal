# LoongArch64 kernel-mode trap vector.
#
# Interrupts and exceptions while in PLV0 (kernel mode) come here.
#
# On entry, the CPU has saved: CRMD.PLV→PRMD.PPLV, CRMD.IE→PRMD.PIE,
# PC→ERA, and set CRMD.PLV=0, CRMD.IE=0, PC=EENTRY.
#
# Before modifying sp, we detect kernel-stack guard faults by checking
# ESTAT.Ecode (PIL=1/PIS=2) against BADV in [KSTACK_REGION_BOTTOM,
# KSTACK_TOP).  A guard fault switches to an emergency per-CPU stack
# and calls hal_stack_overflow_panic(), which does not return.

.globl kerneltrap
.globl kernelvec
.align 4
kernelvec:
        # ---- Phase 0: save $t0/$t1 so we can use them for detection ----
        csrwr   $t0, 0x33              # KSave3 = old $t0
        csrwr   $t1, 0x34              # KSave4 = old $t1

        # ---- Phase 1: read exception code from ESTAT ----
        csrrd   $t0, 0x5               # $t0 = ESTAT
        srli.d  $t0, $t0, 16           # shift right to Ecode field
        andi    $t0, $t0, 0x3F         # mask Ecode (6 bits)

        # Only PIL (1) and PIS (2) could be stack guard faults.
        ori     $t1, $r0, 1            # $t1 = 1 = EXCCODE_PIL
        beq     $t0, $t1, .Lchk_badv
        ori     $t1, $r0, 2            # $t1 = 2 = EXCCODE_PIS
        beq     $t0, $t1, .Lchk_badv
        b       .Lnormal               # timer / device / other → normal path

.Lchk_badv:
        # ---- Phase 2: check if BADV falls in kernel-stack region ----
        # Valid kernel stacks: [KSTACK_REGION_BOTTOM, KSTACK_TOP)
        #   KSTACK_REGION_BOTTOM = 0xFFFFFFFFFFF3F000
        #   KSTACK_TOP          = 0xFFFFFFFFFFFFF000
        csrrd   $t0, 0x7               # $t0 = BADV
        lu12i.w $t1, -193              # $t1 = KSTACK_REGION_BOTTOM
        bltu    $t0, $t1, .Lnormal     # BADV < bottom → not a guard fault
        lu12i.w $t1, -1               # $t1 = KSTACK_TOP
        bgeu    $t0, $t1, .Lnormal     # BADV >= top → not a guard fault

        # ---- Stack overflow confirmed! ----
        # $t0 = BADV (faulting address).  Switch to emergency stack.
        csrwr   $sp, 0x34              # KSave4 = bad $sp (overwrites saved $t1)

        # Compute emergency stack top: &stack0[cpuid + 1] (one page per CPU)
        csrrd   $t1, 0x20              # $t1 = CPUID
        addi.d  $t1, $t1, 1            # $t1 = CPUID + 1
        slli.d  $t1, $t1, 12           # $t1 = (CPUID+1) * PGSIZE
        la.abs  $sp, stack0            # $sp = &stack0[0]
        add.d   $sp, $sp, $t1          # $sp = top of emergency stack

        # a0 = original sp (from KSave4), a1 = BADV (still in $t0)
        csrrd   $a0, 0x34              # $a0 = bad sp
        or      $a1, $t0, $r0          # $a1 = BADV

        la.abs  $t0, hal_stack_overflow_panic
        jirl    $ra, $t0, 0
        # hal_stack_overflow_panic does not return.

.Lnormal:
        # ---- Normal path: restore $t0/$t1 and handle the trap ----
        csrwr   $t0, 0x33              # restore $t0 from KSave3
        csrwr   $t1, 0x34              # restore $t1 from KSave4

        # make room to save registers (17 regs x 8 bytes + padding)
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
