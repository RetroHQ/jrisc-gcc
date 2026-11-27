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
   (DRAM_SP_REG	27)
   (CC_REG	34)])

(define_c_enum "unspecv"
  [UNSPECV_BLOCKAGE UNSPECV_WAIT])

(define_c_enum "unspec"
  [UNSPEC_PACK UNSPEC_UNPACK UNSPEC_SAT8 UNSPEC_SAT16 UNSPEC_SAT24
   UNSPEC_SAT16S UNSPEC_MIRROR UNSPEC_NORMI UNSPEC_MTOI
   UNSPEC_ADDQMOD UNSPEC_SUBQMOD UNSPEC_LOADP UNSPEC_STOREP])

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

;; A return's delay slot executes after the jump, where the hazard pass
;; (jrisc_hazards) can no longer wait for what it leaves pending: it may
;; not start a divide or a load from main RAM, except a load of the
;; return value.
(define_delay (eq_attr "type" "branch,jump")
  [(eq_attr "slottable" "yes") (nil) (nil)])

(define_delay (eq_attr "type" "return")
  [(and (eq_attr "slottable" "yes")
	(match_test "jrisc_return_slot_ok_p (insn)"))
   (nil) (nil)])

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

;; Extending the low part of an SImode register is done on the whole
;; register, so that there is no QImode or HImode subreg, which LRA could
;; reload from part of a spill slot: a narrow access that local RAM does
;; not support.  Such extensions that combine or lower-subreg make are
;; split before register allocation.

(define_expand "zero_extend<mode>si2"
  [(parallel [(set (match_operand:SI 0 "register_operand")
		   (zero_extend:SI
		     (match_operand:QHI 1 "nonimmediate_operand")))
	      (clobber (reg:CC CC_REG))])]
  ""
{
  if (jrisc_expand_extend (operands, <MODE>mode, false))
    DONE;
})

(define_expand "extend<mode>si2"
  [(parallel [(set (match_operand:SI 0 "register_operand")
		   (sign_extend:SI
		     (match_operand:QHI 1 "nonimmediate_operand")))
	      (clobber (reg:CC CC_REG))])]
  ""
{
  if (jrisc_expand_extend (operands, <MODE>mode, true))
    DONE;
})

(define_insn_and_split "*zero_extend<mode>si2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(zero_extend:SI
	  (match_operand:QHI 1 "nonimmediate_operand" "0,m")))
   (clobber (reg:CC CC_REG))]
  ""
  "* return jrisc_output_extend (operands, <MODE>mode, false);"
  "&& can_create_pseudo_p () && jrisc_word_lowpart_p (operands[1])"
  [(const_int 0)]
{
  jrisc_expand_extend (operands, <MODE>mode, false);
  DONE;
}
  [(set_attr "type" "multi,load")
   (set (attr "length") (symbol_ref "jrisc_insn_length (insn)"))])

(define_insn_and_split "*extend<mode>si2"
  [(set (match_operand:SI 0 "register_operand" "=r,r")
	(sign_extend:SI
	  (match_operand:QHI 1 "nonimmediate_operand" "0,m")))
   (clobber (reg:CC CC_REG))]
  ""
  "* return jrisc_output_extend (operands, <MODE>mode, true);"
  "&& can_create_pseudo_p () && jrisc_word_lowpart_p (operands[1])"
  [(const_int 0)]
{
  jrisc_expand_extend (operands, <MODE>mode, true);
  DONE;
}
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

;; MULT and IMULT multiply the low 16 bits of two registers.  The insns
;; take SImode registers, so that there are no HImode subregs, which LRA
;; could reload from the low half of a spill slot: a halfword access
;; that local RAM does not support.

(define_expand "umulhisi3"
  [(set (match_operand:SI 0 "register_operand")
	(mult:SI (zero_extend:SI (match_operand:HI 1 "register_operand"))
		 (zero_extend:SI (match_operand:HI 2 "register_operand"))))]
  ""
{
  emit_insn (gen_jrisc_umul16 (operands[0],
			       jrisc_word_reg (operands[1]),
			       jrisc_word_reg (operands[2])));
  DONE;
})

(define_expand "mulhisi3"
  [(set (match_operand:SI 0 "register_operand")
	(mult:SI (sign_extend:SI (match_operand:HI 1 "register_operand"))
		 (sign_extend:SI (match_operand:HI 2 "register_operand"))))]
  ""
{
  emit_insn (gen_jrisc_smul16 (operands[0],
			       jrisc_word_reg (operands[1]),
			       jrisc_word_reg (operands[2])));
  DONE;
})

(define_insn "jrisc_umul16"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (and:SI (match_operand:SI 1 "register_operand" "%0")
			 (const_int 65535))
		 (and:SI (match_operand:SI 2 "register_operand" "r")
			 (const_int 65535))))
   (clobber (reg:CC CC_REG))]
  ""
  "mult\t%2,%0"
  [(set_attr "type" "mul")])

