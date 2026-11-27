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
#include "opts.h"
#include "output.h"
#include "insn-attr.h"
#include "stor-layout.h"
#include "varasm.h"
#include "calls.h"
#include "explow.h"
#include "expr.h"
#include "optabs.h"
#include "builtins.h"
#include "predict.h"
#include "gimple.h"
#include "gimple-iterator.h"
#include "gimple-walk.h"
#include "ssa.h"
#include "cgraph.h"
#include "langhooks.h"
#include "value-query.h"
#include "tree-ssa-live.h"
#include "tree-outof-ssa.h"
#include "rtl-iter.h"
#include "lra.h"
#include "tm-constrs.h"
#include "tree-pass.h"
#include "context.h"

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

  /* The function's local variables, as expansion starts (expansion
     takes them off the function's list).  */
  vec<tree, va_gc> *locals;

  /* Bytes of locals and outgoing arguments, allocated by the second.  */
  HOST_WIDE_INT locals_size;
  HOST_WIDE_INT outgoing_size;

  /* Registers to save, as a mask of hard register numbers.  */
  unsigned int save_mask;

  /* Bytes of the frame in main RAM, allocated from r27, and the
     register its locals are addressed from: r27, or a pseudo set from
     it if alloca moves r27.  */
  HOST_WIDE_INT dram_frame_size;
  rtx dram_base;

  /* Whether r27 may move in the body (alloca).  */
  bool dram_dynamic;

  /* Parameters that are word-only but not safe in local RAM, from
     jrisc_find_unsafe_locals: their homes, if they need one, go in the
     frame in main RAM (see jrisc_stack_object_mem).  */
  vec<tree, va_gc> *dram_parms;
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

  /* Code runs from the small local RAM, so the linker lays out larger
     programs as overlays, which it builds from whole input sections:
     put every function in a section of its own.  */
  if (!OPTION_SET_P (flag_function_sections))
    flag_function_sections = 1;

  /* The DSP store bug is the DSP's only.  */
  if (jrisc_fix_dsp_stores < 0)
    jrisc_fix_dsp_stores = TARGET_DSP;
}

/* Attributes.

   resident	  A function that is never put in an overlay.  Interrupt
		  handlers and code that runs while overlays are being
		  loaded must be resident.
   overlay (N)	  Put a function in overlay group N (1-255): the linker
		  keeps the functions of a group in the same overlay.
   local	  Put a variable in local RAM (.ldata or .lbss) rather
		  than main RAM.  Local RAM can only be accessed a word at
		  a time, so the variable may not have parts smaller than
		  a word.  */

/* Can every part of an object of TYPE be accessed as whole, aligned
   words?  */

static bool
jrisc_word_only_type_p (const_tree type)
{
  switch (TREE_CODE (type))
    {
    case ARRAY_TYPE:
    case VECTOR_TYPE:
    case COMPLEX_TYPE:
      return jrisc_word_only_type_p (TREE_TYPE (type));

    case RECORD_TYPE:
    case UNION_TYPE:
    case QUAL_UNION_TYPE:
      for (tree f = TYPE_FIELDS (type); f; f = DECL_CHAIN (f))
	if (TREE_CODE (f) == FIELD_DECL
	    && (DECL_BIT_FIELD (f)
		|| !tree_fits_uhwi_p (bit_position (f))
		|| tree_to_uhwi (bit_position (f)) % BITS_PER_WORD != 0
		|| !jrisc_word_only_type_p (TREE_TYPE (f))))
	  return false;
      return true;

    default:
      return (TYPE_SIZE_UNIT (type) != NULL_TREE
	      && tree_fits_uhwi_p (TYPE_SIZE_UNIT (type))
	      && tree_to_uhwi (TYPE_SIZE_UNIT (type)) % UNITS_PER_WORD == 0
	      && TYPE_ALIGN (type) >= BITS_PER_WORD);
    }
}

/* Is BASE a local variable or a parameter of this function?  */

static bool
jrisc_auto_decl_p (const_tree base)
{
  return base != NULL_TREE
	 && (VAR_P (base) || TREE_CODE (base) == PARM_DECL);
}

/* If OP is an aggregate, or a part of one, add the local variable or
   parameter it is in to SET.  */

static void
jrisc_note_whole_aggregate (hash_set<tree> *set, tree op)
{
  if (op == NULL_TREE || !AGGREGATE_TYPE_P (TREE_TYPE (op)))
    return;
  tree base = get_base_address (op);
  if (jrisc_auto_decl_p (base))
    set->add (base);
}

/* Whether expanding a call with argument or result OP allocates space
   on the main RAM stack.  */

static bool
jrisc_variable_size_p (tree op)
{
  return (op != NULL_TREE
	  && TYPE_SIZE_UNIT (TREE_TYPE (op)) != NULL_TREE
	  && TREE_CODE (TYPE_SIZE_UNIT (TREE_TYPE (op))) != INTEGER_CST);
}

/* walk_stmt_load_store_addr_ops callbacks for jrisc_find_unsafe_locals.
   A load or store of anything but whole, aligned words makes BASE
   unsafe in local RAM.  */

static bool
jrisc_note_access (gimple *, tree base, tree op, void *data)
{
  if (jrisc_auto_decl_p (base)
      && (!jrisc_word_only_type_p (TREE_TYPE (op))
	  || get_object_alignment (op) < BITS_PER_WORD))
    ((hash_set<tree> *) data)->add (base);
  return false;
}

/* Whether the value of an assignment with code CODE is derived from its
   operands as a pointer may be, by offsetting it or converting it.  */

static bool
jrisc_pointer_arith_code_p (enum tree_code code)
{
  switch (code)
    {
    case SSA_NAME:
    case ADDR_EXPR:
    case NOP_EXPR:
    case CONVERT_EXPR:
    case VIEW_CONVERT_EXPR:
    case PLUS_EXPR:
    case MINUS_EXPR:
    case POINTER_PLUS_EXPR:
    case BIT_AND_EXPR:
    case COND_EXPR:
    case MIN_EXPR:
    case MAX_EXPR:
      return true;
    default:
      return false;
    }
}

/* If REF is a memory reference whose address is based on NAME, return
   whether the access is of whole words; otherwise return -1.  */

static int
jrisc_word_access_through_p (tree ref, tree name)
{
  tree base = ref;
  while (handled_component_p (base))
    base = TREE_OPERAND (base, 0);
  if ((TREE_CODE (base) == MEM_REF && TREE_OPERAND (base, 0) == name)
      || (TREE_CODE (base) == TARGET_MEM_REF
	  && (TMR_BASE (base) == name || TMR_INDEX (base) == name
	      || TMR_INDEX2 (base) == name)))
    return jrisc_word_only_type_p (TREE_TYPE (ref));
  return -1;
}

/* Whether the address computed by STMT (the address of a local, or an
   address derived from it) is used only to access whole words of it:
   follow the SSA names it flows into.  */

static bool
jrisc_address_contained_p (gimple *stmt)
{
  auto_vec<tree> worklist;
  hash_set<tree> seen;

  if (gphi *phi = dyn_cast <gphi *> (stmt))
    worklist.safe_push (gimple_phi_result (phi));
  else if (is_gimple_assign (stmt)
	   && TREE_CODE (gimple_assign_lhs (stmt)) == SSA_NAME
	   && jrisc_pointer_arith_code_p (gimple_assign_rhs_code (stmt)))
    worklist.safe_push (gimple_assign_lhs (stmt));
  else
    return false;

  while (!worklist.is_empty ())
    {
      tree name = worklist.pop ();
      if (seen.add (name))
	continue;

      imm_use_iterator iter;
      gimple *use;
      FOR_EACH_IMM_USE_STMT (use, iter, name)
	{
	  if (is_gimple_debug (use))
	    continue;
	  if (gphi *phi = dyn_cast <gphi *> (use))
	    {
	      worklist.safe_push (gimple_phi_result (phi));
	      continue;
	    }
	  if (gimple_code (use) == GIMPLE_COND)
	    continue;
	  if (!is_gimple_assign (use))
	    return false;

	  tree lhs = gimple_assign_lhs (use);
	  enum tree_code code = gimple_assign_rhs_code (use);
	  if (TREE_CODE_CLASS (code) == tcc_comparison)
	    continue;
	  if (gimple_assign_single_p (use))
	    {
	      /* A load or store through NAME?  */
	      int l = jrisc_word_access_through_p (lhs, name);
	      int r = jrisc_word_access_through_p (gimple_assign_rhs1 (use),
						   name);
	      if (l == 0 || r == 0)
		return false;
	      if (l > 0 || r > 0)
		{
		  /* NAME itself must not be the value stored.  */
		  if (gimple_assign_rhs1 (use) == name)
		    return false;
		  continue;
		}
	    }
	  if (TREE_CODE (lhs) == SSA_NAME && jrisc_pointer_arith_code_p (code))
	    {
	      worklist.safe_push (lhs);
	      continue;
	    }
	  return false;
	}
    }
  return true;
}

/* Taking the address of BASE in STMT (other than for a memory reference)
   lets it escape, perhaps to code that accesses it as bytes, unless the
   address is only used for word accesses.  */

static bool
jrisc_note_address (gimple *stmt, tree base, tree, void *data)
{
  base = get_base_address (base);
  if (jrisc_auto_decl_p (base)
      && (gimple_code (stmt) == GIMPLE_ASM
	  || !jrisc_address_contained_p (stmt)))
    ((hash_set<tree> *) data)->add (base);
  return false;
}

/* Find the locals and parameters that are not safe in local RAM, where
   only whole words may be accessed (see the comment at the top of
   jrisc.h), and add them to SET:

   - those whose address escapes;
   - those accessed other than as whole, aligned words;
   - those used in asm statements;
   - aggregates used as a whole, which expand may do with a byte-by-byte
     memcpy or by passing their address (aggregate arguments and results
     are passed by reference): call arguments and results, returned
     values and aggregate assignments.

   Return true if a call has a variable-sized argument or result, which
   moves r27 like alloca.  */

