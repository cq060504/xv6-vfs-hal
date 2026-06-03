# LoongArch64 user <-> kernel trap trampoline.
#
# When a user-space (PLV3) trap occurs:
#   Hardware saves: CRMD.PLV -> PRMD.PPLV, CRMD.IE -> PRMD.PIE
#                   PC -> ERA, CRMD.PLV=0, CRMD.IE=0, PC=EENTRY
#
# DMW0 (VSEG=0, PLV0=1) identity-maps all VA[63:60]=0 for kernel mode,
# so the kernel accesses physical addresses directly.  The trampoline is
# placed at its flash LMA (0x1C008000) so DMW0 reaches the real code.
#
# Trapframe access: KSave1 (CSR 0x31) holds the *kernel* virtual address
# of the current process's trapframe (p->trapframe).  This KVA is in the
# DMW0 identity-mapped window, so uservec/userret can read/write the
# trapframe without going through the user page table or the TRAPFRAME VA.
#
# KSave0 (CSR 0x30) is used to save/restore user a0 across the transition.

#include "arch.h"
#include "memlayout.h"

.section trampsec
.globl trampoline
.globl usertrap

trampoline:
.align 4
.globl uservec
uservec:
        # Swap user a0 with KSave0; then load trapframe KVA from KSave1.
        csrwr   $a0, 0x30              # KSave0=user_a0 (a0 gets old KSave0, ignored)
        csrrd   $a0, 0x31              # a0 = KSave1 = p->trapframe (KVA, DMW0-accessible)

        # Save all user registers to trapframe via KVA (DMW0 identity-mapped).
        # Offsets match struct trapframe in proc.h.
        st.d    $ra, $a0, 40
        st.d    $sp, $a0, 48
        st.d    $r0, $a0, 56           # gp slot -- LoongArch has no gp
        st.d    $tp, $a0, 64
        st.d    $t0, $a0, 72
        st.d    $t1, $a0, 80
        st.d    $t2, $a0, 88
        st.d    $s0, $a0, 96
        st.d    $s1, $a0, 104
        st.d    $a1, $a0, 120
        st.d    $a2, $a0, 128
        st.d    $a3, $a0, 136
        st.d    $a4, $a0, 144
        st.d    $a5, $a0, 152
        st.d    $a6, $a0, 160
        st.d    $a7, $a0, 168
        st.d    $s2, $a0, 176
        st.d    $s3, $a0, 184
        st.d    $s4, $a0, 192
        st.d    $s5, $a0, 200
        st.d    $s6, $a0, 208
        st.d    $s7, $a0, 216
        st.d    $s8, $a0, 224
        st.d    $fp, $a0, 232          # fp = $r22 (s9 slot)
        st.d    $r0, $a0, 240          # s10 slot -- no LA equivalent
        st.d    $r0, $a0, 248          # s11 slot
        st.d    $t3, $a0, 256
        st.d    $t4, $a0, 264
        st.d    $t5, $a0, 272
        st.d    $t6, $a0, 280

        # Recover user a0 from KSave0 and store to trapframe->a0.
        csrrd   $t0, 0x30
        st.d    $t0, $a0, 112

        # Save user pc (ERA) to trapframe->epc.
        csrrd   $t0, 0x6               # ERA
        st.d    $t0, $a0, 24

        # Load kernel stack pointer from trapframe.
        ld.d    $sp, $a0, 8

        # Load kernel hartid into tp.
        ld.d    $tp, $a0, 32

        # Load address of usertrap() handler.
        ld.d    $t0, $a0, 16

        # Load kernel page table base.
        ld.d    $t1, $a0, 0

        # Switch to kernel page table (no invtlb — kernel uses DMW, not TLB).
        csrwr   $t1, 0x19              # PGDL = kernel page table

        # Call usertrap().  Use $ra so usertrap() can return to userret.
        jirl    $ra, $t0, 0

.globl userret
userret:
        # usertrap() returns here with the user page table in a0.

        # Switch to user page table.
        csrwr   $a0, 0x19              # PGDL = user page table
        invtlb  0, $r0, $r0

        # Load trapframe KVA from KSave1 (DMW0-accessible, per-process).
        csrrd   $a0, 0x31              # a0 = p->trapframe KVA

        # Restore all user registers from trapframe.
        # a0 is the base pointer, restored LAST.
        ld.d    $ra, $a0, 40
        ld.d    $sp, $a0, 48
        # skip gp -- LoongArch has no gp
        ld.d    $tp, $a0, 64
        ld.d    $t0, $a0, 72
        ld.d    $t1, $a0, 80
        ld.d    $t2, $a0, 88
        ld.d    $s0, $a0, 96
        ld.d    $s1, $a0, 104
        ld.d    $a1, $a0, 120
        ld.d    $a2, $a0, 128
        ld.d    $a3, $a0, 136
        ld.d    $a4, $a0, 144
        ld.d    $a5, $a0, 152
        ld.d    $a6, $a0, 160
        ld.d    $a7, $a0, 168
        ld.d    $s2, $a0, 176
        ld.d    $s3, $a0, 184
        ld.d    $s4, $a0, 192
        ld.d    $s5, $a0, 200
        ld.d    $s6, $a0, 208
        ld.d    $s7, $a0, 216
        ld.d    $s8, $a0, 224
        ld.d    $fp, $a0, 232
        ld.d    $t3, $a0, 256
        ld.d    $t4, $a0, 264
        ld.d    $t5, $a0, 272
        ld.d    $t6, $a0, 280

        # Restore user a0 last (it was the base pointer).
        ld.d    $a0, $a0, 112

        # ERA and PRMD already set by prepare_return() in trap.c.
        # ertn uses ERA for the return PC and PRMD.PPLV/PIE for
        # mode/interrupt configuration.
        ertn
