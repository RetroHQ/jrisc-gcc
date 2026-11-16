;; Constraint definitions for the Atari Jaguar RISC processors.
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

(define_register_constraint "c" "CALL_ADDR_REGS"
  "Registers that can hold the target of an indirect call.")

(define_constraint "I"
  "An unsigned 5-bit constant (MOVEQ, BSET, BCLR)."
  (and (match_code "const_int")
       (match_test "ival >= 0 && ival <= 31")))

(define_constraint "K"
  "A constant in the range 1..32 (ADDQ, SUBQ)."
  (and (match_code "const_int")
       (match_test "ival >= 1 && ival <= 32")))

(define_constraint "N"
  "A constant in the range -32..-1 (ADDQ/SUBQ of the negation)."
  (and (match_code "const_int")
       (match_test "ival >= -32 && ival <= -1")))

(define_constraint "L"
  "A signed 5-bit constant (CMPQ)."
  (and (match_code "const_int")
       (match_test "ival >= -16 && ival <= 15")))

(define_constraint "J"
  "A shift count in the range 1..31."
  (and (match_code "const_int")
       (match_test "ival >= 1 && ival <= 31")))

(define_constraint "M"
  "A mask of 1..31 low-order ones, done with a pair of shifts."
  (and (match_code "const_int")
       (match_test "jrisc_low_mask_p (ival)")))

(define_constraint "P"
  "A constant with exactly one bit set (BSET)."
  (and (match_code "const_int")
       (match_test "exact_log2 (ival & 0xffffffff) >= 0")))

(define_constraint "Q"
  "A constant with exactly one bit clear (BCLR)."
  (and (match_code "const_int")
       (match_test "exact_log2 (~ival & 0xffffffff) >= 0")))

(define_constraint "S"
  "A symbolic call target."
  (match_code "symbol_ref,label_ref,const"))
