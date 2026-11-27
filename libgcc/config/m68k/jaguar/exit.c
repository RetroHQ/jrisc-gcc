/* exit and _exit for Atari Jaguar 68000 programs.
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

typedef void (*jag_func) (void);
extern jag_func __fini_array_start[] __attribute__ ((visibility ("hidden")));
extern jag_func __fini_array_end[] __attribute__ ((visibility ("hidden")));

void _exit (int) __attribute__ ((__noreturn__));
void exit (int) __attribute__ ((__noreturn__));

/* There is nothing to return to: halt the 68000 with interrupts off.
   The status is left in d0 for a debugger.  */

void
_exit (int status)
{
  for (;;)
    __asm__ volatile ("move.l %0, %%d0\n\tstop #0x2700" : : "g" (status)
		      : "d0");
}

void
exit (int status)
{
  for (jag_func *f = __fini_array_end; f > __fini_array_start; )
    (*--f) ();
  _exit (status);
}
