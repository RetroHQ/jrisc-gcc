/* Target definitions for the Atari Jaguar RISC processors
   (TOM GPU and JERRY DSP).
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

#ifndef GCC_JRISC_H
#define GCC_JRISC_H

/* Calling convention and register usage (kept in step with
   include/opcode/jrisc.h in binutils and GDB's jrisc-tdep.c):

     r0-r5	arguments; r0 (and r1) return values	call-clobbered
     r6-r13	temporaries				call-clobbered
     r14	frame pointer (when needed)		callee-saved
     r15	stack pointer, grows down		fixed
     r16-r26	callee-saved
     r27	stack pointer for main RAM (see below)	fixed
     r28	assembler/linker/compiler temporary	fixed
     r29	return address				call-clobbered
     r30-r31	reserved for interrupt handlers		fixed

   r14 and r15 are the only registers with base+offset addressing, which
   is why they hold the frame and stack pointers.

   r30 and r31 belong to interrupt handlers: an interrupt switches to
   register bank 0 and pushes the return address through r31, and
   handlers return through r30.  Compiled code never reads or writes
   them.

   r27 points to a second, downward-growing stack in main RAM.  The
   linker's overlay manager keeps its return records on it, so it must
   always be valid, with free space below it.  It is never allocated to
   values.

   TOM/JERRY local RAM can only be accessed as 32-bit words: LOADB and
   LOADW return the whole long and STOREB and STOREW overwrite all of it
   (with the zero-extended value in the simulator, with the whole
   register in JagMCP).  The r15 stack is in local RAM, as it is much
   faster than main RAM, so only data that is accessed a word at a time
   may live there:

   - locals whose type has no parts smaller than a word, whose address
     is not taken and which are no bigger than -mlocal-frame-limit;
   - spill slots and other stack slots, each in a word of its own, so
     that a byte or halfword store cannot overwrite anything else;
   - saved registers and arguments, which are whole words.

   Other locals go in a frame in main RAM that the prologue allocates
   from r27, and so do the homes of parameters that are not word-only
   (chars and shorts at -O0 or whose address is taken) or whose address
   escapes, and alloca.  Aggregates are passed by reference,
   and the callee copies them.  A function with a main RAM frame (or
   that calls alloca) saves r27 with the callee-saved registers and
   restores it on return.  */

/* Driver.  */

#undef  ASM_SPEC
#define ASM_SPEC "%{mcpu=dsp|mcpu=jerry:-mdsp;:-mgpu}"

#undef  LINK_SPEC
#define LINK_SPEC "%{mcpu=dsp|mcpu=jerry:-m elf32jrisc_dsp} %{mjaguar-object:--jaguar-object}"

#undef  STARTFILE_SPEC
#define STARTFILE_SPEC "crt0%O%s"

#undef  ENDFILE_SPEC
#define ENDFILE_SPEC ""

#undef  LIB_SPEC
#define LIB_SPEC ""

/* The named address space __local: the processor's local RAM.  Pointers
   to it are the same as others, but stores through them on the DSP need
   neither the guard nor splitting into halfwords (-mfix-dsp-stores), and
   a variable in it goes in local RAM, as with the local attribute.  */
#define ADDR_SPACE_LOCAL 1

#define REGISTER_TARGET_PRAGMAS() jrisc_register_pragmas ()

#define TARGET_CPU_CPP_BUILTINS()				\
  do								\
    {								\
      builtin_define ("__jrisc__");				\
      builtin_define ("__JRISC__");				\
      builtin_define_std ("jrisc");				\
      builtin_assert ("cpu=jrisc");				\
      builtin_assert ("machine=jrisc");			\
      if (TARGET_DSP)						\
	{							\
	  builtin_define ("__JRISC_DSP__");			\
	  builtin_define ("__JERRY__");				\
	}							\
      else							\
	{							\
	  builtin_define ("__JRISC_GPU__");			\
	  builtin_define ("__TOM__");				\
	}							\
    }								\
  while (0)

