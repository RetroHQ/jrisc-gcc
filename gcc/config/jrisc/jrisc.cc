/* Target code for the Atari Jaguar RISC processors (TOM GPU and
   JERRY DSP).
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published
   by the Free Software Foundation; either version 3, or (at your
   option) any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

#define IN_TARGET_CODE 1

#include "config.h"
#include "system.h"
#include "coretypes.h"
#include "backend.h"
#include "target.h"
#include "rtl.h"
#include "tree.h"
#include "stringpool.h"
#include "attribs.h"
#include "df.h"
#include "memmodel.h"
#include "tm_p.h"
#include "alias.h"
#include "regs.h"
#include "insn-config.h"
#include "emit-rtl.h"
#include "recog.h"
#include "diagnostic-core.h"
#include "output.h"
#include "insn-attr.h"
#include "stor-layout.h"
#include "varasm.h"
#include "calls.h"
#include "explow.h"
#include "expr.h"
#include "optabs.h"
#include "builtins.h"
#include "rtl-iter.h"
#include "lra.h"
#include "tm-constrs.h"

/* This file should be included last.  */
#include "target-def.h"

/* Per-function frame layout; see the comment in jrisc.h.  */

struct GTY(()) machine_function
{
  /* Has the frame layout been computed for the current function?  */
  bool frame_laid_out;

  /* Bytes of register save area and of the area just below the incoming
     stack arguments where argument registers are stored (the registers
     of a varargs function, or the register part of an argument split
     between registers and the stack); both are allocated by the first
     stack adjustment of the prologue.  */
  HOST_WIDE_INT saves_size;
  HOST_WIDE_INT varargs_size;

  /* Bytes of locals and outgoing arguments, allocated by the second.  */
  HOST_WIDE_INT locals_size;
  HOST_WIDE_INT outgoing_size;

  /* Registers to save, as a mask of hard register numbers.  */
  unsigned int save_mask;
};

static struct machine_function *
jrisc_init_machine_status (void)
{
  return ggc_cleared_alloc<machine_function> ();
}

static void
jrisc_option_override (void)
{
  init_machine_status = jrisc_init_machine_status;

  /* Trampolines are not supported yet.  */
  flag_trampolines = 0;
}

/* Registers and addressing.  */

/* Is REGNO usable as a base register?  STRICT says whether pseudos are
   allowed.  */

bool
jrisc_regno_ok_for_base_p (int regno, bool strict)
{
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      if (!strict)
	return true;
      if (!reg_renumber)
	return false;
      regno = reg_renumber[regno];
      if (regno < 0)
	return false;
    }
  return regno < 32 || regno == JRISC_SFP_REGNUM || regno == JRISC_AP_REGNUM;
}

static bool
jrisc_base_reg_p (rtx x, bool strict)
{
  return REG_P (x) && jrisc_regno_ok_for_base_p (REGNO (x), strict);
}

/* Can the base register X take a constant offset?  Only r14 and r15
   have base+offset addressing; the soft frame and argument pointers
   become one of them.  */

static bool
jrisc_offset_base_p (rtx x, bool strict)
{
  if (!REG_P (x))
    return false;
  int regno = REGNO (x);
  if (regno >= FIRST_PSEUDO_REGISTER)
    {
      if (regno <= LAST_VIRTUAL_REGISTER && !strict)
	return true;
      if (!strict || !reg_renumber)
	return false;
      regno = reg_renumber[regno];
    }
  return (regno == JRISC_FP_REGNUM || regno == JRISC_SP_REGNUM
	  || regno == JRISC_SFP_REGNUM || regno == JRISC_AP_REGNUM);
}

/* Implement TARGET_LEGITIMATE_ADDRESS_P.

   Any register can be used as a base on its own.  Word and doubleword
   accesses can add any multiple of 4 to r14 or r15: offsets up to 128
   use (rN+n), larger ones load the offset into r28 and use (rN+r28).
   Byte and halfword accesses (LOADB, LOADW, STOREB, STOREW) only have
   register indirect addressing.  */

static bool
jrisc_legitimate_address_p (machine_mode mode, rtx x, bool strict,
			    code_helper = ERROR_MARK)
{
  if (REG_P (x))
    return jrisc_base_reg_p (x, strict);

  if (GET_CODE (x) != PLUS
      || !REG_P (XEXP (x, 0))
      || !CONST_INT_P (XEXP (x, 1)))
    return false;

  rtx base = XEXP (x, 0);
  HOST_WIDE_INT offset = INTVAL (XEXP (x, 1));
  if (GET_MODE_SIZE (mode) < UNITS_PER_WORD)
    return false;
  if (!jrisc_offset_base_p (base, strict))
    return false;
  if (offset < -0x40000000 || offset > 0x40000000)
    return false;
  return (offset & 3) == 0;
}

/* Implement TARGET_LEGITIMATE_CONSTANT_P.  Floating-point constants go
   in the constant pool.  */

static bool
jrisc_legitimate_constant_p (machine_mode, rtx x)
{
  return !CONST_DOUBLE_P (x);
}

/* Implement TARGET_HARD_REGNO_MODE_OK.  */

static bool
jrisc_hard_regno_mode_ok (unsigned int regno, machine_mode mode)
{
  if (GET_MODE_CLASS (mode) == MODE_CC)
    return regno == JRISC_CC_REGNUM;
  if (regno == JRISC_CC_REGNUM)
    return false;
  if (regno >= 32)
    return GET_MODE_SIZE (mode) <= UNITS_PER_WORD;
  return regno + CEIL (GET_MODE_SIZE (mode), UNITS_PER_WORD) <= 32;
}

