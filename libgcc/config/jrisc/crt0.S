/* Startup code for the Atari Jaguar RISC processors.
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by the
   Free Software Foundation; either version 3, or (at your option) any
   later version.

   This file is distributed in the hope that it will be useful, but
   WITHOUT ANY WARRANTY; without even the implied warranty of
   MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
   General Public License for more details.

   Under Section 7 of GPL version 3, you are granted additional
   permissions described in the GCC Runtime Library Exception, version
   3.1, as published by the Free Software Foundation.

   You should have received a copy of the GNU General Public License and
   a copy of the GCC Runtime Library Exception along with this program;
   see the files COPYING3 and COPYING.RUNTIME respectively.  If not, see
   <http://www.gnu.org/licenses/>.  */

/* The 68000 loads the program into local RAM and starts the processor
   at _start.  This sets up the two stacks and the divide unit, clears
   .lbss and .bss, runs the .init_array constructors, calls main and then
   stops the processor by clearing GO in its control register, which
   also interrupts the 68000 (see below).

   The stack (r15) starts at __stack, which the default linker scripts
   put at the end of local RAM.  It holds only data that is accessed a
   word at a time; GCC puts other locals in a second frame in main RAM,
   allocated from r27.  The linker's overlay manager keeps its return
   records there too.  r27 starts at __dram_stack, by default near the
   top of main RAM: 0x200000 for the GPU and 0x1f8000 for the DSP, so
   that both can run C at once.

   r30 and r31 are left alone: they belong to interrupt handlers (an
   interrupt pushes the return address through r31 in register bank 0).
   Code that enables interrupts must first point r31 at an interrupt
   stack.

   With a linker script that does not define them, the stacks start in
   main RAM: r15 at those addresses and r27 64K below.  Define __stack
   or __dram_stack when linking (for example --defsym=__dram_stack=
   0x100000) to put the stacks elsewhere.

   This code is in .text.resident, so that the linker never puts it in
   an overlay.  */

#ifdef __JRISC_DSP__
	.dsp
#define CTRL		0xf1a114	/* D_CTRL */
#define DIVCTRL		0xf1a11c	/* D_DIVCTRL */
#define STACK_TOP	0x1f8000	/* 32K below the GPU stack.  */
#define DRAM_STACK_TOP	0x1e8000
#else
	.gpu
#define CTRL		0xf02114	/* G_CTRL */
#define DIVCTRL		0xf0211c	/* G_DIVCTRL */
#define STACK_TOP	0x200000	/* Top of 2MB main RAM.  */
#define DRAM_STACK_TOP	0x1f0000
#endif

	/* __stack is left undefined here: an assembler-time default would be
	   folded into the MOVEI below and could not be overridden.  The
	   linker resolves an undefined weak symbol to 0, meaning "use
	   STACK_TOP".  */
	.weak	__stack
	.weak	__dram_stack
	/* Set by the default linker scripts; with other scripts there may be
	   no .lbss.  */
	.weak	__lbss_start
	.weak	__lbss_end

	.section .bss
	.p2align 2

	.section .text.resident,"ax"
	.globl	_start
	.type	_start,@function
_start:
	movei	#__stack,r15
	cmpq	#0,r15
	jr	ne,1f
	moveq	#0,r1
	movei	#STACK_TOP,r15
1:	movei	#__dram_stack,r27
	cmpq	#0,r27
	jr	ne,1f
	nop
	movei	#DRAM_STACK_TOP,r27
1:
	/* Clear .lbss in local RAM, which is word aligned.  */
	movei	#__lbss_start,r0
	movei	#__lbss_end,r2
1:	cmp	r2,r0
	jr	cc,2f		; r0 >= __lbss_end
	nop
	store	r1,(r0)
	jr	t,1b
	addq	#4,r0
2:
	movei	#DIVCTRL,r0
	store	r1,(r0)

	/* Clear .bss a word at a time.  __bss_start is set before the
	   output section is aligned, so round it up; the .bss alignment
	   below makes that the first byte of .bss.  _end is word
	   aligned.  */
	movei	#__bss_start+3,r0
	shrq	#2,r0
	shlq	#2,r0
	movei	#_end,r2
1:	cmp	r2,r0
	jr	cc,2f		; r0 >= _end
	nop
#ifdef __JRISC_DSP__
	/* .bss is in main RAM, where the DSP's long stores can write the
	   wrong data (the bug that GCC's -mfix-dsp-stores works around):
	   it clears a word at a time.  */
	storew	r1,(r0)
	addq	#2,r0
	storew	r1,(r0)
	jr	t,1b
	addq	#2,r0
#else
	store	r1,(r0)
	jr	t,1b
	addq	#4,r0
#endif

	/* Run the constructors.  */
2:	movei	#__init_array_start,r16
	movei	#__init_array_end,r17
3:	cmp	r17,r16
	jr	cc,4f
	nop
	load	(r16),r0
	addq	#4,r16
	move	pc,r29
	jump	t,(r0)
	addq	#6,r29
	jr	t,3b
	nop

	/* main (0, 0).  */
4:	moveq	#0,r0
	moveq	#0,r1
	move	pc,r29
	movei	#main,r28
	jump	t,(r28)
	addq	#12,r29

	/* Stop, and interrupt the 68000 (CPUINT), so that it can wait for
	   the processor with STOP rather than by reading its control
	   register, which takes the bus from the processors.  The
	   interrupt reaches the 68000 only if it has enabled it: the GPU's
	   in Tom's INT1 (bit 1), the DSP's in Jerry's JINTCTRL (bit 1) and
	   Jerry's in INT1 (bit 4).  main's return value is left in r0.  */
	moveq	#2,r2
	movei	#CTRL,r1
	store	r2,(r1)
5:	jr	t,5b
	nop
	.size	_start,.-_start
