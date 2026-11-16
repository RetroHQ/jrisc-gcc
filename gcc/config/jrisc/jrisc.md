;; Machine description for the Atari Jaguar RISC processors
;; (TOM GPU and JERRY DSP).
;; Copyright (C) 2026 Free Software Foundation, Inc.
;;
;; This file is part of GCC.
;;
;; GCC is free software; you can redistribute it and/or modify it
;; under the terms of the GNU General Public License as published
;; by the Free Software Foundation; either version 3, or (at your
;; option) any later version.
;;
;; GCC is distributed in the hope that it will be useful, but WITHOUT
;; ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
;; or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
;; License for more details.
;;
;; You should have received a copy of the GNU General Public License
;; along with GCC; see the file COPYING3.  If not see
;; <http://www.gnu.org/licenses/>.

;; Notes:
;; - All instructions are 16 bits, MOVEI 48.  Arithmetic is two-address.
;; - Most ALU instructions set the Z, C and N flags; MOVE, MOVEI, MOVEQ,
;;   loads, stores, ADDQT and SUBQT leave them alone.  Patterns that
;;   change the flags say so with a clobber of CC_REG.
;; - JUMP and JR have one delay slot, which must not hold a jump or a
;;   MOVEI.
;; - r28 is reserved: templates use it as a scratch register for long
;;   branches, calls and large constants.

(define_constants
  [(FP_REG	14)
   (SP_REG	15)
   (TEMP_REG	28)
   (LR_REG	29)
   (CC_REG	34)])

(define_c_enum "unspecv"
  [UNSPECV_BLOCKAGE])

;; ---------------------------------------------------------------------
;; Attributes

(define_attr "type"
  "alu,move,movei,load,store,mul,div,branch,jump,return,call,multi,nop"
  (const_string "alu"))

(define_attr "length" "" (const_int 2))

;; Can the instruction go in a delay slot?
(define_attr "slottable" "no,yes"
  (if_then_else (and (eq_attr "type" "alu,move,load,store,mul,div,nop")
		     (match_test "get_attr_length (insn) == 2"))
		(const_string "yes")
		(const_string "no")))

;; Does a branch get a NOP after it because nothing filled its slot?
(define_attr "empty_delay_slot" "false,true"
  (symbol_ref "(jrisc_empty_delay_slot_p (insn)
		? EMPTY_DELAY_SLOT_TRUE : EMPTY_DELAY_SLOT_FALSE)"))

(define_delay (eq_attr "type" "branch,jump,return")
  [(eq_attr "slottable" "yes") (nil) (nil)])

(include "predicates.md")
(include "constraints.md")

(define_mode_iterator QHI [QI HI])
(define_mode_iterator SISF [SI SF])
(define_mode_iterator DIDF [DI DF])

;; ---------------------------------------------------------------------
;; Moves

(define_expand "mov<mode>"
  [(set (match_operand:QHI 0 "nonimmediate_operand")
	(match_operand:QHI 1 "general_operand"))]
  ""
{
  if (jrisc_expand_move (<MODE>mode, operands))
    DONE;
})

(define_insn "*mov<mode>_insn"
  [(set (match_operand:QHI 0 "nonimmediate_operand" "=r,r,r,r,m")
	(match_operand:QHI 1 "general_operand"      " r,I,i,m,r"))]
  "register_operand (operands[0], <MODE>mode)
   || register_operand (operands[1], <MODE>mode)"
  "* return jrisc_output_move (operands, <MODE>mode);"
  [(set_attr "type" "move,move,movei,load,store")
   (set (attr "length") (symbol_ref "jrisc_insn_length (insn)"))])

(define_expand "mov<mode>"
  [(set (match_operand:SISF 0 "nonimmediate_operand")
	(match_operand:SISF 1 "general_operand"))]
  ""
{
  if (jrisc_expand_move (<MODE>mode, operands))
    DONE;
})

(define_insn "*mov<mode>_insn"
  [(set (match_operand:SISF 0 "nonimmediate_operand" "=r,r,r,r,m")
	(match_operand:SISF 1 "general_operand"      " r,I,i,m,r"))]
  "register_operand (operands[0], <MODE>mode)
   || register_operand (operands[1], <MODE>mode)"
  "* return jrisc_output_move (operands, <MODE>mode);"
  [(set_attr "type" "move,move,movei,load,store")
   (set (attr "length") (symbol_ref "jrisc_insn_length (insn)"))])

(define_expand "mov<mode>"
  [(set (match_operand:DIDF 0 "nonimmediate_operand")
	(match_operand:DIDF 1 "general_operand"))]
  ""
{
  if (jrisc_expand_move (<MODE>mode, operands))
    DONE;
})

(define_insn "*mov<mode>_insn"
  [(set (match_operand:DIDF 0 "nonimmediate_operand" "=r,r,m")
	(match_operand:DIDF 1 "general_operand"      "ri,m,r"))]
  "register_operand (operands[0], <MODE>mode)
   || register_operand (operands[1], <MODE>mode)"
  "* return jrisc_output_move (operands, <MODE>mode);"
  [(set_attr "type" "multi")
   (set (attr "length") (symbol_ref "jrisc_insn_length (insn)"))])

;; ---------------------------------------------------------------------
;; Extensions

(define_insn "zero_extend<mode>si2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI
	  (match_operand:QHI 1 "nonimmediate_operand" "0,m")))
   (clobber (reg:CC CC_REG))]
  ""
  "* return jrisc_output_extend (operands, <MODE>mode, false);"
  [(set_attr "type" "multi,load")
   (set (attr "length") (symbol_ref "jrisc_insn_length (insn)"))])