#define TARGET_DSP (jrisc_cpu == JRISC_CPU_DSP)
#define TARGET_GPU (jrisc_cpu == JRISC_CPU_GPU)

/* Storage layout.  */

#define BITS_BIG_ENDIAN 0
#define BYTES_BIG_ENDIAN 1
#define WORDS_BIG_ENDIAN 1

#define UNITS_PER_WORD 4
#define POINTER_SIZE 32
#define Pmode SImode
#define FUNCTION_MODE QImode

#define PARM_BOUNDARY 32
#define STACK_BOUNDARY 32
#define FUNCTION_BOUNDARY 16
#define BIGGEST_ALIGNMENT 32
#define EMPTY_FIELD_BOUNDARY 32
#define STRICT_ALIGNMENT 1
#define PCC_BITFIELD_TYPE_MATTERS 1

/* Every stack slot and local in the r15 frame starts a word of its own
   (see the comment at the top).  */
#define STACK_SLOT_ALIGNMENT(TYPE, MODE, ALIGN) \
  ((ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))
#define LOCAL_ALIGNMENT(TYPE, ALIGN) \
  ((ALIGN) < BITS_PER_WORD ? BITS_PER_WORD : (ALIGN))

/* __builtin_setjmp and nonlocal gotos save both stack pointers.  */
#define STACK_SAVEAREA_MODE(LEVEL) \
  ((LEVEL) == SAVE_NONLOCAL ? DImode : Pmode)

/* Registers hold whole words; narrow values are kept extended.  */
#define WORD_REGISTER_OPERATIONS 1
/* LOADB and LOADW zero-extend from main memory.  (From local RAM they
   return the whole long, which is the zero-extended value only for a
   slot that is a word of its own.)  */
#define LOAD_EXTEND_OP(MODE) \
  ((MODE) == QImode || (MODE) == HImode ? ZERO_EXTEND : UNKNOWN)
#define PROMOTE_MODE(MODE, UNSIGNEDP, TYPE)			\
  do								\
    {								\
      if (GET_MODE_CLASS (MODE) == MODE_INT			\
	  && GET_MODE_SIZE (MODE) < UNITS_PER_WORD)		\
	(MODE) = SImode;					\
    }								\
  while (0)

#define INT_TYPE_SIZE 32
#define SHORT_TYPE_SIZE 16
#define LONG_TYPE_SIZE 32
#define LONG_LONG_TYPE_SIZE 64
#define DEFAULT_SIGNED_CHAR 1

#undef  SIZE_TYPE
#define SIZE_TYPE "unsigned int"
#undef  PTRDIFF_TYPE
#define PTRDIFF_TYPE "int"
#undef  WCHAR_TYPE
#define WCHAR_TYPE "int"
#undef  WCHAR_TYPE_SIZE
#define WCHAR_TYPE_SIZE 32

/* Registers.  */

#define JRISC_FP_REGNUM		14
#define JRISC_SP_REGNUM		15
#define JRISC_TEMP_REGNUM	28	/* Scratch for long jumps and calls.  */
#define JRISC_LR_REGNUM		29
#define JRISC_DRAM_SP_REGNUM	27	/* Main RAM stack pointer.  */

/* Tom's and Jerry's spaces (their registers and local RAMs) start here;
   main RAM and the ROMs are below.  */
#define JRISC_LOCAL_SPACE	0xf00000
#define JRISC_SFP_REGNUM	32	/* Soft frame pointer, eliminated.  */
#define JRISC_AP_REGNUM		33	/* Argument pointer, eliminated.  */
#define JRISC_CC_REGNUM		34	/* Z, C and N flags.  */
#define FIRST_PSEUDO_REGISTER	35

#define JRISC_FIRST_ARG_REGNUM	0
#define JRISC_NUM_ARG_REGS	6

#define FIXED_REGISTERS						\
  { 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1,		\
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 0, 1, 1,		\
    1, 1, 1 }

#define CALL_USED_REGISTERS					\
  { 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0, 1,		\
    0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 0, 1, 1, 1, 1, 1,		\
    1, 1, 1 }