(define_insn "jrisc_smul16"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(mult:SI (sign_extend:SI
		   (truncate:HI (match_operand:SI 1 "register_operand" "%0")))
		 (sign_extend:SI
		   (truncate:HI (match_operand:SI 2 "register_operand" "r")))))
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

;; Wait for a register that a divide or a load from main RAM has still to
;; write: MOVE reads it, so it waits, and then rewrites it unchanged
;; (and leaves the flags alone).  Inserted by the hazard pass
;; (jrisc_hazards).
(define_insn "jrisc_wait"
  [(unspec_volatile [(match_operand:SI 0 "register_operand" "r")]
		    UNSPECV_WAIT)]
  ""
  "move\t%0,%0"
  [(set_attr "type" "move")])

(define_insn "blockage"
  [(unspec_volatile [(const_int 0)] UNSPECV_BLOCKAGE)]
  ""
  ""
  [(set_attr "length" "0")
   (set_attr "type" "multi")])
;; ---------------------------------------------------------------------
;; The main RAM stack (r27), which alloca uses; see jrisc.h.  The r15
;; stack only moves in the prologue and epilogue.

(define_expand "allocate_stack"
  [(match_operand:SI 0 "register_operand")
   (match_operand:SI 1 "general_operand")]
  ""
{
  rtx sp = gen_rtx_REG (SImode, DRAM_SP_REG);

  emit_insn (gen_subsi3 (sp, sp, force_reg (SImode, operands[1])));
  /* Keep the stack pointer word aligned.  */
  emit_insn (gen_lshrsi3 (sp, sp, GEN_INT (2)));
  emit_insn (gen_ashlsi3 (sp, sp, GEN_INT (2)));
  emit_move_insn (operands[0], sp);
  DONE;
})

(define_expand "save_stack_block"
  [(match_operand:SI 0 "register_operand")
   (match_operand:SI 1 "register_operand")]
  ""
{
  emit_move_insn (operands[0], gen_rtx_REG (SImode, DRAM_SP_REG));
  DONE;
})

(define_expand "restore_stack_block"
  [(match_operand:SI 0 "register_operand")
   (match_operand:SI 1 "register_operand")]
  ""
{
  emit_move_insn (gen_rtx_REG (SImode, DRAM_SP_REG), operands[1]);
  DONE;
})

;; __builtin_setjmp and nonlocal gotos: r15, then r27.

(define_expand "save_stack_nonlocal"
  [(match_operand:DI 0 "memory_operand")
   (match_operand:SI 1 "register_operand")]
  ""
{
  emit_move_insn (adjust_address (operands[0], SImode, 0), operands[1]);
  emit_move_insn (adjust_address (operands[0], SImode, 4),
		  gen_rtx_REG (SImode, DRAM_SP_REG));
  DONE;
})

(define_expand "restore_stack_nonlocal"
  [(match_operand:SI 0 "register_operand")
   (match_operand:DI 1 "memory_operand")]
  ""
{
  emit_move_insn (gen_rtx_REG (SImode, DRAM_SP_REG),
		  adjust_address (operands[1], SImode, 4));
  emit_move_insn (operands[0], adjust_address (operands[1], SImode, 0));
  DONE;
})
;; ---------------------------------------------------------------------
;; Block moves and sets of word-aligned objects, a word at a time, so
;; that they work in local RAM.