(define_insn "extend<mode>si2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(sign_extend:SI
	  (match_operand:QHI 1 "nonimmediate_operand" "0,m")))
   (clobber (reg:CC CC_REG))]
  ""
  "* return jrisc_output_extend (operands, <MODE>mode, true);"
  [(set_attr "type" "multi")
   (set (attr "length") (symbol_ref "jrisc_insn_length (insn)"))])

;; ---------------------------------------------------------------------
;; Arithmetic

(define_expand "addsi3"
  [(parallel [(set (match_operand:SI 0 "register_operand")
		   (plus:SI (match_operand:SI 1 "register_operand")
			    (match_operand:SI 2 "jrisc_add_operand")))
	      (clobber (reg:CC CC_REG))])]
  "")

(define_insn "*addsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(plus:SI (match_operand:SI 1 "register_operand" "%0,0,0,0")
		 (match_operand:SI 2 "jrisc_add_operand" "r,K,N,i")))
   (clobber (reg:CC CC_REG))]
  ""
  "@
   add\t%2,%0
   addq\t%2,%0
   subq\t#%n2,%0
   movei\t%2,r28\;add\tr28,%0"
  [(set_attr "length" "2,2,2,8")])

;; ADDQT and SUBQT do not change the flags, which lets reload and the
;; prologue adjust registers without disturbing a comparison.
(define_insn "*addsi3_noflags"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(plus:SI (match_operand:SI 1 "jrisc_noelim_register_operand" "0,0")
		 (match_operand:SI 2 "jrisc_addq_operand" "K,N")))]
  ""
  "@
   addqt\t%2,%0
   subqt\t#%n2,%0")

(define_insn "subsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(minus:SI (match_operand:SI 1 "register_operand" "0,0,0,0")
		  (match_operand:SI 2 "jrisc_add_operand" "r,K,N,i")))
   (clobber (reg:CC CC_REG))]
  ""
  "@
   sub\t%2,%0
   subq\t%2,%0
   addq\t#%n2,%0
   movei\t%2,r28\;sub\tr28,%0"
  [(set_attr "length" "2,2,2,8")])

(define_insn "negsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(neg:SI (match_operand:SI 1 "register_operand" "0")))
   (clobber (reg:CC CC_REG))]
  ""
  "neg\t%0")

(define_insn "abssi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(abs:SI (match_operand:SI 1 "register_operand" "0")))
   (clobber (reg:CC CC_REG))]
  ""
  "abs\t%0")

;; MULT and IMULT multiply the low 16 bits of two registers.
(define_insn "umulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (zero_extend:SI (match_operand:HI 1 "register_operand" "%0"))
		 (zero_extend:SI (match_operand:HI 2 "register_operand" "r"))))
   (clobber (reg:CC CC_REG))]
  ""
  "mult\t%2,%0"
  [(set_attr "type" "mul")])

