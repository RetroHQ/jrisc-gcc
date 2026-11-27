/* Startup code for Atari Jaguar 68000 programs (m68k-jaguar-elf).
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

/* The linker's Jaguar emulation (ld -m m68kjaguar) puts this at the
   start of the program, and defines the symbols used here.  Run with
   interrupts off and the stack at the end of RAM (__stack), copy .data
   to where it runs if it was loaded elsewhere (a cartridge), clear
   .bss, run the constructors and call main; when main returns, exit
   with its value.  Starting the GPU and DSP is left to the program
   (jag_gpu_start and jag_dsp_start in jaguar.h).  */

	.text
	.globl	_start
	.type	_start, @function
_start:
	move.w	#0x2700, %sr
	lea	__stack, %sp

	lea	__data_load, %a0
	lea	__data_start, %a1
	cmp.l	%a0, %a1
	beq.s	2f
	lea	__data_end, %a2
1:	cmp.l	%a2, %a1
	bcc.s	2f
	move.l	(%a0)+, (%a1)+
	bra.s	1b

2:	lea	__bss_start, %a1
	lea	__bss_end, %a2
3:	cmp.l	%a2, %a1
	bcc.s	4f
	clr.l	(%a1)+
	bra.s	3b

4:	lea	__init_array_start, %a2
5:	cmp.l	#__init_array_end, %a2
	bcc.s	6f
	move.l	(%a2)+, %a0
	jsr	(%a0)
	bra.s	5b

6:	clr.l	-(%sp)		/* main (0, 0, 0) */
	clr.l	-(%sp)
	clr.l	-(%sp)
	jsr	main
	move.l	%d0, -(%sp)
	jsr	exit
	.size	_start, . - _start