(define_expand "cpymemsi"
  [(match_operand:BLK 0 "memory_operand")
   (match_operand:BLK 1 "memory_operand")
   (match_operand:SI 2 "general_operand")
   (match_operand:SI 3 "const_int_operand")]
  ""
{
  if (jrisc_expand_block_op (operands[0], operands[1], operands[2],
			     NULL_RTX, operands[3]))
    DONE;
  FAIL;
})

(define_expand "setmemsi"
  [(match_operand:BLK 0 "memory_operand")
   (match_operand:SI 1 "general_operand")
   (match_operand:QI 2 "nonmemory_operand")
   (match_operand:SI 3 "const_int_operand")]
  ""
{
  if (jrisc_expand_block_op (operands[0], NULL_RTX, operands[1],
			     operands[2], operands[3]))
    DONE;
  FAIL;
})
;; ---------------------------------------------------------------------
;; Instructions for the __builtin_jrisc_* functions (see jrisc.cc).

(define_int_iterator GPU_UNARY [UNSPEC_PACK UNSPEC_UNPACK UNSPEC_SAT8
				UNSPEC_SAT16 UNSPEC_SAT24])
(define_int_iterator DSP_UNARY [UNSPEC_SAT16S UNSPEC_MIRROR])
(define_int_iterator BOTH_BINARY [UNSPEC_NORMI UNSPEC_MTOI])
(define_int_iterator DSP_MOD [UNSPEC_ADDQMOD UNSPEC_SUBQMOD])
(define_int_attr insn [(UNSPEC_PACK "pack") (UNSPEC_UNPACK "unpack")
		       (UNSPEC_SAT8 "sat8") (UNSPEC_SAT16 "sat16")
		       (UNSPEC_SAT24 "sat24") (UNSPEC_SAT16S "sat16s")
		       (UNSPEC_MIRROR "mirror") (UNSPEC_NORMI "normi")
		       (UNSPEC_MTOI "mtoi") (UNSPEC_ADDQMOD "addqmod")
		       (UNSPEC_SUBQMOD "subqmod")])

;; One register, read and written.
(define_insn "jrisc_<insn>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:SI 1 "register_operand" "0")] GPU_UNARY))
   (clobber (reg:CC CC_REG))]
  "TARGET_GPU"
  "<insn>\t%0")

(define_insn "jrisc_<insn>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:SI 1 "register_operand" "0")] DSP_UNARY))
   (clobber (reg:CC CC_REG))]
  "!TARGET_GPU"
  "<insn>\t%0")

(define_insn "jrisc_<insn>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:SI 1 "register_operand" "r")] BOTH_BINARY))
   (clobber (reg:CC CC_REG))]
  ""
  "<insn>\t%1,%0")

;; ADDQMOD and SUBQMOD keep the bits that D_MOD sets.
(define_insn "jrisc_<insn>"
  [(set (match_operand:SI 0 "register_operand" "=r")
	(unspec:SI [(match_operand:SI 1 "register_operand" "0")
		    (match_operand:SI 2 "const_int_operand" "n")] DSP_MOD))
   (clobber (reg:CC CC_REG))]
  "!TARGET_GPU"
  "<insn>\t#%c2,%0")

;; A phrase (8 bytes, phrase aligned) to or from a register pair: the
;; register's long word is the phrase's second, G_HIDATA has the first.
;; LOADP's register is waited for before G_HIDATA is read.
(define_insn "jrisc_loadp"
  [(set (match_operand:DI 0 "register_operand" "=r")
	(unspec:DI [(mem:DI (match_operand:SI 1 "register_operand" "r"))]
		   UNSPEC_LOADP))]
  "TARGET_GPU"
  "* return jrisc_output_phrase (operands, true);"
  [(set_attr "type" "multi")
   (set_attr "length" "12")])

(define_insn "jrisc_storep"
  [(set (mem:DI (match_operand:SI 0 "register_operand" "r"))
	(unspec:DI [(match_operand:DI 1 "register_operand" "r")]
		   UNSPEC_STOREP))]
  "TARGET_GPU"
  "* return jrisc_output_phrase (operands, false);"
  [(set_attr "type" "multi")
   (set_attr "length" "10")])