/* Implement TARGET_MODES_TIEABLE_P.  */

static bool
jrisc_modes_tieable_p (machine_mode mode1, machine_mode mode2)
{
  return ((GET_MODE_CLASS (mode1) == MODE_CC)
	  == (GET_MODE_CLASS (mode2) == MODE_CC));
}

/* Implement TARGET_FIXED_CONDITION_CODE_REGS.  */

static bool
jrisc_fixed_condition_code_regs (unsigned int *p1, unsigned int *p2)
{
  *p1 = JRISC_CC_REGNUM;
  *p2 = INVALID_REGNUM;
  return true;
}

/* Frame layout.  */

/* Does the prologue need to save hard register REGNO?  */

static bool
jrisc_save_reg_p (unsigned int regno)
{
  if (regno == JRISC_LR_REGNUM)
    return !crtl->is_leaf || df_regs_ever_live_p (regno);
  if (regno == JRISC_FP_REGNUM && frame_pointer_needed)
    return true;
  return (regno < 32
	  && df_regs_ever_live_p (regno)
	  && !call_used_or_fixed_reg_p (regno));
}

static void
jrisc_compute_frame (void)
{
  struct machine_function *m = cfun->machine;

  m->save_mask = 0;
  m->saves_size = 0;
  for (unsigned int regno = 0; regno < 32; regno++)
    if (jrisc_save_reg_p (regno))
      {
	m->save_mask |= 1u << regno;
	m->saves_size += UNITS_PER_WORD;
      }

  m->varargs_size = crtl->args.pretend_args_size;
  m->locals_size = ROUND_UP (get_frame_size (), UNITS_PER_WORD);
  m->outgoing_size = ROUND_UP (crtl->outgoing_args_size, UNITS_PER_WORD);
  if (reload_completed)
    m->frame_laid_out = true;
}

static HOST_WIDE_INT
jrisc_total_frame_size (void)
{
  struct machine_function *m = cfun->machine;
  return m->saves_size + m->varargs_size + m->locals_size + m->outgoing_size;
}

/* Implement INITIAL_ELIMINATION_OFFSET.  The hard frame pointer, when
   used, equals the stack pointer after the prologue, so the offsets to
   both are the same.  */

HOST_WIDE_INT
jrisc_initial_elimination_offset (int from, int to)
{
  jrisc_compute_frame ();
  gcc_assert (to == STACK_POINTER_REGNUM
	      || to == HARD_FRAME_POINTER_REGNUM);
  if (from == FRAME_POINTER_REGNUM)
    return cfun->machine->outgoing_size;
  /* The argument pointer points at the argument register save area
     (see jrisc.h), below the stack pointer on entry.  */
  if (from == ARG_POINTER_REGNUM)
    return jrisc_total_frame_size () - cfun->machine->varargs_size;
  gcc_unreachable ();
}

/* Implement TARGET_CAN_ELIMINATE.  */

static bool
jrisc_can_eliminate (const int, const int to)
{
  return to == HARD_FRAME_POINTER_REGNUM || !frame_pointer_needed;
}

rtx
jrisc_return_addr_rtx (int count, rtx)
{
  if (count != 0)
    return NULL_RTX;
  return get_hard_reg_initial_val (Pmode, JRISC_LR_REGNUM);
}

/* Emit SP += DELTA.  Small adjustments use ADDQ/SUBQ; larger ones load
   the magnitude into r28 and use ADD/SUB, the form GDB's prologue
   analyzer expects.  The insns are marked frame-related when
   FRAME_RELATED; NOTE is the CFI note kind to attach.  */

static void
jrisc_adjust_sp (HOST_WIDE_INT delta, bool frame_related,
		 enum reg_note note)
{
  rtx insn;

  if (delta == 0)
    return;

  if (delta >= -32 && delta <= 32)
    insn = emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,
				  GEN_INT (delta)));
  else
    {
      rtx temp = gen_rtx_REG (SImode, JRISC_TEMP_REGNUM);
      emit_move_insn (temp, GEN_INT (delta < 0 ? -delta : delta));
      if (delta < 0)
	insn = emit_insn (gen_subsi3 (stack_pointer_rtx, stack_pointer_rtx,
				      temp));
      else
	insn = emit_insn (gen_addsi3 (stack_pointer_rtx, stack_pointer_rtx,
				      temp));
    }

  if (frame_related)
    {
      RTX_FRAME_RELATED_P (insn) = 1;
      add_reg_note (insn, note,
		    gen_rtx_SET (stack_pointer_rtx,
				 plus_constant (Pmode, stack_pointer_rtx,
						delta)));
    }
}

/* Save or restore the registers in the save mask, at the bottom of the
   register save area which starts at the stack pointer.  */

static void
jrisc_save_restore_regs (bool save)
{
  struct machine_function *m = cfun->machine;
  HOST_WIDE_INT offset = 0;

  for (unsigned int regno = 0; regno < 32; regno++)
    {
      if (!(m->save_mask & (1u << regno)))
	continue;

      rtx reg = gen_rtx_REG (SImode, regno);
      rtx mem = gen_frame_mem (SImode,
			       plus_constant (Pmode, stack_pointer_rtx,
					      offset));
      rtx insn;
      if (save)
	{
	  insn = emit_move_insn (mem, reg);
	  RTX_FRAME_RELATED_P (insn) = 1;
	}
      else
	{
	  insn = emit_move_insn (reg, mem);
	  RTX_FRAME_RELATED_P (insn) = 1;
	  add_reg_note (insn, REG_CFA_RESTORE, reg);
	}
      offset += UNITS_PER_WORD;
    }
}