static bool
jrisc_find_unsafe_locals (hash_set<tree> *set)
{
  bool dynamic = false;
  basic_block bb;

  FOR_EACH_BB_FN (bb, cfun)
    {
      for (gphi_iterator gpi = gsi_start_phis (bb); !gsi_end_p (gpi);
	   gsi_next (&gpi))
	{
	  gphi *phi = gpi.phi ();
	  for (unsigned int i = 0; i < gimple_phi_num_args (phi); i++)
	    {
	      tree arg = gimple_phi_arg_def (phi, i);
	      if (TREE_CODE (arg) == ADDR_EXPR)
		jrisc_note_address (phi, TREE_OPERAND (arg, 0), arg, set);
	    }
	}

      for (gimple_stmt_iterator gsi = gsi_start_bb (bb); !gsi_end_p (gsi);
	   gsi_next (&gsi))
	{
	  gimple *stmt = gsi_stmt (gsi);

	  if (is_gimple_debug (stmt) || gimple_clobber_p (stmt))
	    continue;

	  if (gimple_code (stmt) == GIMPLE_ASM)
	    walk_stmt_load_store_addr_ops (stmt, set, jrisc_note_address,
					   jrisc_note_address,
					   jrisc_note_address);
	  else
	    walk_stmt_load_store_addr_ops (stmt, set, jrisc_note_access,
					   jrisc_note_access,
					   jrisc_note_address);

	  if (gcall *call = dyn_cast <gcall *> (stmt))
	    {
	      for (unsigned int i = 0; i < gimple_call_num_args (call); i++)
		{
		  tree arg = gimple_call_arg (call, i);
		  jrisc_note_whole_aggregate (set, arg);
		  dynamic |= jrisc_variable_size_p (arg);
		}
	      jrisc_note_whole_aggregate (set, gimple_call_lhs (call));
	      dynamic |= jrisc_variable_size_p (gimple_call_lhs (call));
	    }
	  else if (greturn *ret = dyn_cast <greturn *> (stmt))
	    jrisc_note_whole_aggregate (set, gimple_return_retval (ret));
	  else if (gimple_assign_single_p (stmt)
		   && AGGREGATE_TYPE_P (TREE_TYPE (gimple_assign_lhs (stmt))))
	    {
	      jrisc_note_whole_aggregate (set, gimple_assign_lhs (stmt));
	      jrisc_note_whole_aggregate (set, gimple_assign_rhs1 (stmt));
	    }
	}
    }
  return dynamic;
}

/* The frame in main RAM (see the comment at the top of jrisc.h).  Should
   local VAR go in it rather than on the stack in local RAM?  UNSAFE is
   the set from jrisc_find_unsafe_locals.  */

static bool
jrisc_dram_local_p (tree var, hash_set<tree> *unsafe)
{
  if (!VAR_P (var)
      || TREE_TYPE (var) == error_mark_node
      || is_global_var (var)
      || DECL_EXTERNAL (var)
      || DECL_HAS_VALUE_EXPR_P (var)
      || DECL_RTL_SET_P (var)
      || DECL_HARD_REGISTER (var)
      || is_gimple_reg (var)
      || use_register_for_decl (var))
    return false;

  /* Variable-sized objects are allocated with alloca, which uses the
     main RAM stack.  */
  tree size = DECL_SIZE_UNIT (var);
  if (size == NULL_TREE || !tree_fits_uhwi_p (size))
    return false;

  /* TREE_ADDRESSABLE is not used: ivopts and others take addresses only
     to make word accesses.  */
  return (unsafe->contains (var)
	  || !jrisc_word_only_type_p (TREE_TYPE (var))
	  || tree_to_uhwi (size) > (unsigned HOST_WIDE_INT)
				    jrisc_local_frame_limit);
}

/* Return a slot of SIZE bytes with alignment ALIGN (in bytes) in the
   frame in main RAM, as a MEM of MODE.  */

static rtx
jrisc_dram_slot (machine_mode mode, unsigned HOST_WIDE_INT size,
		 unsigned int align)
{
  struct machine_function *m = cfun->machine;

  /* If r27 can move (alloca), jrisc_expand_to_rtl_hook has put its value
     after the prologue in a pseudo.  */
  if (m->dram_base == NULL_RTX)
    m->dram_base = gen_rtx_REG (Pmode, JRISC_DRAM_SP_REGNUM);

  HOST_WIDE_INT offset = ROUND_UP (m->dram_frame_size,
				   MAX (align, UNITS_PER_WORD));
  m->dram_frame_size = offset + ROUND_UP (MAX (size, 1), UNITS_PER_WORD);
  return gen_rtx_MEM (mode, plus_constant (Pmode, m->dram_base, offset));
}

/* Implement TARGET_EXPAND_TO_RTL_HOOK.  Give the locals that go in main
   RAM their places there, before expand_used_vars puts the rest on the
   stack.  */

static void
jrisc_expand_to_rtl_hook (void)
{
  struct machine_function *m = cfun->machine;
  unsigned int i;
  tree var;

  m->dram_frame_size = 0;
  m->dram_base = NULL_RTX;

  /* At -O0, parameters are kept in stack slots of their own type, where
     the parts of complex and vector values of chars and shorts would
     share words: keep those in pseudos.  (Other values that are not
     word-only and have no register go in main RAM: see
     jrisc_stack_object_mem.)  */
  if (!optimize)
    for (var = DECL_ARGUMENTS (current_function_decl); var;
	 var = DECL_CHAIN (var))
      if ((TREE_CODE (TREE_TYPE (var)) == COMPLEX_TYPE
	   || TREE_CODE (TREE_TYPE (var)) == VECTOR_TYPE)
	  && DECL_MODE (var) != BLKmode
	  && !TREE_ADDRESSABLE (var)
	  && !jrisc_word_only_type_p (TREE_TYPE (var)))
	DECL_REGISTER (var) = 1;

  m->locals = NULL;
  FOR_EACH_LOCAL_DECL (cfun, i, var)
    vec_safe_push (m->locals, var);

  hash_set<tree> unsafe;
  m->dram_dynamic = (jrisc_find_unsafe_locals (&unsafe)
		     || cfun->calls_alloca);

  m->dram_parms = NULL;
  for (var = DECL_ARGUMENTS (current_function_decl); var;
       var = DECL_CHAIN (var))
    if (unsafe.contains (var))
      vec_safe_push (m->dram_parms, var);

  /* If r27 can move, set up the base register for the frame in main RAM
     now, at the start of the function: slots for temporaries may be
     allocated anywhere in it.  If it is not used, it is deleted.  */
  if (m->dram_dynamic)
    {
      m->dram_base = gen_reg_rtx (Pmode);
      emit_move_insn (m->dram_base,
		      gen_rtx_REG (Pmode, JRISC_DRAM_SP_REGNUM));
      mark_reg_pointer (m->dram_base, BITS_PER_WORD);
    }

  FOR_EACH_LOCAL_DECL (cfun, i, var)
    if (jrisc_dram_local_p (var, &unsafe))
      {
	rtx mem = jrisc_dram_slot (DECL_MODE (var),
				   tree_to_uhwi (DECL_SIZE_UNIT (var)),
				   DECL_ALIGN_UNIT (var));
	set_mem_attributes (mem, var, true);
	SET_DECL_RTL (var, mem);
      }
}

/* Implement TARGET_STACK_OBJECT_MEM.  SSA values that need memory,
   temporaries and the homes of parameters that are not word-only, such
   as vectors of chars or shorts, or (at -O0, or if their address is
   taken) chars and shorts, go in the frame in main RAM, and so do the
   homes of parameters that jrisc_find_unsafe_locals found unsafe in
   local RAM (an int whose address escapes, say).  */

static rtx
jrisc_stack_object_mem (tree obj)
{
  tree type = TYPE_P (obj) ? obj : TREE_TYPE (obj);
  tree size = TYPE_SIZE_UNIT (type);

  if (size == NULL_TREE || !tree_fits_uhwi_p (size))
    return NULL_RTX;

  /* With -mfix-dsp-stores, the slot for a value returned in memory,
     whose address is passed to the function, is in main RAM: the
     function then stores to it a halfword at a time (see
     jrisc_dram_mem_p).  The temporaries of such types go there.  */
  bool return_slot = (jrisc_fix_dsp_stores
		      && TYPE_P (obj)
		      && AGGREGATE_TYPE_P (type)
		      && aggregate_value_p (type, NULL_TREE));

  /* Parts of vectors and complex values are extracted with BIT_FIELD_REF
     and the like, which can access the memory a byte at a time.  */
  bool parm = TREE_CODE (obj) == PARM_DECL;
  if (!return_slot
      && !(parm && vec_safe_contains (cfun->machine->dram_parms, obj))
      && (jrisc_word_only_type_p (type)
	  && TREE_CODE (type) != VECTOR_TYPE
	  && TREE_CODE (type) != COMPLEX_TYPE
	  && tree_to_uhwi (size) <= (unsigned HOST_WIDE_INT)
				    jrisc_local_frame_limit))
    return NULL_RTX;

  rtx mem = jrisc_dram_slot (parm ? DECL_MODE (obj) : TYPE_MODE (type),
			     tree_to_uhwi (size), TYPE_ALIGN_UNIT (type));
  set_mem_attributes (mem, parm ? obj : type, true);
  return mem;
}

static bool
jrisc_local_var_p (const_tree decl)
{
  return (VAR_P (decl)
	  && (lookup_attribute ("local", DECL_ATTRIBUTES (decl)) != NULL_TREE
	      || (TYPE_ADDR_SPACE (strip_array_types (TREE_TYPE (decl)))
		  == ADDR_SPACE_LOCAL)));
}

/* Put the function *NODE in section NAME, unless it already has one.  */

static bool
jrisc_set_function_section (tree *node, tree attr, const char *section)
{
  if (TREE_CODE (*node) != FUNCTION_DECL)
    {
      warning (OPT_Wattributes, "%qE attribute only applies to functions",
	       attr);
      return false;
    }
  if (DECL_SECTION_NAME (*node) != NULL
      && strcmp (DECL_SECTION_NAME (*node), section) != 0)
    {
      error ("%q+D: %qE attribute conflicts with section %qs", *node, attr,
	     DECL_SECTION_NAME (*node));
      return false;
    }
  set_decl_section_name (*node, section);
  return true;
}

static tree
jrisc_handle_resident_attribute (tree *node, tree name, tree, int,
				 bool *no_add_attrs)
{
  if (lookup_attribute ("overlay", DECL_ATTRIBUTES (*node)))
    {
      error ("%q+D: %qE and %<overlay%> attributes are incompatible",
	     *node, name);
      *no_add_attrs = true;
    }
  else if (!jrisc_set_function_section (node, name, ".text.resident"))
    *no_add_attrs = true;
  return NULL_TREE;
}

