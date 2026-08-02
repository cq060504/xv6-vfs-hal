// LoongArch physical memory layout for qemu -machine virt (LS7A chipset).
//
// Based on QEMU hw/loongarch/virt.c:
//
// 00000000 -- Low RAM (256 MB for xv6)
// 0FE00000 -- EIOINTC (extended I/O interrupt controller)
// 1FE001E0 -- UART0 (COM1, 16550a)
// 1C000000 -- Firmware/BIOS flash (kernel loaded here via -bios)
//
// DMW0 (VSEG=0, PLV=PLV0) identity-maps VA[63:60]=0 -> PA=VA for kernel
// direct access to: .data/.bss (0x00400000), .text/flash (0x1c000000),
// UART, EIOINTC, and the trampoline page.
//
// Kernel stacks live at high VA (VA[63]=1) outside DMW0, accessed through
// PGDH page table. This enables real guard pages: a stack overflow moves sp
// into an unmapped page and triggers a PIL/PIS exception instead of silently
// corrupting the next process's stack through the DMW0 alias.
// Trapframe is accessed via KSave1 (kernel VA), not via TRAPFRAME VA.

#ifndef _HAL_LOONGARCH_MEMLAYOUT_H_
#define _HAL_LOONGARCH_MEMLAYOUT_H_

// QEMU puts 16550a UART registers here in physical memory.
// UART0_IRQ: serial irq goes to PCH-PIC input 2 (VIRT_UART_IRQ(66)-VIRT_GSI_BASE(64)),
// but PCH-PIC htmsi_vector[] resets to all zeros, so every input is routed to
// PCH-PIC output 0 = EIOINTC line 0.  Hence UART0_IRQ is EIOINTC line 0.
#define UART0 0x1FE001E0L
#define UART0_IRQ 0

// virtio on LoongArch is PCI-based, not MMIO.
#define VIRTIO0_IRQ 32

// Extended I/O Interrupt Controller base address.
#define EIOINTC 0x0FE00000L

// Kernel code in flash at VMA 0x1c000000. Kernel data in RAM at 0x00400000.
// PHYSTOP = start of kernel data in RAM + 124MB usable RAM.
#define KERNBASE 0x1C000000L
#define PHYSTOP (0x00400000L + 124*1024*1024)

// QEMU loader-backed RAM disks. This region is above PHYSTOP and the kernel
// stacks, so kalloc cannot overwrite it. Three 16 MiB windows end below the
// 256 MiB low-RAM boundary.
#define RAMDISK_BASE   0x09000000L
#define RAMDISK_STRIDE (16*1024*1024L)

// Low-address objects placed where DMW0 (VA[63:60]=0) reaches them directly:
//   TRAMPOLINE = 0x1C009000  (_trampoline symbol in flash, after .tlbrefill page)
//   TRAPFRAME  —  per-process, accessed via KSave1 holding p->trapframe KVA;
//                 keeps the xv6 high user-VA value for sbrk/usertests only.
//
// TRAMPOLINE is the actual low VA used by EENTRY/userret. TRAPFRAME keeps the
// original xv6 high user-VA contract for sbrk/usertests only; LoongArch never
// maps or dereferences that VA on the trap path.
//
// Kernel stacks are at high addresses (KSTACK_TOP downwards), outside DMW0.
// Each process gets 3 pages: 2 mapped for the stack + 1 unmapped guard below.
// The guard has no PTE → true stack overflow → PIL/PIS exception.
#define TRAMPOLINE 0x1C009000L
#define TRAPFRAME  (MAXVA - 2*PGSIZE)

// One byte past the largest user address that sbrk may create.
// RISC-V uses TRAPFRAME as this guard; keep the same contract for tests.
#define USER_TOP TRAPFRAME


// Kernel stacks: descending from high VA, outside DMW0.
// 3*PGSIZE per process (2 mapped + 1 unmapped guard). NPROC=64.
//
// Layout for process p (top→bottom):
//   KSTACK(p) + 2*PGSIZE  ← initial sp (stack grows downward)
//   KSTACK(p) + 1*PGSIZE  ← second usable page
//   KSTACK(p)             ← first usable page (mapped, PTE_R|PTE_W)
//   KSTACK(p) - 1*PGSIZE  ← guard page (NO PTE → PIL/PIS on overflow)
//
// Constraints verified for NPROC=64, PGSIZE=4096:
//   KSTACK(p) % PGSIZE == 0  (page-aligned)
//   KSTACK(p) + 2*PGSIZE <= KSTACK_TOP  (fits in window)
//   KSTACK(NPROC-1) - PGSIZE >= KSTACK_REGION_BOTTOM  (last guard in range)
#define KSTACK_TOP            0xFFFFFFFFFFFFF000ULL
#define KSTACK(p)             (KSTACK_TOP - ((uint64)(p) * 3 + 2) * PGSIZE)
// NPROC = 64 → region spans 64 * 3 = 192 pages (128 mapped + 64 guard)
#define KSTACK_REGION_BOTTOM  (KSTACK_TOP - 192 * PGSIZE)

#ifndef __ASSEMBLER__
// --- Page table walk address validation ---
// Returns 1 if va is a legal address for the page-table walker:
// user space [0, MAXVA) or high kernel stack region.
static inline int hal_pagetable_va_valid(uint64 va) {
    return (va < MAXVA) || (va >= KSTACK_REGION_BOTTOM && va < KSTACK_TOP);
}
#endif

#endif