void
jrisc_expand_prologue (void)
{
  struct machine_function *m = cfun->machine;

  jrisc_compute_frame ();

  if (flag_stack_usage_info)
    current_function_static_stack_size = jrisc_total_frame_size ();

  /* Allocate and fill the register save area first, so that the saves
     are within reach of (r15+n) whatever the size of the frame.  */
  jrisc_adjust_sp (-(m->saves_size + m->varargs_size), true,
		   REG_FRAME_RELATED_EXPR);
  jrisc_save_restore_regs (true);
  jrisc_adjust_sp (-(m->locals_size + m->outgoing_size), true,
		   REG_FRAME_RELATED_EXPR);

  if (frame_pointer_needed)
    {
      rtx insn = emit_move_insn (hard_frame_pointer_rtx, stack_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
    }

  /* Don't let the scheduler move body code into the prologue.  */
  emit_insn (gen_blockage ());
}

void
jrisc_expand_epilogue (bool sibcall_p)
{
  struct machine_function *m = cfun->machine;

  jrisc_compute_frame ();
  HOST_WIDE_INT lower = m->locals_size + m->outgoing_size;
  HOST_WIDE_INT upper = m->saves_size + m->varargs_size;

  emit_insn (gen_blockage ());

  if (frame_pointer_needed)
    {
      rtx insn = emit_move_insn (stack_pointer_rtx, hard_frame_pointer_rtx);
      RTX_FRAME_RELATED_P (insn) = 1;
      add_reg_note (insn, REG_CFA_DEF_CFA,
		    plus_constant (Pmode, stack_pointer_rtx, lower + upper));
    }

  jrisc_adjust_sp (lower, true, REG_CFA_ADJUST_CFA);
  jrisc_save_restore_regs (false);
  jrisc_adjust_sp (upper, true, REG_CFA_ADJUST_CFA);

  if (!sibcall_p)
    emit_jump_insn (gen_jrisc_return ());
}

bool
jrisc_can_use_return_insn (void)
{
  if (!reload_completed)
    return false;
  jrisc_compute_frame ();
  return jrisc_total_frame_size () == 0;
}

/* Function arguments and return values.  */

static int
jrisc_arg_words (const function_arg_info &arg)
{
  HOST_WIDE_INT size = arg.promoted_size_in_bytes ();
  return CEIL (size, UNITS_PER_WORD);
}

/* Implement TARGET_FUNCTION_ARG.  Arguments go in r0-r5 and then on
   the stack.  An argument that starts in a register but does not fit is
   split between the last registers and the stack (see
   jrisc_arg_partial_bytes), so the registers saved by a varargs function
   and the stack arguments form one contiguous image for va_arg.  */

static rtx
jrisc_function_arg (cumulative_args_t cum_v, const function_arg_info &arg)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);

  if (arg.end_marker_p ())
    return NULL_RTX;
  if (*cum >= JRISC_NUM_ARG_REGS)
    return NULL_RTX;
  return gen_rtx_REG (arg.mode, JRISC_FIRST_ARG_REGNUM + *cum);
}

/* Implement TARGET_ARG_PARTIAL_BYTES.  */

static int
jrisc_arg_partial_bytes (cumulative_args_t cum_v, const function_arg_info &arg)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);
  int words = jrisc_arg_words (arg);

  if (*cum < JRISC_NUM_ARG_REGS && *cum + words > JRISC_NUM_ARG_REGS)
    return (JRISC_NUM_ARG_REGS - *cum) * UNITS_PER_WORD;
  return 0;
}

static void
jrisc_function_arg_advance (cumulative_args_t cum_v,
			    const function_arg_info &arg)
{
  CUMULATIVE_ARGS *cum = get_cumulative_args (cum_v);

  *cum = MIN (*cum + jrisc_arg_words (arg), JRISC_NUM_ARG_REGS);
}

static bool
jrisc_pass_by_reference (cumulative_args_t, const function_arg_info &arg)
{
  return arg.type && TREE_CODE (TYPE_SIZE_UNIT (arg.type)) != INTEGER_CST;
}

static bool
jrisc_return_in_memory (const_tree type, const_tree)
{
  HOST_WIDE_INT size = int_size_in_bytes (type);
  return size < 0 || size > 2 * UNITS_PER_WORD;
}

/* Values narrower than a word are returned extended to SImode, matching
   TARGET_PROMOTE_FUNCTION_MODE.  */

static rtx
jrisc_function_value (const_tree valtype, const_tree func, bool)
{
  machine_mode mode = TYPE_MODE (valtype);

  if (INTEGRAL_TYPE_P (valtype))
    {
      int unsignedp = TYPE_UNSIGNED (valtype);
      mode = promote_function_mode (valtype, mode, &unsignedp,
				    func && DECL_P (func)
				    ? TREE_TYPE (func) : func, 1);
    }
  return gen_rtx_REG (mode, JRISC_FIRST_ARG_REGNUM);
}

static rtx
jrisc_libcall_value (machine_mode mode, const_rtx)
{
  return gen_rtx_REG (mode, JRISC_FIRST_ARG_REGNUM);
}

static bool
jrisc_function_value_regno_p (const unsigned int regno)
{
  return regno == JRISC_FIRST_ARG_REGNUM
	 || regno == JRISC_FIRST_ARG_REGNUM + 1;
}

/* Implement TARGET_SETUP_INCOMING_VARARGS.  The unnamed argument
   registers are stored just below the incoming stack arguments, so that
   all the variable arguments are contiguous.  */

