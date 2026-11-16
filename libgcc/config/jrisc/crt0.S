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
   at _start.  This sets up the stack and the divide unit, clears .bss,
   runs the .init_array constructors, calls main and then stops the
   processor by clearing GO in its control register.

   The stack starts at __stack, by default near the top of main RAM:
   0x200000 for the GPU and 0x1f8000 for the DSP, so that both can run
   C at once.  It must not be in local RAM, which only supports 32-bit
   accesses while C code uses byte and halfword loads and stores on the
   stack.  Define __stack when linking (for example
   --defsym=__stack=0x100000) to put it elsewhere.  */

#ifdef __JRISC_DSP__
	.dsp
#define CTRL		0xf1a114	/* D_CTRL */
#define DIVCTRL		0xf1a11c	/* D_DIVCTRL */
#define STACK_TOP	0x1f8000	/* 32K below the GPU stack.  */
#else
	.gpu
#define CTRL		0xf02114	/* G_CTRL */
#define DIVCTRL		0xf0211c	/* G_DIVCTRL */
#define STACK_TOP	0x200000	/* Top of 2MB main RAM.  */
#endif

	/* __stack is left undefined here: an assembler-time default would be
	   folded into the MOVEI below and could not be overridden.  The
	   linker resolves an undefined weak symbol to 0, meaning "use
	   STACK_TOP".  */
	.weak	__stack

	.section .bss
	.p2align 2

	.text
	.globl	_start
	.type	_start,@function
_start:
	movei	#__stack,r15
	cmpq	#0,r15
	jr	ne,1f
	moveq	#0,r1
	movei	#STACK_TOP,r15
1:
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
	store	r1,(r0)
	jr	t,1b
	addq	#4,r0

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

	/* Stop.  main's return value is left in r0.  */
	moveq	#0,r2
	movei	#CTRL,r1
	store	r2,(r1)
5:	jr	t,5b
	nop
	.size	_start,.-_start
