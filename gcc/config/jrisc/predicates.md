;; Predicate definitions for the Atari Jaguar RISC processors.
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

;; Second operand of ADD/SUB.
(define_predicate "jrisc_add_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "const_int_operand")))

;; An ADDQT/SUBQT immediate.
(define_predicate "jrisc_addq_operand"
  (and (match_code "const_int")
       (match_test "IN_RANGE (INTVAL (op), -32, 32) && INTVAL (op) != 0")))

;; A register other than the soft frame and argument pointers (and the
;; virtual registers that become them).  Eliminating those adds an
;; offset that ADDQT/SUBQT cannot encode.
(define_predicate "jrisc_noelim_register_operand"
  (and (match_operand 0 "register_operand")
       (match_test "!REG_P (op)
		    || (REGNO (op) != JRISC_SFP_REGNUM
			&& REGNO (op) != JRISC_AP_REGNUM
			&& !VIRTUAL_REGISTER_P (op))")))

;; Second operand of a comparison: a register or a CMPQ immediate.
(define_predicate "jrisc_cmp_operand"
  (ior (match_operand 0 "register_operand")
       (and (match_code "const_int")
	    (match_test "INTVAL (op) >= -16 && INTVAL (op) <= 15"))))

;; A constant shift count.
(define_predicate "jrisc_shift_count_operand"
  (and (match_code "const_int")
       (match_test "INTVAL (op) >= 1 && INTVAL (op) <= 31")))

(define_predicate "jrisc_shift_operand"
  (ior (match_operand 0 "register_operand")
       (match_operand 0 "jrisc_shift_count_operand")))

(define_predicate "jrisc_symbolic_operand"
  (match_code "symbol_ref,label_ref,const"))

(define_predicate "jrisc_call_operand"
  (ior (match_code "symbol_ref,label_ref,const")
       (match_operand 0 "register_operand")))

;; Comparisons that a single CMP/CMPQ and JR can test.
(define_predicate "jrisc_uns_comparison"
  (match_code "eq,ne,ltu,geu,gtu"))

;; Signed comparisons against zero that the N flag can test.
(define_predicate "jrisc_sz_comparison"
  (match_code "lt,ge,gt"))

(define_predicate "jrisc_signed_comparison"
  (match_code "lt,ge,gt,le"))

;; Conditions a JR/JUMP can test, given the mode of the flags.
(define_predicate "jrisc_cc_comparison"
  (and (match_code "eq,ne,ltu,geu,gtu,lt,ge,gt")
       (match_test "GET_MODE (XEXP (op, 0)) == CC_NZmode
		    ? (GET_CODE (op) == EQ || GET_CODE (op) == NE
		       || GET_CODE (op) == LT || GET_CODE (op) == GE
		       || GET_CODE (op) == GT)
		    : (GET_CODE (op) == EQ || GET_CODE (op) == NE
		       || GET_CODE (op) == LTU || GET_CODE (op) == GEU
		       || GET_CODE (op) == GTU)")))