static tree
jrisc_handle_overlay_attribute (tree *node, tree name, tree args, int,
				bool *no_add_attrs)
{
  tree arg = TREE_VALUE (args);
  char section[32];

  if (lookup_attribute ("resident", DECL_ATTRIBUTES (*node)))
    {
      error ("%q+D: %<resident%> and %qE attributes are incompatible",
	     *node, name);
      *no_add_attrs = true;
      return NULL_TREE;
    }
  if (TREE_CODE (arg) != INTEGER_CST
      || !tree_fits_uhwi_p (arg)
      || tree_to_uhwi (arg) < 1
      || tree_to_uhwi (arg) > 255)
    {
      error ("%qE attribute argument must be an integer from 1 to 255",
	     name);
      *no_add_attrs = true;
      return NULL_TREE;
    }
  snprintf (section, sizeof (section), ".text.overlay.%u",
	    (unsigned) tree_to_uhwi (arg));
  if (!jrisc_set_function_section (node, name, section))
    *no_add_attrs = true;
  return NULL_TREE;
}

static tree
jrisc_handle_local_attribute (tree *node, tree name, tree, int,
			      bool *no_add_attrs)
{
  tree decl = *node;

  if (!VAR_P (decl) || !(TREE_STATIC (decl) || DECL_EXTERNAL (decl)))
    {
      warning (OPT_Wattributes, "%qE attribute only applies to variables "
	       "with static storage", name);
      *no_add_attrs = true;
    }
  else if (!jrisc_word_only_type_p (TREE_TYPE (decl)))
    {
      error ("%q+D: local RAM can only be accessed a word at a time, so "
	     "%qE variables must not have parts smaller than a word",
	     decl, name);
      *no_add_attrs = true;
    }
  else
    DECL_COMMON (decl) = 0;
  return NULL_TREE;
}

TARGET_GNU_ATTRIBUTES (jrisc_attribute_table,
{
  /* { name, min_len, max_len, decl_req, type_req, fn_type_req,
       affects_type_identity, handler, exclude } */
  { "resident", 0, 0, true, false, false, false,
    jrisc_handle_resident_attribute, NULL },
  { "overlay", 1, 1, true, false, false, false,
    jrisc_handle_overlay_attribute, NULL },
  { "local", 0, 0, true, false, false, false,
    jrisc_handle_local_attribute, NULL }
});

/* Sections for "local" variables.  */

static section *
jrisc_select_section (tree decl, int reloc, unsigned HOST_WIDE_INT align)
{
  if (jrisc_local_var_p (decl))
    return get_named_section (decl, bss_initializer_p (decl)
				    ? ".lbss" : ".ldata", reloc);
  return default_elf_select_section (decl, reloc, align);
}

static void
jrisc_unique_section (tree decl, int reloc)
{
  if (jrisc_local_var_p (decl))
    {
      const char *name
	= IDENTIFIER_POINTER (DECL_ASSEMBLER_NAME (decl));

      name = targetm.strip_name_encoding (name);
      set_decl_section_name (decl, ACONCAT ((bss_initializer_p (decl)
					     ? ".lbss." : ".ldata.",
					     name, NULL)));
      return;
    }
  default_unique_section (decl, reloc);
}

static unsigned int
jrisc_section_type_flags (tree decl, const char *name, int reloc)
{
  unsigned int flags = default_section_type_flags (decl, name, reloc);

  if (strcmp (name, ".lbss") == 0 || startswith (name, ".lbss."))
    flags |= SECTION_BSS;
  return flags;
}

/* Implement ASM_OUTPUT_ALIGNED_DECL_LOCAL: output the static variable
   DECL, called NAME, SIZE bytes long and aligned to ALIGN bits, which has
   no initializer.  "local" variables go in .lbss.  */

