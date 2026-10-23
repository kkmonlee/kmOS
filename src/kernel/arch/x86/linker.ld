OUTPUT_FORMAT(elf32-i386)
OUTPUT_ARCH(i386)
ENTRY(_start)

SECTIONS {
    . = 0x00100000;
    kernel_start = .;
    .multiboot : { KEEP(*(.multiboot)) }
    .text ALIGN(0x1000) : { *(.text .text.*) }
    .rodata ALIGN(0x1000) : { *(.rodata .rodata.*) }
    .data ALIGN(0x1000) : {
        start_ctors = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        KEEP(*(.ctors))
        end_ctors = .;
        *(.data .data.*)
    }
    .tbss : { *(.tbss .tbss.*) }
    .bss ALIGN(0x1000) : {
        sbss = .;
        *(COMMON)
        *(.bss .bss.*)
        ebss = .;
    }
    kernel_end = ALIGN(0x1000);
    /DISCARD/ : { *(.eh_frame*) *(.comment) }
}
