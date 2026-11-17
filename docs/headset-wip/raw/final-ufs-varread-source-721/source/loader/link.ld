ENTRY(_start)

SECTIONS
{
    /*
     * 0x40000000, not 0x40080000: LK on this board ignores text_offset in the Image header and
     * places the program at a fixed 0x40000000, so the link map has to agree with that or every
     * adrp lands 512 KiB away from the data it means to touch.  The relocation in start.S stays
     * as a safety net for boards that do honour text_offset; here it simply finds nothing to move.
     */
    . = 0x40000000;

    .text : {
        *(.text.boot)
        *(.text .text.*)
    }

    .rodata : {
        *(.rodata .rodata.*)
    }

    .data : {
        *(.data .data.*)
    }

    .bss : {
        __bss_start = .;
        *(.bss .bss.*)
        *(COMMON)
        __bss_end = .;
    }

    . = ALIGN(16);
    __loader_end = .;
}
