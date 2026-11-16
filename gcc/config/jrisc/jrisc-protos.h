/* Prototypes for the Atari Jaguar RISC back end.
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

#ifndef GCC_JRISC_PROTOS_H
#define GCC_JRISC_PROTOS_H

extern bool jrisc_regno_ok_for_base_p (int, bool);
extern HOST_WIDE_INT jrisc_initial_elimination_offset (int, int);
extern void jrisc_expand_prologue (void);
extern void jrisc_expand_epilogue (bool);
extern bool jrisc_can_use_return_insn (void);

#ifdef RTX_CODE
extern rtx jrisc_return_addr_rtx (int, rtx);
extern bool jrisc_expand_move (machine_mode, rtx *);
extern void jrisc_expand_mulsi3 (rtx, rtx, rtx);
extern void jrisc_expand_cbranch (rtx *);
extern void jrisc_split_cbranch (rtx *, rtx);
extern const char *jrisc_output_move (rtx *, machine_mode);
extern const char *jrisc_output_extend (rtx *, machine_mode, bool);
extern const char *jrisc_output_branch (rtx, rtx);
extern const char *jrisc_output_call (rtx *, int, bool);
extern int jrisc_insn_length (rtx_insn *);
extern bool jrisc_empty_delay_slot_p (rtx_insn *);
extern bool jrisc_low_mask_p (HOST_WIDE_INT);
#endif

#endif /* GCC_JRISC_PROTOS_H */
