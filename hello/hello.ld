/*
 *  hello.ld -- the test program at 0x80030000 (kseg0), where Ultrix and
 *  NetBSD kernels start: the DEC PROM keeps its own data below that.
 */

OUTPUT_FORMAT("elf32-tradlittlemips")
OUTPUT_ARCH(mips)
ENTRY(start)

SECTIONS
{
	. = 0x80030000;
	.text	: { *(.text) }
	.rodata	: { *(.rodata*) }
	.data	: { *(.data) }
	. = ALIGN(16);
	_fbss = .;
	.bss	: { *(.bss) *(COMMON) }
	. = ALIGN(16);
	_end = .;

	/DISCARD/ : { *(.reginfo) *(.MIPS.abiflags) *(.pdr) *(.gnu.attributes) *(.comment) }
}