static void
jrisc_setup_incoming_varargs (cumulative_args_t cum_v,
			      const function_arg_info &arg,
			      int *pretend_size, int no_rtl)
{
  CUMULATIVE_ARGS local_cum = *get_cumulative_args (cum_v);

  if (!TYPE_NO_NAMED_ARGS_STDARG_P (TREE_TYPE (current_function_decl))
      || arg.type != NULL_TREE)
    jrisc_function_arg_advance (pack_cumulative_args (&local_cum), arg);

  int saved = JRISC_NUM_ARG_REGS - local_cum;
  if (saved <= 0)
    return;

  if (!no_rtl)
    {
      rtx mem = gen_frame_mem (BLKmode, virtual_incoming_args_rtx);
      set_mem_alias_set (mem, get_varargs_alias_set ());
      move_block_from_reg (JRISC_FIRST_ARG_REGNUM + local_cum, mem, saved);
    }
  *pretend_size = saved * UNITS_PER_WORD;
}

static bool
jrisc_function_ok_for_sibcall (tree decl, tree)
{
  /* Only direct calls: the target address is loaded into r28 after the
     epilogue has restored the callee-saved registers.  */
  return decl != NULL_TREE;
}

/* Moves.  */

/* Prepare the operands of a move in MODE.  Return true if the move has
   been emitted.  */

bool
jrisc_expand_move (machine_mode mode, rtx *operands)
{
  rtx dest = operands[0];
  rtx src = operands[1];

  if (!can_create_pseudo_p ())
    return false;

  if (MEM_P (dest) && !register_operand (src, mode))
    src = force_reg (mode, src);

  operands[1] = src;
  return false;
}

/* Multiplication.  The cores multiply 16 by 16 bits, so
   a * b = al * bl + ((ah * bl + al * bh) << 16).  */

void
jrisc_expand_mulsi3 (rtx dest, rtx a, rtx b)
{
  a = force_reg (SImode, a);
  b = force_reg (SImode, b);

  rtx t1 = expand_simple_binop (SImode, LSHIFTRT, a, GEN_INT (16),
				NULL_RTX, 1, OPTAB_DIRECT);
  rtx p1 = gen_reg_rtx (SImode);
  emit_insn (gen_umulhisi3 (p1, gen_lowpart (HImode, force_reg (SImode, t1)),
			    gen_lowpart (HImode, b)));

  rtx t2 = expand_simple_binop (SImode, LSHIFTRT, b, GEN_INT (16),
				NULL_RTX, 1, OPTAB_DIRECT);
  rtx p2 = gen_reg_rtx (SImode);
  emit_insn (gen_umulhisi3 (p2, gen_lowpart (HImode, force_reg (SImode, t2)),
			    gen_lowpart (HImode, a)));

  rtx cross = expand_simple_binop (SImode, PLUS, p1, p2, NULL_RTX, 1,
				   OPTAB_DIRECT);
  cross = expand_simple_binop (SImode, ASHIFT, cross, GEN_INT (16),
			       NULL_RTX, 1, OPTAB_DIRECT);

  rtx low = gen_reg_rtx (SImode);
  emit_insn (gen_umulhisi3 (low, gen_lowpart (HImode, a),
			    gen_lowpart (HImode, b)));

  rtx result = expand_simple_binop (SImode, PLUS, low, cross, dest, 1,
				    OPTAB_DIRECT);
  if (result != dest)
    emit_move_insn (dest, result);
}

/* Comparisons.  */

/* Expand cbranchsi4.  OPERANDS are the comparison, its two operands and
   the label.  */

void
jrisc_expand_cbranch (rtx *operands)
{
  enum rtx_code code = GET_CODE (operands[0]);
  rtx a = force_reg (SImode, operands[1]);
  rtx b = operands[2];
  rtx label = operands[3];

  /* Turn LEU into something CMP can test: x <= c is x < c + 1, and
     x <= y is y >= x.  */
  if (code == LEU)
    {
      if (CONST_INT_P (b) && INTVAL (b) != -1 && INTVAL (b) >= -17
	  && INTVAL (b) <= 14)
	{
	  code = LTU;
	  b = GEN_INT (INTVAL (b) + 1);
	}
      else
	{
	  code = GEU;
	  std::swap (a, b);
	  a = force_reg (SImode, a);
	}
    }

  if (code == EQ || code == NE || code == LTU || code == GEU || code == GTU)
    {
      if (!jrisc_cmp_operand (b, SImode))
	b = force_reg (SImode, b);
      rtx cmp = gen_rtx_fmt_ee (code, VOIDmode, a, b);
      emit_jump_insn (gen_jrisc_cbranch_uns (cmp, a, b, label));
      return;
    }

  gcc_assert (code == LT || code == GE || code == GT || code == LE);

  if (b == const0_rtx && code != LE)
    {
      rtx cmp = gen_rtx_fmt_ee (code, VOIDmode, a, const0_rtx);
      emit_jump_insn (gen_jrisc_cbranch_sz (cmp, a, label));
      return;
    }

  if (!REG_P (b) && !CONST_INT_P (b))
    b = force_reg (SImode, b);
  rtx cmp = gen_rtx_fmt_ee (code, VOIDmode, a, b);
  emit_jump_insn (gen_jrisc_cbranch_signed (cmp, a, b, label));
}

/* Split a compare-and-branch after reload.  OPERANDS are the
   comparison, its operands and the label; SCRATCH is the first of two
   consecutive scratch operands for signed comparisons.  */