(define_insn "mulhisi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (sign_extend:SI (match_operand:HI 1 "register_operand" "%0"))
		 (sign_extend:SI (match_operand:HI 2 "register_operand" "r"))))
   (clobber (reg:CC CC_REG))]
  ""
  "imult\t%2,%0"
  [(set_attr "type" "mul")])

;; A 32x32 multiply built from three 16x16 ones.
(define_expand "mulsi3"
  [(set (match_operand:SI 0 "register_operand")
	(mult:SI (match_operand:SI 1 "register_operand")
		 (match_operand:SI 2 "register_operand")))]
  ""
{
  jrisc_expand_mulsi3 (operands[0], operands[1], operands[2]);
  DONE;
})

;; DIV is an unsigned 32/32 division (with DIVCTRL clear, as crt0
;; leaves it).
(define_insn "udivsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(udiv:SI (match_operand:SI 1 "register_operand" "0")
		 (match_operand:SI 2 "register_operand" "r")))
   (clobber (reg:CC CC_REG))]
  ""
  "div\t%2,%0"
  [(set_attr "type" "div")])

;; ---------------------------------------------------------------------
;; Logical operations

(define_insn "andsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r,r")
	(and:SI (match_operand:SI 1 "register_operand" "%0,0,0,0")
		(match_operand:SI 2 "nonmemory_operand" "r,M,Q,i")))
   (clobber (reg:CC CC_REG))]
  ""
{
  switch (which_alternative)
    {
    case 0:
      return "and\t%2,%0";
    case 1:
      operands[2] = GEN_INT (32 - exact_log2 (INTVAL (operands[2]) + 1));
      return "shlq\t%2,%0\;shrq\t%2,%0";
    case 2:
      operands[2] = GEN_INT (exact_log2 (~INTVAL (operands[2]) & 0xffffffff));
      return "bclr\t%2,%0";
    default:
      return "movei\t%2,r28\;and\tr28,%0";
    }
}
  [(set_attr "length" "2,4,2,8")
   (set_attr "type" "alu,multi,alu,multi")])

(define_insn "iorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r,r")
	(ior:SI (match_operand:SI 1 "register_operand" "%0,0,0")
		(match_operand:SI 2 "nonmemory_operand" "r,P,i")))
   (clobber (reg:CC CC_REG))]
  ""
{
  switch (which_alternative)
    {
    case 0:
      return "or\t%2,%0";
    case 1:
      operands[2] = GEN_INT (exact_log2 (INTVAL (operands[2]) & 0xffffffff));
      return "bset\t%2,%0";
    default:
      return "movei\t%2,r28\;or\tr28,%0";
    }
}
  [(set_attr "length" "2,2,8")
   (set_attr "type" "alu,alu,multi")])

(define_insn "xorsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(xor:SI (match_operand:SI 1 "register_operand" "%0,0")
		(match_operand:SI 2 "nonmemory_operand" "r,i")))
   (clobber (reg:CC CC_REG))]
  ""
  "@
   xor\t%2,%0
   movei\t%2,r28\;xor\tr28,%0"
  [(set_attr "length" "2,8")
   (set_attr "type" "alu,multi")])

(define_insn "one_cmplsi2"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(not:SI (match_operand:SI 1 "register_operand" "0")))
   (clobber (reg:CC CC_REG))]
  ""
  "not\t%0")

;; ---------------------------------------------------------------------
;; Shifts and rotates
;;
;; SH and SHA shift right by a positive count and left by a negative one.

(define_insn "lshrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(lshiftrt:SI (match_operand:SI 1 "register_operand" "0,0")
		     (match_operand:SI 2 "jrisc_shift_operand" "J,r")))
   (clobber (reg:CC CC_REG))]
  ""
  "@
   shrq\t%2,%0
   sh\t%2,%0")

(define_insn "ashrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(ashiftrt:SI (match_operand:SI 1 "register_operand" "0,0")
		     (match_operand:SI 2 "jrisc_shift_operand" "J,r")))
   (clobber (reg:CC CC_REG))]
  ""
  "@
   sharq\t%2,%0
   sha\t%2,%0")

