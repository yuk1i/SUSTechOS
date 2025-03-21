OUTPUT_ARCH(riscv)
ENTRY(__start_main)

PHDRS
{
  text      PT_LOAD FLAGS (5); /* 0x5 -> RX */
  rodata    PT_LOAD FLAGS (4); /* 0x4 -> RO */
  data      PT_LOAD FLAGS (6); /* 0x6 -> RW */
}

SECTIONS {

    . = 0x402000;
    .text : { 
        *(.text.entry)
        *(.text .text.*)
    } : text

    . = ALIGN(4K);

    .rodata : {
        *(.rodata .rodata.*) 
        *(.srodata .srodata.*) 
    } : rodata
    
    . = ALIGN(4K);

    .data : { 
        *(.data) 
        *(.sdata .sdata.*)
    } : data

    .bss : { 
        *(.bss .bss.*)
        *(.sbss .sbss.*)
    } : data
    
    /DISCARD/ : { *(.eh_*) }
}