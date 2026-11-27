/* Definitions for the Atari Jaguar's 68000 (m68k-jaguar-elf).
   Copyright (C) 2026 Free Software Foundation, Inc.

   This file is part of GCC.

   GCC is free software; you can redistribute it and/or modify it
   under the terms of the GNU General Public License as published by
   the Free Software Foundation; either version 3, or (at your option)
   any later version.

   GCC is distributed in the hope that it will be useful, but WITHOUT
   ANY WARRANTY; without even the implied warranty of MERCHANTABILITY
   or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU General Public
   License for more details.

   You should have received a copy of the GNU General Public License
   along with GCC; see the file COPYING3.  If not see
   <http://www.gnu.org/licenses/>.  */

/* An m68k-elf compiler for the 68000 in the Atari Jaguar, which links
   programs with the GPU and DSP programs that jrisc-elf-ld writes as
   68000 objects (--jaguar-object), using the linker's Jaguar emulation.
   There is no C library: libjaguar has the startup code, a few string
   and memory functions, and the functions to start the GPU and DSP
   (see jaguar.h).  */

#undef TARGET_OS_CPP_BUILTINS
#define TARGET_OS_CPP_BUILTINS()		\
  do						\
    {						\
      builtin_define ("__embedded__");		\
      builtin_define ("__jaguar__");		\
    }						\
  while (0)

#undef LINK_SPEC
#define LINK_SPEC "-m m68kjaguar"

#undef STARTFILE_SPEC
#define STARTFILE_SPEC "crt0.o%s"

#undef ENDFILE_SPEC
#define ENDFILE_SPEC ""

#undef LIB_SPEC
#define LIB_SPEC "-ljaguar"