#define REGISTER_NAMES						\
  { "r0",  "r1",  "r2",  "r3",  "r4",  "r5",  "r6",  "r7",	\
    "r8",  "r9",  "r10", "r11", "r12", "r13", "r14", "r15",	\
    "r16", "r17", "r18", "r19", "r20", "r21", "r22", "r23",	\
    "r24", "r25", "r26", "r27", "r28", "r29", "r30", "r31",	\
    "?fp", "?ap", "?cc" }

#define ADDITIONAL_REGISTER_NAMES				\
  { { "sp", JRISC_SP_REGNUM }, { "fp", JRISC_FP_REGNUM },	\
    { "lr", JRISC_LR_REGNUM } }

/* Allocate the temporaries first, then the callee-saved registers, and
   r14 last since it may be needed as the frame pointer.  */
#define REG_ALLOC_ORDER						\
  { 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13,		\
    16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26,		\
    29, 14, 15, 27, 28, 30, 31, 32, 33, 34 }

enum reg_class
{
  NO_REGS,
  CALL_ADDR_REGS,	/* Registers that can hold an indirect call target.  */
  GENERAL_REGS,
  CC_REGS,
  ALL_REGS,
  LIM_REG_CLASSES
};

#define N_REG_CLASSES ((int) LIM_REG_CLASSES)

#define REG_CLASS_NAMES \
  { "NO_REGS", "CALL_ADDR_REGS", "GENERAL_REGS", "CC_REGS", "ALL_REGS" }

/* CALL_ADDR_REGS excludes the fixed registers r15, r27, r28, r30 and
   r31, and r29, which the call sequence overwrites before it jumps.  */
#define REG_CLASS_CONTENTS			\
  { { 0x00000000, 0x0 },			\
    { 0x07ff7fff, 0x0 },			\
    { 0xffffffff, 0x3 },			\
    { 0x00000000, 0x4 },			\
    { 0xffffffff, 0x7 } }

#define REGNO_REG_CLASS(R)					\
  ((R) == JRISC_CC_REGNUM ? CC_REGS				\
   : ((R) == JRISC_SP_REGNUM || (R) == JRISC_TEMP_REGNUM		\
      || (R) == JRISC_LR_REGNUM || (R) == JRISC_DRAM_SP_REGNUM	\
      || (R) >= 30) ? GENERAL_REGS					\
   : CALL_ADDR_REGS)

#define BASE_REG_CLASS GENERAL_REGS
#define INDEX_REG_CLASS NO_REGS

#define REGNO_OK_FOR_BASE_P(NUM) jrisc_regno_ok_for_base_p (NUM, true)
#define REGNO_OK_FOR_INDEX_P(NUM) 0

#define AVOID_CCMODE_COPIES 1

/* Frame layout.  From the stack pointer upwards:

     outgoing arguments
     local variables		<- soft frame pointer
     saved registers
     argument register save area	<- argument pointer
				<- stack pointer on entry
     incoming stack arguments

   The argument register save area holds the anonymous argument
   registers of a varargs function, or the register part of an argument
   that is split between registers and the stack
   (crtl->args.pretend_args_size bytes).  It makes the register and
   stack arguments one contiguous block starting at the argument
   pointer.

   When a frame pointer is needed r14 holds the value of the stack
   pointer after the prologue, so locals stay at positive offsets.  */

#define STACK_GROWS_DOWNWARD 1
#define FRAME_GROWS_DOWNWARD 0
#define FIRST_PARM_OFFSET(FNDECL) 0
#define ACCUMULATE_OUTGOING_ARGS 1

#define STACK_POINTER_REGNUM JRISC_SP_REGNUM
#define HARD_FRAME_POINTER_REGNUM JRISC_FP_REGNUM
#define FRAME_POINTER_REGNUM JRISC_SFP_REGNUM
#define ARG_POINTER_REGNUM JRISC_AP_REGNUM

#define ELIMINABLE_REGS						\
  { { ARG_POINTER_REGNUM, STACK_POINTER_REGNUM },		\
    { ARG_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM },		\
    { FRAME_POINTER_REGNUM, STACK_POINTER_REGNUM },		\
    { FRAME_POINTER_REGNUM, HARD_FRAME_POINTER_REGNUM } }

