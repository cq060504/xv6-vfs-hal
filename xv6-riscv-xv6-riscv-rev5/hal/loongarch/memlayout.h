// LoongArch physical memory layout for qemu -machine virt (LS7A chipset).
//
// Based on QEMU hw/loongarch/virt.c:
//
// 00000000 -- Low RAM (256 MB for xv6)
// 0FE00000 -- EIOINTC (extended I/O interrupt controller)
// 1FE001E0 -- UART0 (COM1, 16550a)
// 1C000000 -- Firmware/BIOS flash (kernel loaded here via -bios)
//
// DMW0 maps VA[47:36]=0 -> PA[47:36]=0 for kernel direct access.
// Kernel .data/.bss at VMA 0x00400000 (low RAM).
// Kernel .text at VMA 0x1c000000 (flash).
//
// TRAMPOLINE/TRAPFRAME/KSTACK go at the top of the virtual address space,
// following the RISC-V layout pattern. This avoids collisions with the
// identity-mapped free RAM range.

#ifndef _HAL_LOONGARCH_MEMLAYOUT_H_
#define _HAL_LOONGARCH_MEMLAYOUT_H_

// QEMU puts 16550a UART registers here in physical memory.
#define UART0 0x1FE001E0L
#define UART0_IRQ 31

// virtio on LoongArch is PCI-based, not MMIO.
// VIRTIO0 provides the base for compatibility; actual access goes through PCI.
#define VIRTIO0 0x10000000L
#define VIRTIO0_IRQ 32

// Extended I/O Interrupt Controller base address.
#define EIOINTC 0x0FE00000L
// PLIC alias for vm.c compatibility (maps PLIC region = EIOINTC region).
#define PLIC EIOINTC
#define PLIC_PRIORITY (PLIC + 0x0)
#define PLIC_PENDING (PLIC + 0x1000)

// Kernel code in flash at VMA 0x1c000000. Kernel data in RAM at 0x00400000.
// PHYSTOP = start of kernel data in RAM + 124MB usable RAM.
#define KERNBASE 0x1C000000L
#define PHYSTOP (0x00400000L + 124*1024*1024)

// LoongArch DMW0 (VSEG=0, PLV=PLV0) identity-maps ALL VA[63:60]=0 → PA=VA.
// This covers our entire 39-bit VA space. We exploit this by placing kernel
// objects at physical addresses where they actually exist:
//
//   TRAMPOLINE = 0x1C009000  (_trampoline symbol in flash, after .tlbrefill page)
//   KSTACK     = 0x08000000+ (low RAM above PHYSTOP, unused by kalloc)
//   TRAPFRAME  —  per-process, accessed via KSave1 holding p->trapframe KVA
//
// TRAPFRAME (user VA) only matters for user-mode (PLV3) page-table lookups.
// Kernel-mode (PLV0) trapframe access uses the direct-mapped KVA from KSave1,
// never the TRAPFRAME VA, so DMW0 identity-mapping of TRAPFRAME VA is harmless.
#define TRAMPOLINE 0x1C009000L
#define TRAPFRAME  0x1C007000L

// Kernel stacks: placed at physical addresses with actual RAM (0x08000000+),
// below QEMU's 256MB low-RAM ceiling (0x10000000). DMW0 identity-maps VA=PA.
// 3*PGSIZE per process (2 usable + 1 guard). NPROC=64 → 768KB total.
#define KSTACK_BASE 0x08000000L
#define KSTACK(p) (KSTACK_BASE + (p)*3*PGSIZE)

#endif