void
jrisc_split_cbranch (rtx *operands, rtx scratch)
{
  enum rtx_code code = GET_CODE (operands[0]);
  rtx a = operands[1];
  rtx b = operands[2];
  rtx label = operands[3];
  rtx cc;
  machine_mode ccmode = CCmode;

  if (code == LT || code == GE || code == GT || code == LE)
    {
      if (scratch == NULL_RTX)
	{
	  /* Signed test against zero: the N flag is enough.  */
	  ccmode = CC_NZmode;
	}
      else
	{
	  /* Bias both operands by 0x80000000 and compare unsigned.  */
	  rtx t1 = scratch;
	  rtx t2 = operands[5];
	  emit_move_insn (t2, gen_int_mode (HOST_WIDE_INT_1U << 31, SImode));
	  emit_move_insn (t1, a);
	  emit_insn (gen_xorsi3 (t1, t1, t2));
	  if (CONST_INT_P (b))
	    emit_move_insn (t2, gen_int_mode (INTVAL (b)
					      ^ (HOST_WIDE_INT_1U << 31),
					      SImode));
	  else
	    emit_insn (gen_xorsi3 (t2, t2, b));
	  a = t1;
	  b = t2;
	  switch (code)
	    {
	    case LT: code = LTU; break;
	    case GE: code = GEU; break;
	    case GT: code = GTU; break;
	    case LE: code = GEU; std::swap (a, b); break;
	    default: gcc_unreachable ();
	    }
	}
    }

  cc = gen_rtx_REG (ccmode, JRISC_CC_REGNUM);
  emit_insn (gen_rtx_SET (cc, gen_rtx_COMPARE (ccmode, a, b)));
  rtx cond = gen_rtx_fmt_ee (code, VOIDmode, cc, const0_rtx);
  emit_jump_insn (gen_rtx_SET (pc_rtx,
			       gen_rtx_IF_THEN_ELSE (VOIDmode, cond,
						     gen_rtx_LABEL_REF
						     (VOIDmode, label),
						     pc_rtx)));
}

/* Output.  */

static const char *
jrisc_condition_name (rtx cond)
{
  machine_mode mode = GET_MODE (XEXP (cond, 0));
  switch (GET_CODE (cond))
    {
    case EQ: return "eq";
    case NE: return "ne";
    case LTU: gcc_assert (mode == CCmode); return "cs";
    case GEU: gcc_assert (mode == CCmode); return "cc";
    case GTU: gcc_assert (mode == CCmode); return "hi";
    case LT: gcc_assert (mode == CC_NZmode); return "mi";
    case GE: gcc_assert (mode == CC_NZmode); return "pl";
    case GT: gcc_assert (mode == CC_NZmode); return "nnnz";
    default: gcc_unreachable ();
    }
}

/* Is INSN a branch whose delay slot was not filled?  */

bool
jrisc_empty_delay_slot_p (rtx_insn *insn)
{
  rtx_insn *seq = NEXT_INSN (PREV_INSN (insn));
  return GET_CODE (PATTERN (seq)) != SEQUENCE;
}

/* Output a jump to LABEL, conditional on COND if that is not null.  */

const char *
jrisc_output_branch (rtx label, rtx cond)
{
  const char *cc = cond ? jrisc_condition_name (cond) : "t";
  char buf[64];
  rtx ops[1] = { label };

  if (get_attr_length (current_output_insn)
      - (jrisc_empty_delay_slot_p (current_output_insn) ? 2 : 0) == 2)
    snprintf (buf, sizeof buf, "jr\t%s,%%l0%%#", cc);
  else
    {
      output_asm_insn ("movei\t#%l0,r28", ops);
      snprintf (buf, sizeof buf, "jump\t%s,(r28)%%#", cc);
    }
  output_asm_insn (buf, ops);
  return "";
}

/* Output a call; OPERANDS[OP] is the address.  */

const char *
jrisc_output_call (rtx *operands, int op, bool sibcall)
{
  rtx addr = operands[op];
  rtx ops[1] = { addr };

  if (sibcall)
    {
      output_asm_insn ("movei\t%0,r28", ops);
      output_asm_insn ("jump\tt,(r28)", ops);
      output_asm_insn ("nop", ops);
      return "";
    }

  output_asm_insn ("move\tpc,r29", ops);
  if (REG_P (addr))
    {
      output_asm_insn ("jump\tt,(%0)", ops);
      output_asm_insn ("addq\t#6,r29", ops);
    }
  else
    {
      output_asm_insn ("movei\t%0,r28", ops);
      output_asm_insn ("jump\tt,(r28)", ops);
      output_asm_insn ("addq\t#12,r29", ops);
    }
  return "";
}

/* Split the address of MEM into a base register and a constant
   offset.  */

static void
jrisc_split_address (rtx mem, rtx *base, HOST_WIDE_INT *offset)
{
  rtx addr = XEXP (mem, 0);
  if (GET_CODE (addr) == PLUS)
    {
      *base = XEXP (addr, 0);
      *offset = INTVAL (XEXP (addr, 1));
    }
  else
    {
      *base = addr;
      *offset = 0;
    }
  gcc_assert (REG_P (*base));
}

/* Output (or, if EMIT is false, just measure) a word load or store
   between REG and the word at BASE + OFFSET.  Return the length.  */

static int
jrisc_word_access (bool load, rtx reg, rtx base, HOST_WIDE_INT offset,
		   bool emit)
{
  rtx ops[3] = { reg, base, GEN_INT (offset / 4) };
  int regno = REGNO (base);

  gcc_assert ((offset & 3) == 0);

  if (offset == 0)
    {
      if (emit)
	output_asm_insn (load ? "load\t(%1),%0" : "store\t%0,(%1)", ops);
      return 2;
    }

  gcc_assert (regno == JRISC_FP_REGNUM || regno == JRISC_SP_REGNUM);
  if (offset >= 4 && offset <= 128)
    {
      if (emit)
	output_asm_insn (load ? "load\t(%1+%c2),%0" : "store\t%0,(%1+%c2)",
			 ops);
      return 2;
    }

  ops[2] = GEN_INT (offset);
  if (emit)
    {
      output_asm_insn ("movei\t#%c2,r28", ops);
      output_asm_insn (load ? "load\t(%1+r28),%0" : "store\t%0,(%1+r28)",
		       ops);
    }
  return 8;
}

