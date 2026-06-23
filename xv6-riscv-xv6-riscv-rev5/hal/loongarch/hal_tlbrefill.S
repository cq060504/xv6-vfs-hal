# LoongArch software TLB refill handler.
#
# The CPU enters here through CSR.TLBRENTRY with DA=1/PG=0, so low
# physical addresses are directly reachable. The page-table layout is the
# four-level 9-9-9-9 scheme configured in kvminithart() via PWCL/PWCH.

.section .tlbrefill, "ax"
.balign 4096
.globl tlb_refill_entry
tlb_refill_entry:
        # Use TLBRSAVE as the single scratch save slot; do not touch ERA/PRMD.
        csrwr   $t0, 0x8b              # TLBRSAVE

        # TLBREHI.VPPN is set by hardware, but PS is software-owned.
        csrrd   $t0, 0x8e              # TLBREHI
        srli.d  $t0, $t0, 6
        slli.d  $t0, $t0, 6
        ori     $t0, $t0, 12
        csrwr   $t0, 0x8e

        # Reject non-user VAs before lddir can truncate them to 48 bits.
        csrrd   $t0, 0x89              # TLBRBADV
        srli.d  $t0, $t0, 38           # MAXVA is 1 << 38
        bnez    $t0, .Ltlbr_invalid
        csrrd   $t0, 0x89
        srli.d  $t0, $t0, 13           # reject VA < 2*PGSIZE
        beqz    $t0, .Ltlbr_invalid

        csrrd   $t0, 0x19              # PGDL: root page-table physical base

        lddir   $t0, $t0, 3            # Dir2, VA[47:39]
        andi    $t0, $t0, 1
        beqz    $t0, .Ltlbr_invalid
        csrrd   $t0, 0x19
        lddir   $t0, $t0, 3
        addi.d  $t0, $t0, -1

        lddir   $t0, $t0, 2            # Dir1, VA[38:30]
        andi    $t0, $t0, 1
        beqz    $t0, .Ltlbr_invalid
        csrrd   $t0, 0x19
        lddir   $t0, $t0, 3
        addi.d  $t0, $t0, -1
        lddir   $t0, $t0, 2
        addi.d  $t0, $t0, -1

        lddir   $t0, $t0, 1            # Dir0, VA[29:21]
        andi    $t0, $t0, 1
        beqz    $t0, .Ltlbr_invalid
        csrrd   $t0, 0x19
        lddir   $t0, $t0, 3
        addi.d  $t0, $t0, -1
        lddir   $t0, $t0, 2
        addi.d  $t0, $t0, -1
        lddir   $t0, $t0, 1
        addi.d  $t0, $t0, -1

        ldpte   $t0, 0                 # even page -> TLBRELO0
        ldpte   $t0, 1                 # odd page  -> TLBRELO1
        tlbfill

        csrrd   $t0, 0x8b
        ertn

.Ltlbr_invalid:
        # Install an invalid matching entry so retry reaches the normal
        # PIL/PIS/PIF path instead of looping forever in TLB refill.
        csrwr   $r0, 0x8c              # TLBRELO0
        csrwr   $r0, 0x8d              # TLBRELO1
        tlbfill
        csrrd   $t0, 0x8b
        ertn
