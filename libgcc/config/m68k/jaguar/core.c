/* Starting the Atari Jaguar's GPU and DSP from the 68000.
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

#include "jaguar.h"

void
jag_core_stop (unsigned long base)
{
  JAG_REG (base, JAG_CTRL) = 0;
}

int
jag_core_running (unsigned long base)
{
  return (JAG_REG (base, JAG_CTRL) & JAG_CTRL_GO) != 0;
}

void
jag_core_wait (unsigned long base)
{
  while (jag_core_running (base))
    ;
}

/* Local RAM can only be written a long at a time.  The linker puts the
   image at long-aligned addresses; if it ends half way through a long,
   the whole long is copied.  */

void
jag_core_start (unsigned long base, const void *start, const void *end,
		void *dest, const void *entry)
{
  const volatile unsigned long *from = start;
  volatile unsigned long *to = dest;

  jag_core_stop (base);
  while (from < (const volatile unsigned long *) end)
    *to++ = *from++;
  JAG_REG (base, JAG_PC) = (unsigned long) entry;
  JAG_REG (base, JAG_CTRL) = JAG_CTRL_GO;
}