/* Output or measure the move OPERANDS[0] = OPERANDS[1] in MODE.  */

static int
jrisc_move (rtx *operands, machine_mode mode, bool emit)
{
  rtx dest = operands[0];
  rtx src = operands[1];
  int size = GET_MODE_SIZE (mode);
  rtx base;
  HOST_WIDE_INT offset;

  if (size == 8)
    {
      /* Two words, high first.  */
      int len = 0;
      if (REG_P (dest) && REG_P (src))
	{
	  rtx ops[4] = { gen_rtx_REG (SImode, REGNO (dest)),
			 gen_rtx_REG (SImode, REGNO (src)),
			 gen_rtx_REG (SImode, REGNO (dest) + 1),
			 gen_rtx_REG (SImode, REGNO (src) + 1) };
	  if (emit)
	    {
	      if (REGNO (dest) == REGNO (src) + 1)
		output_asm_insn ("move\t%3,%2\n\tmove\t%1,%0", ops);
	      else
		output_asm_insn ("move\t%1,%0\n\tmove\t%3,%2", ops);
	    }
	  return 4;
	}
      if (REG_P (dest) && CONST_INT_P (src))
	{
	  HOST_WIDE_INT v = INTVAL (src);
	  rtx hi[2] = { gen_rtx_REG (SImode, REGNO (dest)),
			gen_int_mode (v >> 32, SImode) };
	  rtx lo[2] = { gen_rtx_REG (SImode, REGNO (dest) + 1),
			gen_int_mode (v, SImode) };
	  return (jrisc_move (hi, SImode, emit)
		  + jrisc_move (lo, SImode, emit));
	}

      bool load = REG_P (dest);
      rtx mem = load ? src : dest;
      rtx reg = load ? dest : src;
      rtx hi = gen_rtx_REG (SImode, REGNO (reg));
      rtx lo = gen_rtx_REG (SImode, REGNO (reg) + 1);
      jrisc_split_address (mem, &base, &offset);

      if (offset != 0
	  || REGNO (base) == JRISC_FP_REGNUM
	  || REGNO (base) == JRISC_SP_REGNUM)
	{
	  if (load && REGNO (hi) == REGNO (base))
	    {
	      len += jrisc_word_access (true, lo, base, offset + 4, emit);
	      len += jrisc_word_access (true, hi, base, offset, emit);
	    }
	  else
	    {
	      len += jrisc_word_access (load, hi, base, offset, emit);
	      len += jrisc_word_access (load, lo, base, offset + 4, emit);
	    }
	  return len;
	}

      /* A general base register: reach the second word through r28,
	 using ADDQT so that the flags are left alone.  */
      rtx ops[3] = { hi, lo, base };
      if (emit)
	{
	  if (load && REGNO (hi) == REGNO (base))
	    output_asm_insn ("move\t%2,r28\n\taddqt\t#4,r28\n\tload\t(r28),%1\n\t"
			     "load\t(%2),%0", ops);
	  else if (load)
	    output_asm_insn ("load\t(%2),%0\n\tmove\t%2,r28\n\taddqt\t#4,r28\n\t"
			     "load\t(r28),%1", ops);
	  else
	    output_asm_insn ("store\t%0,(%2)\n\tmove\t%2,r28\n\taddqt\t#4,r28\n\t"
			     "store\t%1,(r28)", ops);
	}
      return 8;
    }

  if (REG_P (dest) && REG_P (src))
    {
      if (emit)
	output_asm_insn ("move\t%1,%0", operands);
      return 2;
    }

  if (REG_P (dest) && (CONST_INT_P (src) || CONSTANT_P (src)))
    {
      if (CONST_INT_P (src)
	  && INTVAL (src) >= 0 && INTVAL (src) <= 31)
	{
	  if (emit)
	    output_asm_insn ("moveq\t%1,%0", operands);
	  return 2;
	}
      if (emit)
	output_asm_insn ("movei\t%1,%0", operands);
      return 6;
    }

  bool load = REG_P (dest);
  rtx mem = load ? src : dest;
  rtx reg = load ? dest : src;
  gcc_assert (MEM_P (mem) && REG_P (reg));
  jrisc_split_address (mem, &base, &offset);

  if (size == 4)
    return jrisc_word_access (load, reg, base, offset, emit);

  /* Bytes and halfwords.  */
  gcc_assert (offset == 0);
  if (emit)
    {
      rtx ops[2] = { reg, base };
      if (load)
	output_asm_insn (size == 1 ? "loadb\t(%1),%0" : "loadw\t(%1),%0",
			 ops);
      else
	output_asm_insn (size == 1 ? "storeb\t%0,(%1)" : "storew\t%0,(%1)",
			 ops);
    }
  return 2;
}

const char *
jrisc_output_move (rtx *operands, machine_mode mode)
{
  jrisc_move (operands, mode, true);
  return "";
}

/* Output or measure a zero (SIGNED_P false) or sign extension from
   MODE.  */