(define_expand "ashlsi3"
  [(parallel [(set (match_operand:SI 0 "register_operand")
		   (ashift:SI (match_operand:SI 1 "register_operand")
			      (match_operand:SI 2 "jrisc_shift_operand")))
	      (clobber (reg:CC CC_REG))])]
  ""
{
  if (!CONST_INT_P (operands[2]))
    {
      rtx count = gen_reg_rtx (SImode);
      emit_insn (gen_negsi2 (count, operands[2]));
      emit_insn (gen_jrisc_shl_reg (operands[0], operands[1], count));
      DONE;
    }
})

(define_insn "*ashlsi3_const"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashift:SI (match_operand:SI 1 "register_operand" "0")
		   (match_operand:SI 2 "jrisc_shift_count_operand" "J")))
   (clobber (reg:CC CC_REG))]
  ""
  "shlq\t%2,%0")

;; Operand 2 holds the negated count.
(define_insn "jrisc_shl_reg"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(ashift:SI (match_operand:SI 1 "register_operand" "0")
		   (neg:SI (match_operand:SI 2 "register_operand" "r"))))
   (clobber (reg:CC CC_REG))]
  ""
  "sh\t%2,%0")

(define_insn "rotrsi3"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(rotatert:SI (match_operand:SI 1 "register_operand" "0,0")
		     (match_operand:SI 2 "jrisc_shift_operand" "J,r")))
   (clobber (reg:CC CC_REG))]
  ""
  "@
   rorq\t%2,%0
   ror\t%2,%0")

(define_insn "rotlsi3"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(rotate:SI (match_operand:SI 1 "register_operand" "0")
		   (match_operand:SI 2 "jrisc_shift_count_operand" "J")))
   (clobber (reg:CC CC_REG))]
  ""
{
  operands[2] = GEN_INT (32 - INTVAL (operands[2]));
  return "rorq\t%2,%0";
})

;; ---------------------------------------------------------------------
;; Comparisons and branches
;;
;; A comparison and its branch stay together until after reload, so that
;; reload cannot put an instruction that changes the flags between them.

(define_expand "cbranchsi4"
  [(set (pc)
	(if_then_else (match_operator 0 "comparison_operator"
			[(match_operand:SI 1 "register_operand")
			 (match_operand:SI 2 "nonmemory_operand")])
		      (label_ref (match_operand 3 ""))
		      (pc)))]
  ""
{
  jrisc_expand_cbranch (operands);
  DONE;
})

(define_insn_and_split "jrisc_cbranch_uns"
  [(set (pc)
	(if_then_else (match_operator 0 "jrisc_uns_comparison"
			[(match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "jrisc_cmp_operand" "rL")])
		      (label_ref (match_operand 3 ""))
		      (pc)))
   (clobber (reg:CC CC_REG))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  jrisc_split_cbranch (operands, NULL_RTX);
  DONE;
})

(define_insn_and_split "jrisc_cbranch_sz"
  [(set (pc)
	(if_then_else (match_operator 0 "jrisc_sz_comparison"
			[(match_operand:SI 1 "register_operand" "r")
			 (const_int 0)])
		      (label_ref (match_operand 2 ""))
		      (pc)))
   (clobber (reg:CC CC_REG))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  rtx ops[4] = { operands[0], operands[1], const0_rtx, operands[2] };
  jrisc_split_cbranch (ops, NULL_RTX);
  DONE;
})

;; Signed comparisons other than LT, GE and GT against zero.  The cores
;; have no overflow flag, so both operands are biased by 0x80000000 and
;; compared unsigned (LE against zero is done the same way, which is
;; simpler than testing x | (x - 1)).
(define_insn_and_split "jrisc_cbranch_signed"
  [(set (pc)
	(if_then_else (match_operator 0 "jrisc_signed_comparison"
			[(match_operand:SI 1 "register_operand" "r")
			 (match_operand:SI 2 "nonmemory_operand" "ri")])
		      (label_ref (match_operand 3 ""))
		      (pc)))
   (clobber (match_scratch:SI 4 "=&r"))
   (clobber (match_scratch:SI 5 "=&r"))
   (clobber (reg:CC CC_REG))]
  ""
  "#"
  "reload_completed"
  [(const_int 0)]
{
  jrisc_split_cbranch (operands, operands[4]);
  DONE;
})