void
jrisc_output_aligned_decl_local (FILE *stream, tree decl, const char *name,
				 unsigned HOST_WIDE_INT size,
				 unsigned int align)
{
  if (decl && jrisc_local_var_p (decl))
    {
      switch_to_section (get_named_section (NULL, ".lbss", 0));
      ASM_OUTPUT_ALIGN (stream, floor_log2 (align / BITS_PER_UNIT));
      ASM_DECLARE_OBJECT_NAME (stream, name, decl);
      ASM_OUTPUT_SKIP (stream, size ? size : 1);
      return;
    }
  ASM_OUTPUT_ALIGNED_LOCAL (stream, name, size, align);
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

static bool jrisc_dram_mem_p (rtx);
static void jrisc_emit_dram_store (rtx, rtx);

/* Expand a block copy (SRC non-null) or a block set to VALUE of LENGTH
   bytes at DST, with alignment ALIGN, with word loads and stores.  This
   is needed for objects in local RAM, which may not be accessed as bytes
   (see the comment at the top of jrisc.h); they are word-aligned and a
   whole number of words long.  On the DSP, a block known to be in main
   RAM is stored a halfword at a time (see jrisc_dram_mem_p).  Return
   false to leave it to memcpy or memset.  */

bool
jrisc_expand_block_op (rtx dst, rtx src, rtx length, rtx value, rtx align)
{
  if (!CONST_INT_P (length)
      || !CONST_INT_P (align)
      || INTVAL (align) < UNITS_PER_WORD
      || INTVAL (length) <= 0
      || INTVAL (length) % UNITS_PER_WORD != 0)
    return false;

  rtx word = NULL_RTX;
  if (src == NULL_RTX)
    {
      if (!CONST_INT_P (value))
	return false;
      HOST_WIDE_INT byte = INTVAL (value) & 0xff;
      word = force_reg (SImode, gen_int_mode (byte * 0x01010101, SImode));
    }

  /* Decided here, where DST still says what it is.  */
  bool dram = jrisc_fix_dsp_stores && jrisc_dram_mem_p (dst);

  HOST_WIDE_INT words = INTVAL (length) / UNITS_PER_WORD;
  HOST_WIDE_INT unroll = optimize_insn_for_size_p () ? 4 : 8;

  if (words <= unroll)
    {
      for (HOST_WIDE_INT i = 0; i < words; i++)
	{
	  rtx d = adjust_address (dst, SImode, i * UNITS_PER_WORD);
	  rtx v = (src != NULL_RTX
		   ? force_reg (SImode, adjust_address (src, SImode,
							i * UNITS_PER_WORD))
		   : word);
	  if (dram)
	    jrisc_emit_dram_store (d, v);
	  else
	    emit_move_insn (d, v);
	}
      return true;
    }

  /* A loop, one word at a time.  */
  rtx daddr = copy_addr_to_reg (XEXP (dst, 0));
  rtx saddr = src != NULL_RTX ? copy_addr_to_reg (XEXP (src, 0)) : NULL_RTX;
  rtx count = force_reg (SImode, GEN_INT (words));
  rtx_code_label *loop = gen_label_rtx ();

  emit_label (loop);
  rtx d = change_address (dst, SImode, daddr);
  if (src != NULL_RTX)
    {
      rtx tmp = gen_reg_rtx (SImode);
      emit_move_insn (tmp, change_address (src, SImode, saddr));
      if (dram)
	jrisc_emit_dram_store (d, tmp);
      else
	emit_move_insn (d, tmp);
      emit_insn (gen_addsi3 (saddr, saddr, GEN_INT (UNITS_PER_WORD)));
    }
  else if (dram)
    jrisc_emit_dram_store (d, word);
  else
    emit_move_insn (d, word);
  emit_insn (gen_addsi3 (daddr, daddr, GEN_INT (UNITS_PER_WORD)));
  emit_insn (gen_addsi3 (count, count, constm1_rtx));
  emit_cmp_and_jump_insns (count, const0_rtx, NE, NULL_RTX, SImode, true,
			   loop);
  return true;
}

/* Implement TARGET_NARROW_MEM_OK_P.  The stack is in local RAM, where a
   word may not be read or written in parts (see the comment at the top
   of jrisc.h), so a value on it may not be narrowed below a word by
   accessing it in a narrower mode.  */

static bool
jrisc_narrow_mem_ok_p (machine_mode mode, const_rtx mem)
{
  if (GET_MODE_SIZE (mode) >= UNITS_PER_WORD)
    return true;

  const_rtx addr = XEXP (mem, 0);
  if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
    addr = XEXP (addr, 0);
  if (!REG_P (addr))
    return true;

  unsigned int regno = REGNO (addr);
  return !(regno == STACK_POINTER_REGNUM
	  || regno == JRISC_FP_REGNUM
	  || regno == FRAME_POINTER_REGNUM
	  || regno == ARG_POINTER_REGNUM
	  || (regno >= FIRST_VIRTUAL_REGISTER
	      && regno <= LAST_VIRTUAL_REGISTER));
}

/* Implement TARGET_CONDITIONAL_REGISTER_USAGE.  r27, the main RAM stack
   pointer, is global: every call uses it, and the value it is left with
   matters even if this function does not read it again.  */

static void
jrisc_conditional_register_usage (void)
{
  global_regs[JRISC_DRAM_SP_REGNUM] = 1;
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
  /* The main RAM stack pointer is restored from its save slot.  */
  if (regno == JRISC_DRAM_SP_REGNUM)
    return cfun->machine->dram_frame_size != 0 || cfun->calls_alloca;
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

/* Emit REG += DELTA and return the last insn, or null if DELTA is 0.
   Small adjustments use ADDQ/SUBQ; larger ones load the magnitude into
   r28 and use ADD/SUB, the form GDB's prologue analyzer expects.  */

static rtx
jrisc_adjust_reg (rtx reg, HOST_WIDE_INT delta)
{
  if (delta == 0)
    return NULL_RTX;
  if (delta >= -32 && delta <= 32)
    return emit_insn (gen_addsi3 (reg, reg, GEN_INT (delta)));

  rtx temp = gen_rtx_REG (SImode, JRISC_TEMP_REGNUM);
  emit_move_insn (temp, GEN_INT (delta < 0 ? -delta : delta));
  if (delta < 0)
    return emit_insn (gen_subsi3 (reg, reg, temp));
  return emit_insn (gen_addsi3 (reg, reg, temp));
}

/* Emit SP += DELTA.  The insns are marked frame-related when
   FRAME_RELATED; NOTE is the CFI note kind to attach.  */

static void
jrisc_adjust_sp (HOST_WIDE_INT delta, bool frame_related,
		 enum reg_note note)
{
  rtx insn = jrisc_adjust_reg (stack_pointer_rtx, delta);

  if (insn == NULL_RTX)
    return;

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

  /* Allocate the frame in main RAM.  r27 was saved above, so its value
     in the caller is known without any more CFI.  */
  jrisc_adjust_reg (gen_rtx_REG (Pmode, JRISC_DRAM_SP_REGNUM),
		    -m->dram_frame_size);

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

/* Aggregates are passed by reference and copied by the callee, so that
   the copy is a local like any other (see the comment at the top of
   jrisc.h).  */

static bool
jrisc_pass_by_reference (cumulative_args_t, const function_arg_info &arg)
{
  return (arg.type
	  && (AGGREGATE_TYPE_P (arg.type)
	      || TREE_CODE (TYPE_SIZE_UNIT (arg.type)) != INTEGER_CST));
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

/* X is an operand of a move in MODE.  If it is a SUBREG of memory,
   replace it with memory, or for a part of a word that is read, with
   that part of a register loaded with the whole word (see
   jrisc_narrow_mem_ok_p).  */

static rtx
jrisc_resolve_subreg_mem (rtx x, machine_mode mode, bool read)
{
  if (GET_CODE (x) != SUBREG || !MEM_P (SUBREG_REG (x)))
    return x;

  rtx mem = SUBREG_REG (x);
  poly_uint64 byte = SUBREG_BYTE (x);
  unsigned HOST_WIDE_INT offset;

  if (read
      && GET_MODE_SIZE (mode) < UNITS_PER_WORD
      && byte.is_constant (&offset))
    {
      rtx word = force_reg (SImode,
			    adjust_address (mem, SImode,
					    offset & ~(UNITS_PER_WORD - 1)));
      return simplify_gen_subreg (mode, word, SImode,
				  offset & (UNITS_PER_WORD - 1));
    }
  return adjust_address (mem, mode, byte);
}

/* The DSP's stores to main RAM (Atari's "Jaguar Hardware Bugs &
   Warnings", JDEV0002, GPU/DSP item 7).  The DSP is on the main bus 16
   bits wide, and a long word store there can write the wrong data (its
   high half in both halves), above all while the other processors are
   busy on the bus; its word and byte stores do not fail.  With
   -mfix-dsp-stores, a long word store that is known to be to main RAM
   is made as two STOREWs.  A long word store that may be to local RAM,
   which can only be written a long word at a time (through a pointer,
   say), is a long word store after a load of the same location
   (jrisc_guard_store_p), which makes failures much rarer.  */

/* Is VAR, a variable, in main RAM?  */

static bool
jrisc_dram_var_p (tree var)
{
  if (jrisc_local_var_p (var) || DECL_HARD_REGISTER (var))
    return false;
  if (is_global_var (var))
    {
      /* A variable that is put in the local RAM sections by name.  */
      const char *name = DECL_SECTION_NAME (var);
      return !(name
	       && (startswith (name, ".ldata") || startswith (name, ".lbss")));
    }
  if (!DECL_RTL_SET_P (var) || !MEM_P (DECL_RTL (var)))
    return false;
  rtx addr = XEXP (DECL_RTL (var), 0);
  if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
    addr = XEXP (addr, 0);
  return (REG_P (addr)
	  && (REGNO (addr) == JRISC_DRAM_SP_REGNUM
	      || addr == cfun->machine->dram_base));
}

/* Is T this function's result, returned in memory?  With
   -mfix-dsp-stores, its caller puts it in main RAM (see
   jrisc_stack_object_mem).  */

static bool
jrisc_dram_result_p (tree t)
{
  return (TREE_CODE (t) == RESULT_DECL
	  && jrisc_fix_dsp_stores
	  && (DECL_BY_REFERENCE (t)
	      || aggregate_value_p (t, current_function_decl)));
}

/* Can the SSA pointer PTR only point to objects in main RAM, by its
   points-to information?  Not if it may point to anything unknown or
   escaped, to a local in local RAM, or to a "local" variable.  */

static bool
jrisc_dram_pointer_p (tree ptr)
{
  if (TREE_CODE (ptr) != SSA_NAME || !POINTER_TYPE_P (TREE_TYPE (ptr)))
    return false;
  struct ptr_info_def *pi = SSA_NAME_PTR_INFO (ptr);
  if (pi == NULL)
    return false;
  struct pt_solution *pt = &pi->pt;
  if (pt->anything || pt->nonlocal || pt->escaped || pt->ipa_escaped
      || pt->vars_contains_escaped_heap
      || pt->vars == NULL || bitmap_empty_p (pt->vars))
    return false;

  unsigned int i;
  tree var;
  FOR_EACH_VEC_SAFE_ELT (cfun->machine->locals, i, var)
    if (pt_solution_includes (pt, var) && !jrisc_dram_var_p (var))
      return false;
  for (var = DECL_ARGUMENTS (current_function_decl); var;
       var = DECL_CHAIN (var))
    if (pt_solution_includes (pt, var))
      return false;
  if (DECL_RESULT (current_function_decl) != NULL_TREE
      && !jrisc_dram_result_p (DECL_RESULT (current_function_decl))
      && pt_solution_includes (pt, DECL_RESULT (current_function_decl)))
    return false;

  if (pt->vars_contains_nonlocal)
    {
      varpool_node *vnode;
      FOR_EACH_VARIABLE (vnode)
	if (!jrisc_dram_var_p (vnode->decl)
	    && pt_solution_includes (pt, vnode->decl))
	  return false;
    }
  return true;
}

/* Is the address T known to be in main RAM?  T is an address
   expression or an SSA name, which may be a pointer with points-to
   information or an integer (as ivopts makes them), followed back
   through copies, conversions, constant offsets and PHIs to the objects
   it is based on.  VISITED holds the SSA names on the way.  */

static bool
jrisc_dram_address_p (tree t, hash_set<tree> *visited)
{
  switch (TREE_CODE (t))
    {
    case INTEGER_CST:
      return tree_fits_uhwi_p (t) && tree_to_uhwi (t) < JRISC_LOCAL_SPACE;

    case ADDR_EXPR:
      {
	tree base = TREE_OPERAND (t, 0);
	while (handled_component_p (base))
	  base = TREE_OPERAND (base, 0);
	if (TREE_CODE (base) == MEM_REF)
	  return jrisc_dram_address_p (TREE_OPERAND (base, 0), visited);
	return ((VAR_P (base) && jrisc_dram_var_p (base))
		|| jrisc_dram_result_p (base));
      }

    case SSA_NAME:
      break;

    case RESULT_DECL:
      /* The result's address, as a value of its own.  */
      return DECL_BY_REFERENCE (t) && jrisc_dram_result_p (t);

    default:
      return false;
    }

  if (visited->add (t))
    return true;
  if (POINTER_TYPE_P (TREE_TYPE (t)) && jrisc_dram_pointer_p (t))
    return true;
  if (SSA_NAME_IS_DEFAULT_DEF (t))
    return (SSA_NAME_VAR (t) != NULL_TREE
	    && TREE_CODE (SSA_NAME_VAR (t)) == RESULT_DECL
	    && DECL_BY_REFERENCE (SSA_NAME_VAR (t))
	    && jrisc_dram_result_p (SSA_NAME_VAR (t)));
  if (visited->elements () > 32)
    return false;

  gimple *def = SSA_NAME_DEF_STMT (t);
  if (gphi *phi = dyn_cast <gphi *> (def))
    {
      for (unsigned int i = 0; i < gimple_phi_num_args (phi); i++)
	if (!jrisc_dram_address_p (gimple_phi_arg_def (phi, i), visited))
	  return false;
      return true;
    }
  if (!is_gimple_assign (def))
    return false;
  switch (gimple_assign_rhs_code (def))
    {
    case SSA_NAME:
    case ADDR_EXPR:
    case NOP_EXPR:
    case CONVERT_EXPR:
      return jrisc_dram_address_p (gimple_assign_rhs1 (def), visited);

    case POINTER_PLUS_EXPR:
      return jrisc_dram_address_p (gimple_assign_rhs1 (def), visited);

    case PLUS_EXPR:
    case MINUS_EXPR:
      return (TREE_CODE (gimple_assign_rhs2 (def)) == INTEGER_CST
	      && jrisc_dram_address_p (gimple_assign_rhs1 (def), visited));

    default:
      return false;
    }
}

/* Is MEM known to be in main RAM, and not volatile (a volatile long word
   is left a single store)?  */

static bool
jrisc_dram_mem_p (rtx mem)
{
  if (MEM_VOLATILE_P (mem) || MEM_ADDR_SPACE (mem) == ADDR_SPACE_LOCAL)
    return false;

  rtx addr = XEXP (mem, 0);
  if (GET_CODE (addr) == PLUS && CONST_INT_P (XEXP (addr, 1)))
    addr = XEXP (addr, 0);
  if (REG_P (addr)
      && (REGNO (addr) == JRISC_DRAM_SP_REGNUM
	  || (cfun->machine->dram_base != NULL_RTX
	      && addr == cfun->machine->dram_base)))
    return true;
  if (CONST_INT_P (addr))
    return (UINTVAL (addr) & 0xffffffff) < JRISC_LOCAL_SPACE;

  tree expr = MEM_EXPR (mem);
  while (expr && handled_component_p (expr))
    expr = TREE_OPERAND (expr, 0);
  if (expr
      && (TREE_CODE (expr) == MEM_REF || TREE_CODE (expr) == TARGET_MEM_REF))
    {
      tree base = TREE_OPERAND (expr, 0);
      if (TREE_CODE (base) == INTEGER_CST)
	return (tree_fits_uhwi_p (base)
		&& tree_to_uhwi (base) < JRISC_LOCAL_SPACE);
      if (TREE_CODE (base) == SSA_NAME || TREE_CODE (base) == RESULT_DECL)
	{
	  hash_set<tree> visited;
	  return jrisc_dram_address_p (base, &visited);
	}
      if (TREE_CODE (base) != ADDR_EXPR)
	return false;
      expr = TREE_OPERAND (base, 0);
      while (handled_component_p (expr))
	expr = TREE_OPERAND (expr, 0);
    }
  return (expr != NULL_TREE
	  && ((VAR_P (expr) && jrisc_dram_var_p (expr))
	      || jrisc_dram_result_p (expr)));
}

/* Store SImode register V as two words at MEM, an SImode MEM in main
   RAM.  */

static void
jrisc_emit_dram_store (rtx mem, rtx v)
{
  rtx hi = gen_reg_rtx (SImode);
  emit_insn (gen_lshrsi3 (hi, v, GEN_INT (16)));
  emit_move_insn (adjust_address (mem, HImode, 0), gen_lowpart (HImode, hi));
  emit_move_insn (adjust_address (mem, HImode, 2), gen_lowpart (HImode, v));
}

bool
jrisc_expand_move (machine_mode mode, rtx *operands)
{
  rtx dest = operands[0];
  rtx src = operands[1];

  if (!can_create_pseudo_p ())
    return false;

  dest = jrisc_resolve_subreg_mem (dest, mode, false);
  src = jrisc_resolve_subreg_mem (src, mode, true);
  operands[0] = dest;

  if (MEM_P (dest) && !register_operand (src, mode))
    src = force_reg (mode, src);

  operands[1] = src;

  if (jrisc_fix_dsp_stores
      && MEM_P (dest)
      && (GET_MODE_SIZE (mode) == 4 || GET_MODE_SIZE (mode) == 8)
      && jrisc_dram_mem_p (dest))
    {
      for (int w = 0; w < GET_MODE_SIZE (mode) / 4; w++)
	jrisc_emit_dram_store (adjust_address (dest, SImode, w * 4),
			       force_reg (SImode,
					  operand_subword_force (src, w,
								 mode)));
      return true;
    }
  return false;
}

/* Expand an extension of OPERANDS[1], in MODE, to SImode OPERANDS[0].
   If OPERANDS[1] is the low part of an SImode register, extend that
   register with shifts or a mask and return true.  */

/* Is X the low part of an SImode register?  */

bool
jrisc_word_lowpart_p (rtx x)
{
  return (GET_CODE (x) == SUBREG
	  && REG_P (SUBREG_REG (x))
	  && GET_MODE (SUBREG_REG (x)) == SImode
	  && subreg_lowpart_p (x));
}

bool
jrisc_expand_extend (rtx *operands, machine_mode mode, bool signed_p)
{
  rtx src = operands[1];
  if (!jrisc_word_lowpart_p (src))
    return false;

  rtx reg = SUBREG_REG (src);
  int bits = BITS_PER_WORD - GET_MODE_BITSIZE (mode);
  if (signed_p)
    {
      rtx tmp = expand_simple_binop (SImode, ASHIFT, reg, GEN_INT (bits),
				     NULL_RTX, 0, OPTAB_DIRECT);
      tmp = expand_simple_binop (SImode, ASHIFTRT, tmp, GEN_INT (bits),
				 operands[0], 0, OPTAB_DIRECT);
      if (tmp != operands[0])
	emit_move_insn (operands[0], tmp);
    }
  else
    {
      rtx tmp = expand_simple_binop (SImode, AND, reg,
				     GEN_INT (GET_MODE_MASK (mode)),
				     operands[0], 1, OPTAB_DIRECT);
      if (tmp != operands[0])
	emit_move_insn (operands[0], tmp);
    }
  return true;
}

/* Return an SImode register whose low half is X, a HImode register
   operand.  */

rtx
jrisc_word_reg (rtx x)
{
  if (GET_CODE (x) == SUBREG
      && GET_MODE (SUBREG_REG (x)) == SImode
      && REG_P (SUBREG_REG (x))
      && subreg_lowpart_p (x))
    return SUBREG_REG (x);
  x = force_reg (HImode, x);
  return simplify_gen_subreg (SImode, x, HImode, 0);
}

/* Multiplication.  The cores multiply 16 by 16 bits, so
   a * b = al * bl + ((ah * bl + al * bh) << 16).  */

/* Is X, an operand of an insn being expanded from GIMPLE, known to be
   within [LO, HI]?  A pseudo is if it is that of SSA names (those
   coalesced into it) all of whose value ranges are.  */

static bool
jrisc_rtx_in_range_p (rtx x, HOST_WIDE_INT lo, HOST_WIDE_INT hi)
{
  if (CONST_INT_P (x))
    return IN_RANGE (INTVAL (x), lo, hi);
  if (!REG_P (x) || HARD_REGISTER_P (x) || !currently_expanding_to_rtl
      || !SA.map)
    return false;

  bool found = false;
  unsigned int i;
  tree name;
  FOR_EACH_SSA_NAME (i, name, cfun)
    {
      if (virtual_operand_p (name))
	continue;
      int p = var_to_partition (SA.map, name);
      if (p == NO_PARTITION || SA.partition_to_pseudo[p] != x)
	continue;
      if (!INTEGRAL_TYPE_P (TREE_TYPE (name)))
	return false;
      int_range_max r;
      if (!get_global_range_query ()->range_of_expr (r, name)
	  || r.varying_p ())
	return false;
      if (!r.undefined_p ())
	{
	  signop sgn = TYPE_SIGN (TREE_TYPE (name));
	  unsigned int prec = TYPE_PRECISION (TREE_TYPE (name));
	  /* Compared in the type's signedness: for an unsigned type a
	     negative LO is a large number, so no signed range holds.  */
	  if (prec != 32
	      || !wi::ge_p (r.lower_bound (), wi::shwi (lo, prec), sgn)
	      || !wi::le_p (r.upper_bound (), wi::shwi (hi, prec), sgn))
	    return false;
	}
      found = true;
    }
  return found;
}

/* If A and B, the operands of a 32-bit multiplication, are known to fit
   in 16 bits, signed or unsigned, expand it as the one IMULT or MULT
   that does it.  A widening multiplication is usually seen as such in
   GIMPLE, but not when loop optimizations have moved the conversions
   from 16 bits into PHIs.  */

static bool
jrisc_expand_narrow_mulsi3 (rtx dest, rtx a, rtx b)
{
  rtx (*gen) (rtx, rtx, rtx);
  if (jrisc_rtx_in_range_p (a, -32768, 32767)
      && jrisc_rtx_in_range_p (b, -32768, 32767))
    gen = gen_jrisc_smul16;
  else if (jrisc_rtx_in_range_p (a, 0, 65535)
	   && jrisc_rtx_in_range_p (b, 0, 65535))
    gen = gen_jrisc_umul16;
  else
    return false;

  /* The insns read the low 16 bits of each register.  */
  rtx t = gen_reg_rtx (SImode);
  emit_insn (gen (t, force_reg (SImode, a), force_reg (SImode, b)));
  emit_move_insn (dest, t);
  return true;
}

void
jrisc_expand_mulsi3 (rtx dest, rtx a, rtx b)
{
  if (jrisc_expand_narrow_mulsi3 (dest, a, b))
    return;

  a = force_reg (SImode, a);
  b = force_reg (SImode, b);

  rtx t1 = expand_simple_binop (SImode, LSHIFTRT, a, GEN_INT (16),
				NULL_RTX, 1, OPTAB_DIRECT);
  rtx p1 = gen_reg_rtx (SImode);
  emit_insn (gen_jrisc_umul16 (p1, force_reg (SImode, t1), b));

  rtx t2 = expand_simple_binop (SImode, LSHIFTRT, b, GEN_INT (16),
				NULL_RTX, 1, OPTAB_DIRECT);
  rtx p2 = gen_reg_rtx (SImode);
  emit_insn (gen_jrisc_umul16 (p2, force_reg (SImode, t2), a));

  rtx cross = expand_simple_binop (SImode, PLUS, p1, p2, NULL_RTX, 1,
				   OPTAB_DIRECT);
  cross = expand_simple_binop (SImode, ASHIFT, cross, GEN_INT (16),
			       NULL_RTX, 1, OPTAB_DIRECT);

  rtx low = gen_reg_rtx (SImode);
  emit_insn (gen_jrisc_umul16 (low, a, b));

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

/* A long word store on the DSP that may be to main RAM but is not known
   to be (see jrisc_dram_mem_p) is preceded by a load of the same
   location into r28, and by a MOVE of r28, which waits for that load
   (and leaves the flags alone): a DSP store to main RAM that follows a
   completed load from main RAM fails much less often.  A store to local
   RAM loads from local RAM, which costs a little and does no harm.

   Is the guard needed before a store to MEM, whose address is BASE?  Not
   for the stack and frame in local RAM, the __local address space, a
   "local" variable, nor a
   constant address outside main RAM (a hardware register, whose reads
   may have side effects).  */

static bool
jrisc_guard_store_p (rtx mem, rtx base)
{
  if (!jrisc_fix_dsp_stores)
    return false;
  if (REGNO (base) == JRISC_FP_REGNUM || REGNO (base) == JRISC_SP_REGNUM)
    return false;
  if (mem && MEM_ADDR_SPACE (mem) == ADDR_SPACE_LOCAL)
    return false;
  tree expr = mem ? MEM_EXPR (mem) : NULL_TREE;
  while (expr && handled_component_p (expr))
    expr = TREE_OPERAND (expr, 0);
  if (expr && VAR_P (expr) && jrisc_local_var_p (expr))
    return false;
  /* Hardware registers and the local RAM, by a constant address.  */
  if (expr
      && TREE_CODE (expr) == MEM_REF
      && TREE_CODE (TREE_OPERAND (expr, 0)) == INTEGER_CST
      && !(tree_fits_uhwi_p (TREE_OPERAND (expr, 0))
	   && tree_to_uhwi (TREE_OPERAND (expr, 0)) < JRISC_LOCAL_SPACE))
    return false;
  return true;
}

/* Output (or measure) the guard for a long word store at (BASE).
   Return its length.  */

static int
jrisc_store_guard (rtx base, bool emit)
{
  if (emit)
    {
      rtx ops[1] = { base };
      output_asm_insn ("load\t(%0),r28\n\tmove\tr28,r28", ops);
    }
  return 4;
}

/* Output (or, if EMIT is false, just measure) a word load or store
   between REG and the word at BASE + OFFSET.  Return the length.  MEM
   is the memory operand, if there is one, to tell whether a store needs
   the DSP store guard.  */

static int
jrisc_word_access (bool load, rtx reg, rtx base, HOST_WIDE_INT offset,
		   bool emit, rtx mem = NULL_RTX)
{
  rtx ops[3] = { reg, base, GEN_INT (offset / 4) };
  int regno = REGNO (base);

  gcc_assert ((offset & 3) == 0);

  if (offset == 0)
    {
      int len = 0;
      if (!load && jrisc_guard_store_p (mem, base))
	len += jrisc_store_guard (base, emit);
      if (emit)
	output_asm_insn (load ? "load\t(%1),%0" : "store\t%0,(%1)", ops);
      return len + 2;
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
      if (!load && jrisc_guard_store_p (mem, base))
	{
	  /* Each store needs its own completed load before it; the
	     second word is in the same kind of RAM as the first.  */
	  if (emit)
	    output_asm_insn ("load\t(%2),r28\n\tmove\tr28,r28\n\t"
			     "store\t%0,(%2)\n\tload\t(%2),r28\n\t"
			     "move\tr28,r28\n\tmove\t%2,r28\n\t"
			     "addqt\t#4,r28\n\tstore\t%1,(r28)", ops);
	  return 16;
	}
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
    return jrisc_word_access (load, reg, base, offset, emit, mem);

  /* Bytes and halfwords.  The DSP's do not need the store guard.  */
  gcc_assert (offset == 0);
  int len = 0;
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
  return len + 2;
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

/* Pipeline hazards.

   Atari's TOM and JERRY bug lists name three pipeline hazards that
   compiled code can meet:

   - TOM bug 2: an indexed store (STORE Rn,(R14+n), (R15+n), (R14+Rn) or
     (R15+Rn)) does not wait for its data register.  Issued while a
     divide into that register is in progress, it stores the old value,
     unless its offset field or index register names the register (the
     netlist compares that field as a register number).  Loads are
     interlocked: a store waits while a load from main RAM is pending.

   - TOM bug 13: an instruction that writes a register without reading
     it does not wait for a load from main RAM into the same register,
     whose late write then wins.  Loads from local RAM finish in time.
     A divide's late write is the same hazard.

   - TOM bug 25: consecutive divides fail.  A DIV is not issued while
     the previous divide's quotient is unread.

   This pass runs after the delay slots are filled and before branch
   shortening, and puts a jrisc_wait (MOVE Rn,Rn, which reads Rn and so
   waits for it) in front of each instruction that would meet one of
   them.  It follows what is pending through the function: at a label,
   the union of what every jump to it and the fall through leave.

   - Nothing is pending when a function is entered: a call waits for
     the divider and for loads into the call-clobbered registers, the
     arguments included, and a return waits for everything, the
     return value included: callers often ignore a value, and would
     then have to wait before writing its register.  Assembly code
     called from C (libgcc's lib1funcs.S) keeps the same rules.
   - A return's delay slot may not start a divide or a load from main
     RAM (jrisc_return_slot_ok_p).
   - A divide is over after JRISC_DIV_HAZARD_INSNS instructions; a load
     from main RAM is pending until its register is read or written.

   gas checks the same rules within straight-line code
   (gas/config/tc-jrisc.c), so assembly that breaks them, the compiler's
   included, is rejected.  */

/* Instructions after a DIV by which its quotient is certainly written
   (it takes 16 cycles; each instruction takes one at least).  The same
   number is in gas/config/tc-jrisc.c.  */
#define JRISC_DIV_HAZARD_INSNS 18

/* Registers as bit masks of r0-r31.  */
#define HZ_REG(n) (1u << (n))
#define HZ_CALL_CLOBBERED \
  (0x00003fffu | HZ_REG (JRISC_TEMP_REGNUM) | HZ_REG (JRISC_LR_REGNUM))

/* What may still be written at a point of the function.  */

struct jrisc_hz_state
{
  bool live;			/* Reached at all.  */
  unsigned int load;		/* By a load from main RAM.  */
  unsigned int div;		/* By the divider.  */
  int div_age;			/* Instructions issued since that DIV.  */
};

/* What an instruction does, as far as the hazards are concerned.  */

struct jrisc_hz_effect
{
  unsigned int reads;		/* Registers it reads (and so waits for).  */
  unsigned int writes;		/* Registers it writes.  */
  unsigned int idx_data;	/* Data of indexed stores, not interlocked.  */
  bool load;			/* Loads from main RAM into WRITES.  */
  bool div;			/* A DIV into WRITES.  */
  bool call, sibcall, ret, asm_p;
  int insns;			/* Instructions, at least.  */
};

static unsigned int
jrisc_hz_reg_mask (const_rtx reg)
{
  unsigned int mask = 0;
  unsigned int regno = REGNO (reg);
  for (unsigned int i = 0; i < REG_NREGS (reg); i++)
    if (regno + i < 32)
      mask |= HZ_REG (regno + i);
  return mask;
}

static unsigned int
jrisc_hz_regs_in (rtx x)
{
  unsigned int mask = 0;
  subrtx_iterator::array_type array;
  FOR_EACH_SUBRTX (iter, array, x, NONCONST)
    if (REG_P (*iter))
      mask |= jrisc_hz_reg_mask (*iter);
  return mask;
}

/* Is MEM certainly in local RAM?  */

static bool
jrisc_hz_local_mem_p (const_rtx mem)
{
  if (MEM_ADDR_SPACE (mem) == ADDR_SPACE_LOCAL)
    return true;
  rtx addr = XEXP (mem, 0);
  if (GET_CODE (addr) == PLUS)
    addr = XEXP (addr, 0);
  if (REG_P (addr)
      && (REGNO (addr) == JRISC_SP_REGNUM
	  || (REGNO (addr) == JRISC_FP_REGNUM && frame_pointer_needed)))
    return true;
  tree expr = MEM_EXPR (mem);
  while (expr && handled_component_p (expr))
    expr = TREE_OPERAND (expr, 0);
  return expr && VAR_P (expr) && jrisc_local_var_p (expr);
}

/* Does X read a MEM that may be in main RAM?  */

static bool
jrisc_hz_reads_dram_p (rtx x)
{
  subrtx_iterator::array_type array;
  FOR_EACH_SUBRTX (iter, array, x, NONCONST)
    if (MEM_P (*iter) && !jrisc_hz_local_mem_p (*iter))
      return true;
  return false;
}

/* Record in E the store or set X of an ordinary instruction.  SINGLE_P
   is true if the instruction is a single machine instruction.  */

static void
jrisc_hz_note_set (rtx x, bool single_p, jrisc_hz_effect *e)
{
  if (GET_CODE (x) == CLOBBER)
    {
      if (REG_P (XEXP (x, 0)))
	e->writes |= jrisc_hz_reg_mask (XEXP (x, 0));
      return;
    }
  rtx dest = SET_DEST (x);
  rtx src = SET_SRC (x);
  e->reads |= jrisc_hz_regs_in (src);
  if (jrisc_hz_reads_dram_p (src))
    e->load = true;

  while (GET_CODE (dest) == STRICT_LOW_PART || GET_CODE (dest) == ZERO_EXTRACT
	 || GET_CODE (dest) == SUBREG)
    {
      /* A part of a register: the rest is read.  */
      e->reads |= jrisc_hz_regs_in (dest);
      dest = XEXP (dest, 0);
    }
  if (REG_P (dest))
    e->writes |= jrisc_hz_reg_mask (dest);
  else if (MEM_P (dest))
    {
      rtx addr = XEXP (dest, 0);
      e->reads |= jrisc_hz_regs_in (addr);
      if (GET_CODE (addr) == PLUS
	  && REG_P (XEXP (addr, 0))
	  && (REGNO (XEXP (addr, 0)) == JRISC_FP_REGNUM
	      || REGNO (XEXP (addr, 0)) == JRISC_SP_REGNUM))
	{
	  /* An indexed store.  Its data is not interlocked, unless the
	     offset field (the index register, or the offset in words)
	     names the data register.  */
	  unsigned int data = jrisc_hz_regs_in (src);
	  rtx off = XEXP (addr, 1);
	  if (single_p && REG_P (src) && GET_MODE_SIZE (GET_MODE (dest)) == 4)
	    {
	      unsigned int field = 32;
	      if (REG_P (off))
		field = REGNO (off);
	      else if (CONST_INT_P (off)
		       && INTVAL (off) >= 4 && INTVAL (off) <= 128)
		field = (INTVAL (off) / 4) & 31;
	      if (field == REGNO (src))
		data = 0;
	    }
	  e->idx_data |= data;
	}
    }
}

static void
jrisc_hz_effect_of (rtx_insn *insn, jrisc_hz_effect *e)
{
  rtx pat = PATTERN (insn);

  memset (e, 0, sizeof *e);
  e->insns = 1;

  if (CALL_P (insn))
    {
      rtx call = get_call_rtx_from (insn);
      rtx addr = XEXP (XEXP (call, 0), 0);
      e->call = true;
      e->sibcall = SIBLING_CALL_P (insn);
      if (REG_P (addr))
	e->reads |= jrisc_hz_reg_mask (addr);
      e->writes |= HZ_REG (JRISC_TEMP_REGNUM);
      if (!e->sibcall)
	e->writes |= HZ_REG (JRISC_LR_REGNUM);
      e->insns = 3;
      return;
    }

  if (asm_noperands (pat) >= 0 || GET_CODE (pat) == ASM_INPUT)
    {
      e->asm_p = true;
      e->reads = e->writes = jrisc_hz_regs_in (pat);
      e->load = jrisc_hz_reads_dram_p (pat);
      return;
    }

  if (JUMP_P (insn) && returnjump_p (insn))
    {
      e->ret = true;
      e->reads = HZ_REG (JRISC_LR_REGNUM);
      return;
    }

  bool single_p = (recog_memoized (insn) >= 0
		   && get_attr_type (insn) != TYPE_MULTI
		   && get_attr_length (insn) <= 2);
  if (GET_CODE (pat) == PARALLEL)
    {
      for (int i = 0; i < XVECLEN (pat, 0); i++)
	{
	  rtx x = XVECEXP (pat, 0, i);
	  if (GET_CODE (x) == SET || GET_CODE (x) == CLOBBER)
	    jrisc_hz_note_set (x, single_p, e);
	  else if (GET_CODE (x) != USE)
	    e->reads |= jrisc_hz_regs_in (x);
	}
    }
  else if (GET_CODE (pat) == SET)
    jrisc_hz_note_set (pat, single_p, e);
  else if (GET_CODE (pat) != USE && GET_CODE (pat) != CLOBBER)
    e->reads |= jrisc_hz_regs_in (pat);

  if (recog_memoized (insn) >= 0)
    {
      enum attr_type type = get_attr_type (insn);
      e->div = type == TYPE_DIV;
      if (type != TYPE_BRANCH && type != TYPE_JUMP && type != TYPE_RETURN)
	e->insns = MAX (1, get_attr_length (insn) / 6);
    }
}

/* Return the registers to wait for before an instruction with effect E
   in state S.  */

static unsigned int
jrisc_hz_waits (const jrisc_hz_state &s, const jrisc_hz_effect &e)
{
  unsigned int pending = s.load | s.div;
  unsigned int wait = 0;

  /* TOM bug 2.  */
  wait |= e.idx_data & s.div;
  /* TOM bug 13, and a divide's late write.  */
  wait |= e.writes & ~e.reads & pending;
  /* TOM bug 25.  */
  if (e.div)
    wait |= s.div;
  if (e.call)
    wait |= s.div | (s.load & HZ_CALL_CLOBBERED);
  if (e.ret)
    wait |= s.div | s.load;
  if (e.asm_p)
    wait |= pending & (e.reads | e.writes);
  return wait;
}

static void
jrisc_hz_step (jrisc_hz_state &s, const jrisc_hz_effect &e,
	       unsigned int wait)
{
  s.load &= ~wait;
  s.div &= ~wait;

  s.load &= ~(e.reads | e.writes);
  s.div &= ~(e.reads | e.writes);
  s.div_age += e.insns;
  if (s.div_age >= JRISC_DIV_HAZARD_INSNS)
    s.div = 0;

  if (e.call && !e.sibcall)
    {
      s.load &= ~HZ_CALL_CLOBBERED;
      s.div = 0;
    }
  else if (e.load)
    s.load |= e.writes;
  if (e.div)
    {
      s.div = e.writes;
      s.div_age = 0;
    }
}

/* Merge FROM into TO; return true if TO changed.  */

static bool
jrisc_hz_merge (jrisc_hz_state &to, const jrisc_hz_state &from)
{
  if (!from.live)
    return false;
  if (!to.live)
    {
      to = from;
      return true;
    }
  jrisc_hz_state old = to;
  to.load |= from.load;
  to.div |= from.div;
  to.div_age = MIN (to.div_age, from.div_age);
  if (!to.div)
    to.div_age = JRISC_DIV_HAZARD_INSNS;
  return (to.load != old.load || to.div != old.div
	  || to.div_age != old.div_age);
}

/* Is LABEL reached from places this pass doesn't follow: a computed
   goto, a nonlocal goto or an exception handler?  */

static bool
jrisc_hz_unknown_label_p (rtx_insn *label)
{
  return LABEL_PRESERVE_P (label) || vec_safe_contains (forced_labels, label);
}

/* Walk the function from its entry, given the states the jumps leave at
   the labels in LAB_IN (indexed by label number - FIRST).  Merge what
   the jumps leave there into LAB_IN; if FIX_P, insert the waits.
   Return true if LAB_IN changed.  */

static bool
jrisc_hz_walk (vec<jrisc_hz_state> &lab_in, int first, bool fix_p)
{
  bool changed = false;
  jrisc_hz_state cur = { true, 0, 0, JRISC_DIV_HAZARD_INSNS };
  rtx_insn *next;

  for (rtx_insn *insn = get_insns (); insn; insn = next)
    {
      next = NEXT_INSN (insn);

      if (LABEL_P (insn))
	{
	  jrisc_hz_state &in = lab_in[CODE_LABEL_NUMBER (insn) - first];
	  if (jrisc_hz_unknown_label_p (insn))
	    {
	      jrisc_hz_state all = { true, 0xffffffffu, 0xffffffffu, 0 };
	      changed |= jrisc_hz_merge (in, all);
	    }
	  jrisc_hz_merge (cur, in);
	  continue;
	}
      if (BARRIER_P (insn))
	{
	  cur.live = false;
	  continue;
	}
      if (!NONDEBUG_INSN_P (insn)
	  || GET_CODE (PATTERN (insn)) == USE
	  || GET_CODE (PATTERN (insn)) == CLOBBER
	  || !cur.live)
	continue;

      rtx_insn *jump = insn;
      unsigned int wait = 0;
      if (rtx_sequence *seq = dyn_cast <rtx_sequence *> (PATTERN (insn)))
	{
	  /* A branch and its delay slot.  Every wait goes before the
	     branch: the branch itself leaves nothing pending.  */
	  jump = seq->insn (0);
	  for (int i = 0; i < seq->len (); i++)
	    {
	      jrisc_hz_effect e;
	      jrisc_hz_effect_of (seq->insn (i), &e);
	      unsigned int w = jrisc_hz_waits (cur, e);
	      jrisc_hz_step (cur, e, w);
	      wait |= w;
	    }
	}
      else
	{
	  jrisc_hz_effect e;
	  jrisc_hz_effect_of (insn, &e);
	  wait = jrisc_hz_waits (cur, e);
	  jrisc_hz_step (cur, e, wait);
	  /* A branch whose slot is empty: the NOP after it.  */
	  if (JUMP_P (insn))
	    {
	      jrisc_hz_effect nop;
	      memset (&nop, 0, sizeof nop);
	      nop.insns = 1;
	      jrisc_hz_step (cur, nop, 0);
	    }
	}

      if (fix_p)
	for (int r = 0; r < 32; r++)
	  if (wait & HZ_REG (r))
	    emit_insn_before (gen_jrisc_wait (gen_rtx_REG (SImode, r)),
			      insn);

      if (JUMP_P (jump))
	{
	  rtx_insn *label;
	  rtx_jump_table_data *table;
	  if (tablejump_p (jump, &label, &table))
	    {
	      rtvec vec = table->get_labels ();
	      for (int i = 0; i < GET_NUM_ELEM (vec); i++)
		{
		  rtx l = XEXP (RTVEC_ELT (vec, i), 0);
		  changed |= jrisc_hz_merge (lab_in[CODE_LABEL_NUMBER (l)
						    - first], cur);
		}
	    }
	  else if (JUMP_LABEL (jump) && LABEL_P (JUMP_LABEL (jump)))
	    changed |= jrisc_hz_merge (lab_in[CODE_LABEL_NUMBER
					      (JUMP_LABEL (jump)) - first],
				       cur);
	}
    }
  return changed;
}

static unsigned int
jrisc_hazards (void)
{
  int first = get_first_label_num ();
  int n = max_label_num () - first + 1;
  auto_vec<jrisc_hz_state> lab_in;
  jrisc_hz_state none = { false, 0, 0, JRISC_DIV_HAZARD_INSNS };

  lab_in.safe_grow (n);
  for (int i = 0; i < n; i++)
    lab_in[i] = none;
  while (jrisc_hz_walk (lab_in, first, false))
    ;
  jrisc_hz_walk (lab_in, first, true);
  return 0;
}

/* May INSN go in the delay slot of a return?  See above.  */

bool
jrisc_return_slot_ok_p (rtx_insn *insn)
{
  if (!jrisc_fix_hazards)
    return true;
  jrisc_hz_effect e;
  jrisc_hz_effect_of (insn, &e);
  return !e.div && !e.load;
}

namespace {

const pass_data pass_data_jrisc_hazards =
{
  RTL_PASS,			/* type */
  "jrisc_hazards",		/* name */
  OPTGROUP_NONE,		/* optinfo_flags */
  TV_MACH_DEP,			/* tv_id */
  0,				/* properties_required */
  0,				/* properties_provided */
  0,				/* properties_destroyed */
  0,				/* todo_flags_start */
  0,				/* todo_flags_finish */
};

class pass_jrisc_hazards : public rtl_opt_pass
{
public:
  pass_jrisc_hazards (gcc::context *ctxt)
    : rtl_opt_pass (pass_data_jrisc_hazards, ctxt)
  {}

  bool gate (function *) final override { return jrisc_fix_hazards; }
  unsigned int execute (function *) final override
  {
    return jrisc_hazards ();
  }
};

} // anon namespace

rtl_opt_pass *
make_pass_jrisc_hazards (gcc::context *ctxt)
{
  return new pass_jrisc_hazards (ctxt);
}

/* Target hooks.  */

#undef  TARGET_OPTION_OVERRIDE
#define TARGET_OPTION_OVERRIDE jrisc_option_override

/* Built-in functions for the instructions that C has no operator for.
   The GPU's: __builtin_jrisc_pack and _unpack (CRY pixels), _sat8,
   _sat16 and _sat24, _loadp and _storep; the DSP's: _sat16s, _mirror,
   _addqmod and _subqmod; both processors': _normi and _mtoi.  */

enum jrisc_builtin_type
{
  JRISC_BT_U_U,		/* unsigned (unsigned) */
  JRISC_BT_U_I,		/* unsigned (int) */
  JRISC_BT_I_I,		/* int (int) */
  JRISC_BT_I_U,		/* int (unsigned) */
  JRISC_BT_U_UI,	/* unsigned (unsigned, int): a constant 1-32 */
  JRISC_BT_ULL_CVP,	/* unsigned long long (const void *) */
  JRISC_BT_V_VP_ULL	/* void (void *, unsigned long long) */
};

enum jrisc_builtin_core
{
  JRISC_CORE_BOTH,
  JRISC_CORE_GPU,
  JRISC_CORE_DSP
};

struct jrisc_builtin_info
{
  const char *name;
  enum insn_code icode;
  enum jrisc_builtin_type type;
  enum jrisc_builtin_core core;
};

static const struct jrisc_builtin_info jrisc_builtin_info[] =
{
  { "__builtin_jrisc_pack", CODE_FOR_jrisc_pack, JRISC_BT_U_U,
    JRISC_CORE_GPU },
  { "__builtin_jrisc_unpack", CODE_FOR_jrisc_unpack, JRISC_BT_U_U,
    JRISC_CORE_GPU },
  { "__builtin_jrisc_sat8", CODE_FOR_jrisc_sat8, JRISC_BT_U_I,
    JRISC_CORE_GPU },
  { "__builtin_jrisc_sat16", CODE_FOR_jrisc_sat16, JRISC_BT_U_I,
    JRISC_CORE_GPU },
  { "__builtin_jrisc_sat24", CODE_FOR_jrisc_sat24, JRISC_BT_U_I,
    JRISC_CORE_GPU },
  { "__builtin_jrisc_loadp", CODE_FOR_jrisc_loadp, JRISC_BT_ULL_CVP,
    JRISC_CORE_GPU },
  { "__builtin_jrisc_storep", CODE_FOR_jrisc_storep, JRISC_BT_V_VP_ULL,
    JRISC_CORE_GPU },
  { "__builtin_jrisc_sat16s", CODE_FOR_jrisc_sat16s, JRISC_BT_I_I,
    JRISC_CORE_DSP },
  { "__builtin_jrisc_mirror", CODE_FOR_jrisc_mirror, JRISC_BT_U_U,
    JRISC_CORE_DSP },
  { "__builtin_jrisc_addqmod", CODE_FOR_jrisc_addqmod, JRISC_BT_U_UI,
    JRISC_CORE_DSP },
  { "__builtin_jrisc_subqmod", CODE_FOR_jrisc_subqmod, JRISC_BT_U_UI,
    JRISC_CORE_DSP },
  { "__builtin_jrisc_normi", CODE_FOR_jrisc_normi, JRISC_BT_I_U,
    JRISC_CORE_BOTH },
  { "__builtin_jrisc_mtoi", CODE_FOR_jrisc_mtoi, JRISC_BT_I_I,
    JRISC_CORE_BOTH }
};

#define JRISC_NUM_BUILTINS ARRAY_SIZE (jrisc_builtin_info)

static GTY(()) tree jrisc_builtin_decls[JRISC_NUM_BUILTINS];

/* Implement TARGET_INIT_BUILTINS.  All are declared for both processors,
   so that a call for the other one is an error that says so.  */

static void
jrisc_init_builtins (void)
{
  tree u = unsigned_type_node, i = integer_type_node;
  tree ull = long_long_unsigned_type_node;
  tree cvp = build_pointer_type (build_qualified_type (void_type_node,
						       TYPE_QUAL_CONST));
  tree types[] = {
    build_function_type_list (u, u, NULL_TREE),
    build_function_type_list (u, i, NULL_TREE),
    build_function_type_list (i, i, NULL_TREE),
    build_function_type_list (i, u, NULL_TREE),
    build_function_type_list (u, u, i, NULL_TREE),
    build_function_type_list (ull, cvp, NULL_TREE),
    build_function_type_list (void_type_node, ptr_type_node, ull, NULL_TREE)
  };

  for (unsigned int k = 0; k < JRISC_NUM_BUILTINS; k++)
    {
      const struct jrisc_builtin_info *b = &jrisc_builtin_info[k];
      tree decl = add_builtin_function (b->name, types[b->type], k,
					BUILT_IN_MD, NULL, NULL_TREE);
      /* All but STOREP and LOADP are pure functions of their
	 arguments.  */
      if (b->type != JRISC_BT_ULL_CVP && b->type != JRISC_BT_V_VP_ULL)
	TREE_READONLY (decl) = 1;
      TREE_NOTHROW (decl) = 1;
      jrisc_builtin_decls[k] = decl;
    }
}

/* Implement TARGET_BUILTIN_DECL.  */

static tree
jrisc_builtin_decl (unsigned int code, bool)
{
  return code < JRISC_NUM_BUILTINS ? jrisc_builtin_decls[code]
				   : error_mark_node;
}

/* Implement TARGET_EXPAND_BUILTIN.  */

static rtx
jrisc_expand_builtin (tree exp, rtx, rtx, machine_mode, int)
{
  tree fndecl = TREE_OPERAND (CALL_EXPR_FN (exp), 0);
  unsigned int code = DECL_MD_FUNCTION_CODE (fndecl);
  const struct jrisc_builtin_info *b = &jrisc_builtin_info[code];

  if ((b->core == JRISC_CORE_GPU && !TARGET_GPU)
      || (b->core == JRISC_CORE_DSP && TARGET_GPU))
    {
      error_at (EXPR_LOCATION (exp), "%qs is only available on the %s",
		b->name, b->core == JRISC_CORE_GPU ? "GPU" : "DSP");
      return const0_rtx;
    }

  rtx a0 = expand_normal (CALL_EXPR_ARG (exp, 0));
  switch (b->type)
    {
    case JRISC_BT_ULL_CVP:
      {
	rtx addr = force_reg (Pmode, convert_to_mode (Pmode, a0, 1));
	rtx t = gen_reg_rtx (DImode);
	emit_insn (GEN_FCN (b->icode) (t, addr));
	return t;
      }

    case JRISC_BT_V_VP_ULL:
      {
	rtx addr = force_reg (Pmode, convert_to_mode (Pmode, a0, 1));
	rtx v = force_reg (DImode,
			   convert_to_mode (DImode,
					    expand_normal (CALL_EXPR_ARG (exp,
									  1)),
					    1));
	emit_insn (GEN_FCN (b->icode) (addr, v));
	return const0_rtx;
      }

    case JRISC_BT_U_UI:
      {
	rtx n = expand_normal (CALL_EXPR_ARG (exp, 1));
	if (!CONST_INT_P (n) || INTVAL (n) < 1 || INTVAL (n) > 32)
	  {
	    error_at (EXPR_LOCATION (exp), "the second argument of %qs must "
		      "be a constant from 1 to 32", b->name);
	    return const0_rtx;
	  }
	rtx t = gen_reg_rtx (SImode);
	emit_move_insn (t, force_reg (SImode, convert_to_mode (SImode, a0,
							       1)));
	emit_insn (GEN_FCN (b->icode) (t, t, n));
	return t;
      }

    default:
      {
	rtx t = gen_reg_rtx (SImode);
	rtx x = force_reg (SImode, convert_to_mode (SImode, a0, 1));
	emit_insn (GEN_FCN (b->icode) (t, x));
	return t;
      }
    }
}

/* Output LOADP into, or STOREP from, the register pair OPERANDS[0] or
   OPERANDS[1] (LOAD says which), at the address in the other operand.
   The pair's first register has the phrase's first long word, through
   G_HIDATA (0xf02118, 15737112); r28 is the scratch register.  */

const char *
jrisc_output_phrase (rtx *operands, bool load)
{
  rtx pair = operands[load ? 0 : 1];
  rtx ops[3] = { gen_rtx_REG (SImode, REGNO (pair)),
		 gen_rtx_REG (SImode, REGNO (pair) + 1),
		 operands[load ? 1 : 0] };
  if (load)
    /* G_HIDATA is set as the load ends: the MOVE waits for LOADP's
       register.  */
    output_asm_insn ("loadp\t(%2),%1\n\tmove\t%1,%1\n\t"
		     "movei\t#15737112,r28\n\t"
		     "load\t(r28),%0", ops);
  else
    output_asm_insn ("movei\t#15737112,r28\n\t"
		     "store\t%0,(r28)\n\tstorep\t%1,(%2)", ops);
  return "";
}

/* The named address space __local (see ADDR_SPACE_LOCAL in jrisc.h).
   It is the processor's local RAM, part of the generic address space, so
   a __local pointer converts to an ordinary one implicitly, and an
   ordinary pointer to a __local one only with a cast: the programmer's
   word that it points to local RAM.  The pointers are the same.  */

static scalar_int_mode
jrisc_addr_space_pointer_mode (addr_space_t)
{
  return SImode;
}

static bool
jrisc_addr_space_subset_p (addr_space_t subset, addr_space_t superset)
{
  return subset == superset || superset == ADDR_SPACE_GENERIC;
}

static rtx
jrisc_addr_space_convert (rtx op, tree, tree)
{
  return op;
}

static bool
jrisc_addr_space_legitimate_address_p (machine_mode mode, rtx x, bool strict,
				       addr_space_t, code_helper ch)
{
  return jrisc_legitimate_address_p (mode, x, strict, ch);
}

static rtx
jrisc_addr_space_legitimize_address (rtx x, rtx oldx, machine_mode mode,
				     addr_space_t)
{
  return targetm.legitimize_address (x, oldx, mode);
}


#undef TARGET_ATTRIBUTE_TABLE
#define TARGET_ATTRIBUTE_TABLE jrisc_attribute_table
#undef TARGET_ASM_SELECT_SECTION
#define TARGET_ASM_SELECT_SECTION jrisc_select_section
#undef TARGET_ASM_UNIQUE_SECTION
#define TARGET_ASM_UNIQUE_SECTION jrisc_unique_section
#undef TARGET_SECTION_TYPE_FLAGS
#define TARGET_SECTION_TYPE_FLAGS jrisc_section_type_flags

#undef  TARGET_LEGITIMATE_ADDRESS_P
#define TARGET_LEGITIMATE_ADDRESS_P jrisc_legitimate_address_p
#undef  TARGET_LEGITIMATE_CONSTANT_P
#define TARGET_LEGITIMATE_CONSTANT_P jrisc_legitimate_constant_p
#undef  TARGET_NARROW_MEM_OK_P
#define TARGET_NARROW_MEM_OK_P jrisc_narrow_mem_ok_p
#undef  TARGET_CONDITIONAL_REGISTER_USAGE
#define TARGET_CONDITIONAL_REGISTER_USAGE jrisc_conditional_register_usage
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
#undef  TARGET_CALLEE_COPIES
#define TARGET_CALLEE_COPIES hook_bool_CUMULATIVE_ARGS_arg_info_true
#undef  TARGET_EXPAND_TO_RTL_HOOK
#define TARGET_EXPAND_TO_RTL_HOOK jrisc_expand_to_rtl_hook
#undef  TARGET_STACK_OBJECT_MEM
#define TARGET_STACK_OBJECT_MEM jrisc_stack_object_mem
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

#undef TARGET_INIT_BUILTINS
#define TARGET_INIT_BUILTINS jrisc_init_builtins
#undef TARGET_BUILTIN_DECL
#define TARGET_BUILTIN_DECL jrisc_builtin_decl
#undef TARGET_EXPAND_BUILTIN
#define TARGET_EXPAND_BUILTIN jrisc_expand_builtin

#undef TARGET_ADDR_SPACE_POINTER_MODE
#define TARGET_ADDR_SPACE_POINTER_MODE jrisc_addr_space_pointer_mode
#undef TARGET_ADDR_SPACE_ADDRESS_MODE
#define TARGET_ADDR_SPACE_ADDRESS_MODE jrisc_addr_space_pointer_mode
#undef TARGET_ADDR_SPACE_SUBSET_P
#define TARGET_ADDR_SPACE_SUBSET_P jrisc_addr_space_subset_p
#undef TARGET_ADDR_SPACE_CONVERT
#define TARGET_ADDR_SPACE_CONVERT jrisc_addr_space_convert
#undef TARGET_ADDR_SPACE_LEGITIMATE_ADDRESS_P
#define TARGET_ADDR_SPACE_LEGITIMATE_ADDRESS_P \
  jrisc_addr_space_legitimate_address_p
#undef TARGET_ADDR_SPACE_LEGITIMIZE_ADDRESS
#define TARGET_ADDR_SPACE_LEGITIMIZE_ADDRESS \
  jrisc_addr_space_legitimize_address

struct gcc_target targetm = TARGET_INITIALIZER;

#include "gt-jrisc.h"
