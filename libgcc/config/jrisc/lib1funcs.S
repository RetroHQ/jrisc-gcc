/* Integer division for the Atari Jaguar RISC processors.
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

/* Arguments in r0 and r1, result in r0, return address in r29.  r0-r13
   may be clobbered.  DIV is an unsigned 32/32 division; it needs
   DIVCTRL clear, which crt0 sees to.  The remainder is computed as
   a - (a / b) * b rather than read from the REMAIN register.

   As for compiled code (GCC's -mfix-hazards), a function returns with
   no divide or load from main RAM still to write a register: the
   divides' returns wait for the quotient in the delay slot.  */

#ifdef __JRISC_DSP__
	.dsp
#else
	.gpu
#endif
	.text

/* R0 = R0 - (R0 / R1) * R1, unsigned.  Clobbers r2-r4.  */
	.macro	UMOD
	move	r0,r2
	div	r1,r2		; q
	move	r2,r3
	shrq	#16,r3
	mult	r1,r3		; qh * bl
	move	r1,r4
	shrq	#16,r4
	mult	r2,r4		; bh * ql
	add	r4,r3
	shlq	#16,r3
	mult	r1,r2		; ql * bl
	add	r3,r2		; q * b
	sub	r2,r0
	.endm

#ifdef L_udivsi3
	.globl	__udivsi3
	.type	__udivsi3,@function
__udivsi3:
	div	r1,r0
	jump	t,(r29)
	move	r0,r0		; wait for the quotient
	.size	__udivsi3,.-__udivsi3
#endif

#ifdef L_umodsi3
	.globl	__umodsi3
	.type	__umodsi3,@function
__umodsi3:
	UMOD
	jump	t,(r29)
	nop
	.size	__umodsi3,.-__umodsi3
#endif

#ifdef L_divsi3
	.globl	__divsi3
	.type	__divsi3,@function
__divsi3:
	move	r0,r5
	xor	r1,r5		; sign of the quotient
	abs	r0
	abs	r1
	div	r1,r0
	cmpq	#0,r5
	jr	pl,1f
	nop
	neg	r0
1:	jump	t,(r29)
	move	r0,r0		; wait for the quotient
	.size	__divsi3,.-__divsi3
#endif

#ifdef L_modsi3
	.globl	__modsi3
	.type	__modsi3,@function
__modsi3:
	move	r0,r5		; the remainder has the sign of the dividend
	abs	r0
	abs	r1
	UMOD
	cmpq	#0,r5
	jr	pl,1f
	nop
	neg	r0
1:	jump	t,(r29)
	nop
	.size	__modsi3,.-__modsi3
#endif
