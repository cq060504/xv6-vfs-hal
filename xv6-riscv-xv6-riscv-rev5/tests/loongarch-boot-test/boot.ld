OUTPUT_ARCH("loongarch")
ENTRY(_start)

SECTIONS
{
  . = 0x1c000000;
  .text : {
    *(.text.boot)
    *(.text)
  }
  .data : { *(.data) }
  .bss  : { *(.bss)  }
}
