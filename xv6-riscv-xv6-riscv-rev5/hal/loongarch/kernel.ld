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
  }

  /* TLB refill and trampoline live at fixed pages used by memlayout.h. */
  . = 0x1c008000;
  .tlbrefill : {
    *(.tlbrefill)
  }

  . = 0x1c009000;
  .trampoline : {
    _trampoline = .;
    *(trampsec)
    . = ALIGN(0x1000);
    ASSERT(. - _trampoline == 0x1000, "error: trampoline larger than one page");
  }

  . = 0x1c00a000;
  .text.rest : {
    *(.text .text.*)
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

  .bootstack (NOLOAD) : {
    . = ALIGN(16);
    *(.bootstack)
    . = ALIGN(4096);
  }

  PROVIDE(end = .);
}