(define_insn "*cmpsi_cc"
  [(set (reg:CC CC_REG)
	(compare:CC (match_operand:SI 0 "register_operand" "r,r")
		    (match_operand:SI 1 "jrisc_cmp_operand" "r,L")))]
  "reload_completed"
  "@
   cmp\t%1,%0
   cmpq\t%1,%0")

(define_insn "*cmpsi_cc_nz"
  [(set (reg:CC_NZ CC_REG)
	(compare:CC_NZ (match_operand:SI 0 "register_operand" "r")
		       (const_int 0)))]
  "reload_completed"
  "cmpq\t#0,%0")

(define_insn "*jcc"
  [(set (pc)
	(if_then_else (match_operator 1 "jrisc_cc_comparison"
			[(reg CC_REG) (const_int 0)])
		      (label_ref (match_operand 0 ""))
		      (pc)))]
  "reload_completed"
  "* return jrisc_output_branch (operands[0], operands[1]);"
  [(set_attr "type" "branch")
   (set (attr "length")
	(if_then_else
	  (and (ge (minus (match_dup 0) (pc)) (const_int -26))
	       (le (minus (match_dup 0) (pc)) (const_int 28)))
	  (if_then_else (eq_attr "empty_delay_slot" "true")
			(const_int 4)
			(const_int 2))
	  (if_then_else (eq_attr "empty_delay_slot" "true")
			(const_int 10)
			(const_int 8))))])

(define_insn "jump"
  [(set (pc) (label_ref (match_operand 0 "" "")))]
  ""
  "* return jrisc_output_branch (operands[0], NULL_RTX);"
  [(set_attr "type" "jump")
   (set (attr "length")
	(if_then_else
	  (and (ge (minus (match_dup 0) (pc)) (const_int -26))
	       (le (minus (match_dup 0) (pc)) (const_int 28)))
	  (if_then_else (eq_attr "empty_delay_slot" "true")
			(const_int 4)
			(const_int 2))
	  (if_then_else (eq_attr "empty_delay_slot" "true")
			(const_int 10)
			(const_int 8))))])

(define_insn "indirect_jump"
  [(set (pc) (match_operand:SI 0 "register_operand" "r"))]
  ""
  "jump\tt,(%0)%#"
  [(set_attr "type" "jump")
   (set (attr "length")
	(if_then_else (eq_attr "empty_delay_slot" "true")
		      (const_int 4)
		      (const_int 2)))])

(define_insn "tablejump"
  [(set (pc) (match_operand:SI 0 "register_operand" "r"))
   (use (label_ref (match_operand 1 "" "")))]
  ""
  "jump\tt,(%0)%#"
  [(set_attr "type" "jump")
   (set (attr "length")
	(if_then_else (eq_attr "empty_delay_slot" "true")
		      (const_int 4)
		      (const_int 2)))])

;; ---------------------------------------------------------------------
;; Calls
;;
;; There is no call instruction.  MOVE PC,Rn gives the address of the
;; MOVE itself, so the return address is set up as
;;
;;	move	pc,r29
;;	movei	#func,r28		(or use the register holding it)
;;	jump	t,(r28)
;;	addq	#12,r29			; delay slot

(define_expand "call"
  [(parallel [(call (match_operand:QI 0 "memory_operand")
		    (match_operand 1 ""))
	      (clobber (reg:SI LR_REG))
	      (clobber (reg:SI TEMP_REG))])]
  ""
{
  rtx addr = XEXP (operands[0], 0);
  if (!jrisc_call_operand (addr, SImode))
    operands[0] = replace_equiv_address (operands[0],
					 force_reg (SImode, addr));
})

(define_insn "*call"
  [(call (mem:QI (match_operand:SI 0 "jrisc_call_operand" "c,S"))
	 (match_operand 1 "" ""))
   (clobber (reg:SI LR_REG))
   (clobber (reg:SI TEMP_REG))]
  ""
  "* return jrisc_output_call (operands, 0, false);"
  [(set_attr "type" "call")
   (set_attr "length" "6,12")])

;; __builtin_apply: values can come back in r0 and r1, so call_value
;; cannot describe the call; make a plain call and copy the result
;; registers into the block.
(define_expand "untyped_call"
  [(parallel [(call (match_operand 0 "")
		    (const_int 0))
	      (match_operand 1 "")
	      (match_operand 2 "")])]
  ""
{
  emit_call_insn (gen_call (operands[0], const0_rtx));

  for (int i = 0; i < XVECLEN (operands[2], 0); i++)
    {
      rtx set = XVECEXP (operands[2], 0, i);
      emit_move_insn (SET_DEST (set), SET_SRC (set));
    }

  /* The optimizer must not move the copies of the result registers
     away from the call.  */
  emit_insn (gen_blockage ());
  DONE;
})

(define_expand "call_value"
  [(parallel [(set (match_operand 0 "")
		   (call (match_operand:QI 1 "memory_operand")
			 (match_operand 2 "")))
	      (clobber (reg:SI LR_REG))
	      (clobber (reg:SI TEMP_REG))])]
  ""
{
  rtx addr = XEXP (operands[1], 0);
  if (!jrisc_call_operand (addr, SImode))
    operands[1] = replace_equiv_address (operands[1],
					 force_reg (SImode, addr));
})

(define_insn "*call_value"
  [(set (match_operand 0 "" "")
	(call (mem:QI (match_operand:SI 1 "jrisc_call_operand" "c,S"))
	      (match_operand 2 "" "")))
   (clobber (reg:SI LR_REG))
   (clobber (reg:SI TEMP_REG))]
  ""
  "* return jrisc_output_call (operands, 1, false);"
  [(set_attr "type" "call")
   (set_attr "length" "6,12")])

(define_expand "sibcall"
  [(parallel [(call (match_operand:QI 0 "memory_operand")
		    (match_operand 1 ""))
	      (return)
	      (clobber (reg:SI TEMP_REG))])]
  "")

(define_insn "*sibcall"
  [(call (mem:QI (match_operand:SI 0 "jrisc_symbolic_operand" "S"))
	 (match_operand 1 "" ""))
   (return)
   (clobber (reg:SI TEMP_REG))]
  "SIBLING_CALL_P (insn)"
  "* return jrisc_output_call (operands, 0, true);"
  [(set_attr "type" "call")
   (set_attr "length" "10")])

(define_expand "sibcall_value"
  [(parallel [(set (match_operand 0 "")
		   (call (match_operand:QI 1 "memory_operand")
			 (match_operand 2 "")))
	      (return)
	      (clobber (reg:SI TEMP_REG))])]
  "")

(define_insn "*sibcall_value"
  [(set (match_operand 0 "" "")
	(call (mem:QI (match_operand:SI 1 "jrisc_symbolic_operand" "S"))
	      (match_operand 2 "" "")))
   (return)
   (clobber (reg:SI TEMP_REG))]
  "SIBLING_CALL_P (insn)"
  "* return jrisc_output_call (operands, 1, true);"
  [(set_attr "type" "call")
   (set_attr "length" "10")])

;; ---------------------------------------------------------------------
;; Prologue, epilogue and miscellaneous

(define_expand "prologue"
  [(const_int 0)]
  ""
{
  jrisc_expand_prologue ();
  DONE;
})

(define_expand "epilogue"
  [(return)]
  ""
{
  jrisc_expand_epilogue (false);
  DONE;
})

(define_expand "sibcall_epilogue"
  [(return)]
  ""
{
  jrisc_expand_epilogue (true);
  DONE;
})

(define_insn "jrisc_return"
  [(return)
   (use (reg:SI LR_REG))]
  "reload_completed"
  "jump\tt,(r29)%#"
  [(set_attr "type" "return")
   (set (attr "length")
	(if_then_else (eq_attr "empty_delay_slot" "true")
		      (const_int 4)
		      (const_int 2)))])

(define_insn "nop"
  [(const_int 0)]
  ""
  "nop"
  [(set_attr "type" "nop")])

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPECV_BLOCKAGE)]
  ""
  ""
  [(set_attr "length" "0")
   (set_attr "type" "multi")])
