# LoongArch software TLB refill handler.
#
# Invoked on TLB miss when PG=1, DA=0.  DMW0 covers PLV0 with
# VA[63:60]=0, so this handler only fires for PLV3 user-space
# accesses (or accesses with VA[63:60]!=0, none in our 39-bit space).
#
# CRITICAL: In TLB refill context (IsTLBR=1), the CPU uses
#   TLBREHI / TLBRELO0 / TLBRELO1  (NOT TLBEHI/TLBELO0/1!).
#   TLBREHI.PS is NOT auto-filled — software MUST set it to 12 (4KB).
#   Use tlbfill (random slot), not tlbwr (indexed).
#
# DMW0 identity-maps all low PA, so page-table loads from PGDL
# cannot cause nested TLB misses.

# .tlbrefill section — placed on its own 4KB-aligned page in kernel.ld,
# outside the main .text execution flow to prevent accidental execution.
.section .tlbrefill, "ax"
.balign 4096
.globl tlb_refill_entry
tlb_refill_entry:
        # --- Step 1: Read VA and page table root ---
        csrrd   $t0, 0x7               # t0 = BADV (faulting VA)
        csrrd   $t1, 0x19              # t1 = PGDL (page table PA)

        # --- Step 2: Level 2 walk ---
        srli.d  $t2, $t0, 30           # PX(2) = (VA >> 30) & 0x1FF
        andi    $t2, $t2, 0x1FF
        slli.d  $t2, $t2, 3
        add.d   $t2, $t1, $t2          # &pte = PGDL + PX(2)*8
        ld.d    $t2, $t2, 0            # t2 = L2 PTE
        andi    $t3, $t2, 1            # check V
        beqz    $t3, .Lfail

        # Extract L1 table PA: PTE[59:12] = PA[59:12]
        srli.d  $t1, $t2, 12
        slli.d  $t1, $t1, 12           # t1 = L1 PA

        # --- Step 3: Level 1 walk ---
        srli.d  $t2, $t0, 21           # PX(1) = (VA >> 21) & 0x1FF
        andi    $t2, $t2, 0x1FF
        slli.d  $t2, $t2, 3
        add.d   $t2, $t1, $t2
        ld.d    $t2, $t2, 0            # t2 = L1 PTE
        andi    $t3, $t2, 1
        beqz    $t3, .Lfail

        # Extract L0 table PA
        srli.d  $t1, $t2, 12
        slli.d  $t1, $t1, 12           # t1 = L0 PA

        # --- Step 4: Level 0 walk (leaf) ---
        srli.d  $t2, $t0, 12           # PX(0) = (VA >> 12) & 0x1FF
        andi    $t2, $t2, 0x1FF
        slli.d  $t2, $t2, 3
        add.d   $t2, $t1, $t2
        ld.d    $t2, $t2, 0            # t2 = leaf PTE
        andi    $t3, $t2, 1
        beqz    $t3, .Lfail

        # --- Step 5: Set TLBREHI.PS = 12 (4KB) ---
        # TLBREHI.VPPN is auto-filled by hardware from the faulting VA.
        # But TLBREHI.PS[5:0] must be set by software (not auto-filled!).
        csrrd   $t3, 0x11              # read TLBREHI (= TLBEHI reg, but in refill context)
        srli.d  $t3, $t3, 6            # clear bits [5:0]
        slli.d  $t3, $t3, 6            # VPPN preserved, PS=0
        ori     $t3, $t3, 12           # PS = 12 (4KB page)
        csrwr   $t3, 0x11              # write back TLBREHI

        # --- Step 6: Fill TLB ---
        # Clear TLBRELO1 (odd page invalid), write leaf PTE to TLBRELO0.
        # Note: TLBELO bits [11:7] are reserved (always 0).  Our PTE may
        # have PPN0 there from PA2PTE, but for PA<4GB, PTE[11:7]=0.
        # The TLB hardware ignores these bits, so no masking needed.
        csrwr   $r0, 0x13              # TLBRELO1 = 0 (odd page invalid)
        csrwr   $t2, 0x12              # TLBRELO0 = leaf PTE
        tlbfill                         # fill random TLB slot

        # --- Step 7: Return ---
        # ertn with IsTLBR=1: restores PLV/IE from TLBRPRMD, PC from TLBRERA,
        # clears DA, sets PG=1, and then clears IsTLBR.
        ertn

.Lfail:
        # Walk failed: write both entries invalid so the re-execute
        # triggers a normal TLB exception (PIL/PIS/PIF) through EENTRY.
        csrwr   $r0, 0x13
        csrwr   $r0, 0x12
        tlbfill
        ertn