static int
jrisc_extend (rtx *operands, machine_mode mode, bool signed_p, bool emit)
{
  int shift = mode == QImode ? 24 : 16;
  rtx ops[2] = { operands[0], GEN_INT (shift) };
  int len = 0;

  if (MEM_P (operands[1]))
    {
      rtx base;
      HOST_WIDE_INT offset;
      rtx mem = operands[1];
      jrisc_split_address (mem, &base, &offset);
      rtx lops[2] = { operands[0], base };
      gcc_assert (offset == 0);
      if (emit)
	output_asm_insn (mode == QImode ? "loadb\t(%1),%0"
			 : "loadw\t(%1),%0", lops);
      len += 2;
      /* LOADB and LOADW zero-extend.  */
      if (!signed_p)
	return len;
    }

  if (emit)
    output_asm_insn (signed_p ? "shlq\t%1,%0\n\tsharq\t%1,%0"
		     : "shlq\t%1,%0\n\tshrq\t%1,%0", ops);
  return len + 4;
}

const char *
jrisc_output_extend (rtx *operands, machine_mode mode, bool signed_p)
{
  jrisc_extend (operands, mode, signed_p, true);
  return "";
}

/* Return the length of a move or extension INSN.  */

int
jrisc_insn_length (rtx_insn *insn)
{
  extract_constrain_insn_cached (insn);
  rtx set = single_set (insn);
  gcc_assert (set);
  rtx src = SET_SRC (set);
  machine_mode mode = GET_MODE (SET_DEST (set));

  if (GET_CODE (src) == ZERO_EXTEND || GET_CODE (src) == SIGN_EXTEND)
    return jrisc_extend (recog_data.operand, GET_MODE (XEXP (src, 0)),
			 GET_CODE (src) == SIGN_EXTEND, false);
  return jrisc_move (recog_data.operand, mode, false);
}

/* Is C a mask of 1..31 low-order ones?  */

bool
jrisc_low_mask_p (HOST_WIDE_INT c)
{
  c &= 0xffffffff;
  return c != 0 && c != 0xffffffff && (c & (c + 1)) == 0;
}

/* Implement TARGET_PRINT_OPERAND.

   %#	"nop" if the delay slot of the current insn was not filled.
   %n	the negation of a constant, without '#'.
   %c	a constant without '#'.  */

static void
jrisc_print_operand (FILE *file, rtx x, int code)
{
  switch (code)
    {
    case '#':
      if (dbr_sequence_length () == 0)
	fputs ("\n\tnop", file);
      return;

    case 'n':
      gcc_assert (CONST_INT_P (x));
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, -INTVAL (x));
      return;

    case 'c':
      gcc_assert (CONST_INT_P (x));
      fprintf (file, HOST_WIDE_INT_PRINT_DEC, INTVAL (x));
      return;

    case 0:
      break;

    default:
      output_operand_lossage ("invalid operand code '%c'", code);
      return;
    }

  switch (GET_CODE (x))
    {
    case REG:
      fputs (reg_names[REGNO (x)], file);
      break;

    case MEM:
      output_address (GET_MODE (x), XEXP (x, 0));
      break;

    case CONST_INT:
      fprintf (file, "#" HOST_WIDE_INT_PRINT_DEC,
	       trunc_int_for_mode (INTVAL (x), SImode));
      break;

    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
    case CODE_LABEL:
      fputc ('#', file);
      output_addr_const (file, x);
      break;

    default:
      output_operand_lossage ("invalid operand");
      break;
    }
}

static void
jrisc_print_operand_address (FILE *file, machine_mode, rtx addr)
{
  if (REG_P (addr))
    fprintf (file, "(%s)", reg_names[REGNO (addr)]);
  else if (GET_CODE (addr) == PLUS
	   && REG_P (XEXP (addr, 0))
	   && CONST_INT_P (XEXP (addr, 1))
	   && INTVAL (XEXP (addr, 1)) >= 4
	   && INTVAL (XEXP (addr, 1)) <= 128
	   && (INTVAL (XEXP (addr, 1)) & 3) == 0)
    fprintf (file, "(%s+" HOST_WIDE_INT_PRINT_DEC ")",
	     reg_names[REGNO (XEXP (addr, 0))], INTVAL (XEXP (addr, 1)) / 4);
  else
    output_operand_lossage ("invalid address");
}

static bool
jrisc_print_operand_punct_valid_p (unsigned char code)
{
  return code == '#';
}

/* Costs.  */

static bool
jrisc_rtx_costs (rtx x, machine_mode mode, int outer_code, int,
		 int *total, bool speed)
{
  int code = GET_CODE (x);

  switch (code)
    {
    case CONST_INT:
      {
	HOST_WIDE_INT v = INTVAL (x);
	if ((outer_code == PLUS || outer_code == MINUS)
	    && v >= -32 && v <= 32)
	  *total = 0;
	else if (outer_code == COMPARE && v >= -16 && v <= 15)
	  *total = 0;
	else if ((outer_code == ASHIFT || outer_code == LSHIFTRT
		  || outer_code == ASHIFTRT || outer_code == ROTATERT
		  || outer_code == ROTATE)
		 && v >= 0 && v <= 31)
	  *total = 0;
	else if (v >= 0 && v <= 31)
	  *total = COSTS_N_INSNS (1) / 2;
	else
	  *total = COSTS_N_INSNS (speed ? 2 : 3);
	return true;
      }

    case SYMBOL_REF:
    case LABEL_REF:
    case CONST:
    case CONST_DOUBLE:
      *total = COSTS_N_INSNS (speed ? 2 : 3);
      return true;

    case MEM:
      *total = COSTS_N_INSNS (2);
      return false;

    case MULT:
      if (GET_CODE (XEXP (x, 0)) == ZERO_EXTEND
	  || GET_CODE (XEXP (x, 0)) == SIGN_EXTEND)
	*total = COSTS_N_INSNS (2);
      else if (mode == SImode)
	*total = COSTS_N_INSNS (10);
      else
	*total = COSTS_N_INSNS (40);
      return false;

    case DIV:
    case MOD:
    case UDIV:
    case UMOD:
      *total = COSTS_N_INSNS (mode == SImode && code == UDIV ? 18 : 40);
      return false;

    case ZERO_EXTEND:
    case SIGN_EXTEND:
      *total = MEM_P (XEXP (x, 0)) ? COSTS_N_INSNS (2) : COSTS_N_INSNS (2);
      return false;

    default:
      return false;
    }
}