#define INITIAL_ELIMINATION_OFFSET(FROM, TO, OFFSET) \
  ((OFFSET) = jrisc_initial_elimination_offset ((FROM), (TO)))

#define RETURN_ADDR_RTX(COUNT, FRAME) jrisc_return_addr_rtx ((COUNT), (FRAME))
#define INCOMING_RETURN_ADDR_RTX gen_rtx_REG (Pmode, JRISC_LR_REGNUM)
#define DWARF_FRAME_RETURN_COLUMN JRISC_LR_REGNUM
#define INCOMING_FRAME_SP_OFFSET 0
#define EPILOGUE_USES(REGNO) ((REGNO) == JRISC_LR_REGNUM)

/* DWARF register numbers are the hardware numbers of the active bank.  */
#define DEBUGGER_REGNO(REGNO) ((REGNO) < 32 ? (REGNO) : INVALID_REGNUM)

/* Function arguments.  */

#define CUMULATIVE_ARGS int
#define INIT_CUMULATIVE_ARGS(CUM, FNTYPE, LIBNAME, FNDECL, N_NAMED_ARGS) \
  ((CUM) = 0)
#define FUNCTION_ARG_REGNO_P(R) \
  ((unsigned) (R) < JRISC_FIRST_ARG_REGNUM + JRISC_NUM_ARG_REGS)

#define DEFAULT_PCC_STRUCT_RETURN 0

/* Static chain for nested functions: a call-clobbered non-argument
   register.  */
#define STATIC_CHAIN_REGNUM 13

/* There is no trampoline support.  */
#define TRAMPOLINE_SIZE 0
#define TRAMPOLINE_ALIGNMENT 32

#define FUNCTION_PROFILER(FILE, LABELNO) \
  sorry ("profiling is not supported on this target")
#define NO_PROFILE_COUNTERS 1

/* Addressing.  */

#define MAX_REGS_PER_ADDRESS 1
#define HAVE_PRE_DECREMENT 0
#define HAVE_POST_INCREMENT 0

/* Miscellaneous.  */

#define CASE_VECTOR_MODE SImode
#define MOVE_MAX 4
#define SLOW_BYTE_ACCESS 1
#define SHIFT_COUNT_TRUNCATED 0
#define STORE_FLAG_VALUE 1
#define BRANCH_COST(SPEED_P, PREDICTABLE_P) 2
#define NO_FUNCTION_CSE 1

/* Assembler output.  */

#define ASM_COMMENT_START ";"
#define ASM_APP_ON "#APP\n"
#define ASM_APP_OFF "#NO_APP\n"

#define TEXT_SECTION_ASM_OP "\t.text"
#define DATA_SECTION_ASM_OP "\t.data"
#undef  BSS_SECTION_ASM_OP
#define BSS_SECTION_ASM_OP "\t.section\t.bss"

#define GLOBAL_ASM_OP "\t.globl\t"

/* A static, zero-initialised "local" variable goes in .lbss, not in the
   main RAM .bss that .local/.comm would give it.  */
#define ASM_OUTPUT_ALIGNED_DECL_LOCAL(STREAM, DECL, NAME, SIZE, ALIGN)	\
  jrisc_output_aligned_decl_local (STREAM, DECL, NAME, SIZE, ALIGN)

#undef  ASM_OUTPUT_ALIGN
#define ASM_OUTPUT_ALIGN(STREAM, POWER) \
  fprintf ((STREAM), "\t.p2align\t%d\n", (POWER))

#define ASM_OUTPUT_ADDR_VEC_ELT(STREAM, VALUE) \
  fprintf ((STREAM), "\t.long\t.L%d\n", (VALUE))

#define ASM_OUTPUT_ADDR_DIFF_ELT(STREAM, BODY, VALUE, REL) \
  fprintf ((STREAM), "\t.long\t.L%d-.L%d\n", (VALUE), (REL))


#endif /* GCC_JRISC_H */
