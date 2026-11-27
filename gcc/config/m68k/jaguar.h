/* Atari Jaguar support for 68000 programs (m68k-jaguar-elf).
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

#ifndef _JAGUAR_H
#define _JAGUAR_H

#include <stddef.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The GPU's and DSP's control registers: add JAG_FLAGS, JAG_PC or
   JAG_CTRL to the processor's base.  */
#define JAG_GPU_BASE	0xF02100UL
#define JAG_DSP_BASE	0xF1A100UL
#define JAG_FLAGS	0x00
#define JAG_PC		0x10
#define JAG_CTRL	0x14
#define JAG_CTRL_GO	1UL
#define JAG_REG(base, reg) (*(volatile unsigned long *) ((base) + (reg)))

/* Stop the processor whose registers are at BASE, copy its program's
   image from START (up to END) to DEST in its local RAM, a long at a
   time, and start it at ENTRY.  */
extern void jag_core_start (unsigned long base, const void *start,
			    const void *end, void *dest, const void *entry);

/* Stop the processor; tell whether it is running; wait until it stops
   (the program stops itself by clearing GO in its control register).  */
extern void jag_core_stop (unsigned long base);
extern int jag_core_running (unsigned long base);
extern void jag_core_wait (unsigned long base);

/* Start the GPU or DSP program linked into this program: one that
   jrisc-elf-ld wrote with --jaguar-object, whose symbols have the
   prefix PREFIX (gpu_ or dsp_ unless --jaguar-prefix chose another).
   This refers to the symbols that the linker defines for it, so use
   it only in a program that has it.  */
#define JAG_CORE_START(base, prefix)					\
  do									\
    {									\
      extern char __##prefix##image_start[], __##prefix##image_end[];	\
      extern char __##prefix##image_dest[], __##prefix##entry[];	\
      jag_core_start ((base), __##prefix##image_start,			\
		      __##prefix##image_end, __##prefix##image_dest,	\
		      __##prefix##entry);				\
    }									\
  while (0)

#define jag_gpu_start() JAG_CORE_START (JAG_GPU_BASE, gpu_)
#define jag_dsp_start() JAG_CORE_START (JAG_DSP_BASE, dsp_)

/* Run the functions registered for the end of the program (fini_array)
   and halt the 68000.  main returning does the same.  */
extern void exit (int) __attribute__ ((__noreturn__));
extern void _exit (int) __attribute__ ((__noreturn__));

/* The string and memory functions that libjaguar has (there is no C
   library).  */
extern void *memcpy (void *, const void *, size_t);
extern void *memmove (void *, const void *, size_t);
extern void *memset (void *, int, size_t);
extern int memcmp (const void *, const void *, size_t);
extern void *memchr (const void *, int, size_t);
extern size_t strlen (const char *);
extern char *strcpy (char *, const char *);
extern char *strncpy (char *, const char *, size_t);
extern char *strcat (char *, const char *);
extern int strcmp (const char *, const char *);
extern int strncmp (const char *, const char *, size_t);
extern char *strchr (const char *, int);

#ifdef __cplusplus
}
#endif

#endif /* _JAGUAR_H */