static int
jrisc_address_cost (rtx addr, machine_mode, addr_space_t, bool)
{
  if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
    {
      HOST_WIDE_INT off = INTVAL (XEXP (addr, 1));
      return (off >= 4 && off <= 128) ? 0 : 3;
    }
  return 0;
}

static int
jrisc_memory_move_cost (machine_mode, reg_class_t, bool)
{
  return 4;
}

/* Implement TARGET_ASM_FILE_START.  Record the processor in the
   assembly so that the file assembles correctly on its own.  */

static void
jrisc_asm_file_start (void)
{
  default_file_start ();
  fputs (TARGET_DSP ? "\t.dsp\n" : "\t.gpu\n", asm_out_file);
}

/* Target hooks.  */

#undef  TARGET_OPTION_OVERRIDE
#define TARGET_OPTION_OVERRIDE jrisc_option_override

#undef  TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P jrisc_legitimate_address_p
#undef  TARGET_LEGITIMATE_CONSTANT_P
#define TARGET_LEGITIMATE_CONSTANT_P jrisc_legitimate_constant_p
#undef  TARGET_HARD_REGNO_MODE_OK
#define TARGET_HARD_REGNO_MODE_OK jrisc_hard_regno_mode_ok
#undef  TARGET_MODES_TIEABLE_P
#define TARGET_MODES_TIEABLE_P jrisc_modes_tieable_p
#undef  TARGET_FIXED_CONDITION_CODE_REGS
#define TARGET_FIXED_CONDITION_CODE_REGS jrisc_fixed_condition_code_regs
#undef  TARGET_CAN_ELIMINATE
#define TARGET_CAN_ELIMINATE jrisc_can_eliminate

#undef  TARGET_FUNCTION_ARG
#define TARGET_FUNCTION_ARG jrisc_function_arg
#undef  TARGET_ARG_PARTIAL_BYTES
#define TARGET_ARG_PARTIAL_BYTES jrisc_arg_partial_bytes
#undef  TARGET_FUNCTION_ARG_ADVANCE
#define TARGET_FUNCTION_ARG_ADVANCE jrisc_function_arg_advance
#undef  TARGET_PASS_BY_REFERENCE
#define TARGET_PASS_BY_REFERENCE jrisc_pass_by_reference
#undef  TARGET_MUST_PASS_IN_STACK
#define TARGET_MUST_PASS_IN_STACK must_pass_in_stack_var_size
#undef  TARGET_RETURN_IN_MEMORY
#define TARGET_RETURN_IN_MEMORY jrisc_return_in_memory
#undef  TARGET_FUNCTION_VALUE
#define TARGET_FUNCTION_VALUE jrisc_function_value
#undef  TARGET_LIBCALL_VALUE
#define TARGET_LIBCALL_VALUE jrisc_libcall_value
#undef  TARGET_FUNCTION_VALUE_REGNO_P
#define TARGET_FUNCTION_VALUE_REGNO_P jrisc_function_value_regno_p
#undef  TARGET_PROMOTE_PROTOTYPES
#define TARGET_PROMOTE_PROTOTYPES hook_bool_const_tree_true
#undef  TARGET_PROMOTE_FUNCTION_MODE
#define TARGET_PROMOTE_FUNCTION_MODE \
  default_promote_function_mode_always_promote
#undef  TARGET_SETUP_INCOMING_VARARGS
#define TARGET_SETUP_INCOMING_VARARGS jrisc_setup_incoming_varargs
#undef  TARGET_FUNCTION_OK_FOR_SIBCALL
#define TARGET_FUNCTION_OK_FOR_SIBCALL jrisc_function_ok_for_sibcall

#undef  TARGET_PRINT_OPERAND
#define TARGET_PRINT_OPERAND jrisc_print_operand
#undef  TARGET_PRINT_OPERAND_ADDRESS
#define TARGET_PRINT_OPERAND_ADDRESS jrisc_print_operand_address
#undef  TARGET_PRINT_OPERAND_PUNCT_VALID_P
#define TARGET_PRINT_OPERAND_PUNCT_VALID_P jrisc_print_operand_punct_valid_p
#undef  TARGET_ASM_FILE_START
#define TARGET_ASM_FILE_START jrisc_asm_file_start

#undef  TARGET_RTX_COSTS
#define TARGET_RTX_COSTS jrisc_rtx_costs
#undef  TARGET_ADDRESS_COST
#define TARGET_ADDRESS_COST jrisc_address_cost
#undef  TARGET_MEMORY_MOVE_COST
#define TARGET_MEMORY_MOVE_COST jrisc_memory_move_cost

#undef  TARGET_CONSTANT_ALIGNMENT
#define TARGET_CONSTANT_ALIGNMENT constant_alignment_word_strings

#undef  TARGET_HAVE_SPECULATION_SAFE_VALUE
#define TARGET_HAVE_SPECULATION_SAFE_VALUE speculation_safe_value_not_needed

struct gcc_target targetm = TARGET_INITIALIZER;

#include "gt-jrisc.h"
