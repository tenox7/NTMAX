/*
 *  hello.S -- first boot test for the DECstation 5000/200 (KN02, MIPS R3000)
 *
 *  The DEC PROM (or gxemul's PROM emulation) loads this program at
 *  0x80030000, from the DEC boot block of a disk or as an ELF file, and
 *  starts it with:
 *
 *	a0 = argc, a1 = argv, a2 = 0x30464354 (REX PROM magic),
 *	a3 = the PROM callback vector
 *
 *  Part 1 uses the PROM's services (printf, getsysid, getbitmap, getenv).
 *  Part 2 does not use the PROM: it drives the DZ (DC7085) serial chip
 *  directly, line 3 = the console port, and reads CP0.  Then it halts back
 *  to the PROM.
 *
 *  MIPS I: a load (and mfc0) has a delay slot, the next instruction still
 *  sees the old register.  In reorder mode the assembler fills the slots.
 */

	.set	mips1
	.set	reorder

	.equ	REX_MAGIC,	0x30464354

/*  PROM callback vector offsets (REX)  */
	.equ	CB_PRINTF,	0x30
	.equ	CB_GETENV,	0x64
	.equ	CB_GETSYSID,	0x80
	.equ	CB_GETBITMAP,	0x84
	.equ	CB_REX,		0xac			/* rex('h') = halt, as NetBSD does */

/*  KN02: the DC7085 at physical 0x1fe00000, used through kseg1 (uncached)  */
	.equ	DZ_BASE,	0xbfe00000
	.equ	DZ_CSR,		0x00
	.equ	DZ_LPR,		0x08
	.equ	DZ_TCR,		0x10
	.equ	DZ_TDR,		0x18
	.equ	CSR_TRDY,	0x8000
	.equ	CSR_MSE,	0x0020
	.equ	CONS_LINE,	3
	.equ	LPR_CONS,	0x1000 | 0x0e00 | 0x0018 | CONS_LINE	/* RX on, 9600, 8 bits */
	.equ	TCR_CONS,	0x0200 | 0x0100 | (1 << CONS_LINE)	/* RTS3, DTR3, line 3 on */

	.text

	.globl	start
	.ent	start
start:
	move	$s0, $a0			/* argc */
	move	$s1, $a1			/* argv */
	move	$s2, $a2			/* PROM magic */
	move	$s3, $a3			/* PROM callback vector */

	la	$t0, _fbss		/* clear .bss */
	la	$t1, _end
1:	sw	$zero, 0($t0)
	addiu	$t0, $t0, 4
	sltu	$t2, $t0, $t1
	bnez	$t2, 1b

	la	$sp, stack_top - 32	/* 16-byte argument area for callees */

	jal	dz_init
	la	$a0, s_hello
	jal	dz_puts

	li	$t0, REX_MAGIC
	bne	$s2, $t0, no_rex

/*  Part 1: the PROM's services  */

	la	$a0, s_prom
	move	$a1, $s3
	move	$a2, $s0
	lw	$t9, CB_PRINTF($s3)
	jalr	$t9

	lw	$t9, CB_GETSYSID($s3)
	jalr	$t9
	move	$s4, $v0			/* system id */

	la	$a0, memmap
	lw	$t9, CB_GETBITMAP($s3)
	jalr	$t9
	move	$s5, $v0			/* bitmap length in bytes */

	la	$a0, s_bootvar
	lw	$t9, CB_GETENV($s3)
	jalr	$t9
	move	$s6, $v0			/* the "boot" variable, or 0 */

/*  Part 2: straight to the hardware  */

	la	$a0, s_part2
	jal	dz_puts

	la	$a0, s_sysid
	jal	dz_puts
	move	$a0, $s4
	jal	dz_puthex
	jal	dz_crlf

	.set	noreorder
	mfc0	$s4, $15			/* PRId */
	mfc0	$s7, $12			/* Status */
	nop				/* mfc0 delay slot */
	.set	reorder

	la	$a0, s_prid
	jal	dz_puts
	move	$a0, $s4
	jal	dz_puthex
	la	$a0, s_status
	jal	dz_puts
	move	$a0, $s7
	jal	dz_puthex
	jal	dz_crlf

	la	$t0, memmap + 4		/* count the usable pages */
	addu	$t1, $t0, $s5
	move	$s7, $zero
2:	beq	$t0, $t1, 4f
	lbu	$t2, 0($t0)
	addiu	$t0, $t0, 1
3:	beqz	$t2, 2b
	andi	$t3, $t2, 1
	addu	$s7, $s7, $t3
	srl	$t2, $t2, 1
	b	3b
4:	la	$a0, s_mem
	jal	dz_puts
	lw	$t0, memmap		/* bytes = pages * page size */
	multu	$s7, $t0
	mflo	$a0
	srl	$a0, $a0, 20
	jal	dz_putdec
	la	$a0, s_memmb
	jal	dz_puts
	move	$a0, $s7
	jal	dz_putdec
	la	$a0, s_pages
	jal	dz_puts
	lw	$a0, memmap
	jal	dz_putdec
	la	$a0, s_pagesz
	jal	dz_puts

	la	$a0, s_args		/* the arguments the PROM passed */
	jal	dz_puts
	move	$a0, $s0
	jal	dz_putdec
	jal	dz_crlf
	move	$s4, $zero
5:	beq	$s4, $s0, 6f
	la	$a0, s_arg
	jal	dz_puts
	move	$a0, $s4
	jal	dz_putdec
	la	$a0, s_argeq
	jal	dz_puts
	sll	$t0, $s4, 2
	addu	$t0, $t0, $s1
	lw	$a0, 0($t0)
	jal	dz_puts
	jal	dz_crlf
	addiu	$s4, $s4, 1
	b	5b
