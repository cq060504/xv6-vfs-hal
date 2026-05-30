OUTPUT_ARCH("loongarch")
ENTRY(_entry)

SECTIONS
{
  /*
   * Simple layout: everything at VMA 0x1c000000.
   * .data and .bss have AT() in flash, start() copies them to RAM at 0x00400000.
   */
  . = 0x1c000000;

  .text : {
    *(.text.entry)
    *hal_entry.o(.text)
    *hal_start.o(.text)
    *hal_uart.o(.text)
    *(.text .text.*)

    /* TLB refill handler on its own page (4KB-aligned, not in execution flow) */
    . = ALIGN(0x1000);
    *(.tlbrefill)

    /* Trampoline page */
    . = ALIGN(0x1000);
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
    PROVIDE(etext = .);
  }

  .rodata : { *(.rodata .rodata.*) }
  .eh_frame : { *(.eh_frame) }

  /* .data LMA follows eh_frame. VMA goes to RAM. */
  _data_lma = ALIGN(4096);

  . = 0x00400000;
  .data : AT(_data_lma) {
    _data_start = .;
    *(.data .data.*) *(.got .got.*)
    . = ALIGN(16);
  }

  .bss (NOLOAD) : {
    . = ALIGN(4096);
    _bss_start = .;
    *(.bss .bss.*)
    . = ALIGN(4096);
    _bss_end = .;
  }

  PROVIDE(end = .);
}