6:	la	$a0, s_boot
	jal	dz_puts
	beqz	$s6, 7f
	move	$a0, $s6
	jal	dz_puts
7:	jal	dz_crlf

	la	$a0, s_bye
	jal	dz_puts
	li	$a0, 'h'
	lw	$t9, CB_REX($s3)
	jalr	$t9
8:	b	8b

no_rex:
	la	$a0, s_norex
	jal	dz_puts
	b	8b
	.end	start


/*  dz_init: line 3 at 9600 bits/s, 8 bits, receiver on; its transmitter on  */
	.ent	dz_init
dz_init:
	li	$t0, DZ_BASE
	li	$t1, LPR_CONS
	sh	$t1, DZ_LPR($t0)
	li	$t1, TCR_CONS
	sh	$t1, DZ_TCR($t0)
	li	$t1, CSR_MSE
	sh	$t1, DZ_CSR($t0)
	jr	$ra
	.end	dz_init


/*  dz_putc(a0): wait until the DZ may send on line 3, then send a0  */
	.ent	dz_putc
dz_putc:
	li	$t0, DZ_BASE
1:	lhu	$t1, DZ_CSR($t0)
	andi	$t2, $t1, CSR_TRDY
	beqz	$t2, 1b
	srl	$t2, $t1, 8		/* the line that is ready */
	andi	$t2, $t2, 3
	li	$t3, CONS_LINE
	beq	$t2, $t3, 2f
	li	$t3, 1			/* another line: switch it off */
	sllv	$t3, $t3, $t2
	nor	$t3, $t3, $zero
	lhu	$t4, DZ_TCR($t0)
	and	$t4, $t4, $t3
	sh	$t4, DZ_TCR($t0)
	b	1b
2:	sh	$a0, DZ_TDR($t0)
	jr	$ra
	.end	dz_putc


/*  dz_puts(a0): a string; "\n" goes out as "\r\n"  */
	.ent	dz_puts
dz_puts:
	addiu	$sp, $sp, -24
	sw	$ra, 20($sp)
	sw	$s0, 16($sp)
	move	$s0, $a0
1:	lbu	$a0, 0($s0)
	beqz	$a0, 3f
	addiu	$s0, $s0, 1
	li	$t0, 10
	bne	$a0, $t0, 2f
	li	$a0, 13
	jal	dz_putc
	li	$a0, 10
2:	jal	dz_putc
	b	1b
3:	lw	$s0, 16($sp)
	lw	$ra, 20($sp)
	addiu	$sp, $sp, 24
	jr	$ra
	.end	dz_puts


/*  dz_crlf: end the line  */
	.ent	dz_crlf
dz_crlf:
	la	$a0, s_nl
	b	dz_puts
	.end	dz_crlf


/*  dz_puthex(a0): 8 hex digits  */
	.ent	dz_puthex
dz_puthex:
	addiu	$sp, $sp, -32
	sw	$ra, 28($sp)
	sw	$s0, 24($sp)
	sw	$s1, 20($sp)
	move	$s0, $a0
	li	$s1, 8
1:	srl	$t0, $s0, 28
	sll	$s0, $s0, 4
	la	$t1, hexdigits
	addu	$t1, $t1, $t0
	lbu	$a0, 0($t1)
	jal	dz_putc
	addiu	$s1, $s1, -1
	bnez	$s1, 1b
	lw	$s1, 20($sp)
	lw	$s0, 24($sp)
	lw	$ra, 28($sp)
	addiu	$sp, $sp, 32
	jr	$ra
	.end	dz_puthex


/*  dz_putdec(a0): unsigned decimal  */
	.ent	dz_putdec
dz_putdec:
	addiu	$sp, $sp, -40
	sw	$ra, 36($sp)
	sw	$s0, 32($sp)
	addiu	$s0, $sp, 31		/* digits go backwards from 31(sp) */
	sb	$zero, 0($s0)
	li	$t1, 10
1:	divu	$zero, $a0, $t1
	mflo	$a0
	mfhi	$t2
	addiu	$t2, $t2, '0'
	addiu	$s0, $s0, -1
	sb	$t2, 0($s0)
	bnez	$a0, 1b
	move	$a0, $s0
	jal	dz_puts
	lw	$s0, 32($sp)
	lw	$ra, 36($sp)
	addiu	$sp, $sp, 40
	jr	$ra
	.end	dz_putdec


	.section .rodata
hexdigits:	.ascii	"0123456789abcdef"
s_nl:		.asciz	"\n"
s_hello:	.asciz	"\nHello, world!  3MAX test program, DECstation 5000/200\n"
s_prom:		.asciz	"Part 1, PROM printf: callback vector %x, argc %d\n"
s_bootvar:	.asciz	"boot"
s_part2:	.asciz	"Part 2, straight to the DZ chip (line 3) and CP0:\n"
s_sysid:	.asciz	"  PROM getsysid  = 0x"
s_prid:		.asciz	"  CP0 PRId       = 0x"
s_status:	.asciz	"   Status = 0x"
s_mem:		.asciz	"  memory         = "
s_memmb:	.asciz	" MB ("
s_pages:	.asciz	" pages of "
s_pagesz:	.asciz	" bytes, PROM getbitmap)\n"
s_args:		.asciz	"  PROM arguments = "
s_arg:		.asciz	"    argv["
s_argeq:	.asciz	"] = "
s_boot:		.asciz	"  boot variable  = "
s_bye:		.asciz	"Halting back to the PROM.\n"
s_norex:	.asciz	"No REX PROM magic in a2: not started by a 5000-series PROM.\n"


	.bss
	.align	4
memmap:		.space	4 + 15360	/* PROM getbitmap: page size + bitmap */
	.align	4
stack:		.space	8192
stack_top:
