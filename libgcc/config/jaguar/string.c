/* String and memory functions for Atari Jaguar programs: the 68000's
   (libjaguar) and the GPU's and DSP's (libgcc).
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

/* There is no C library, but GCC calls memcpy, memmove, memset and
   memcmp itself.  This file is compiled with
   -fno-tree-loop-distribute-patterns, so that the loops here are not
   turned back into calls to these functions.  The 68000 reads and
   writes words and longs only at even addresses, the GPU and DSP longs
   only at multiples of 4.

   On the GPU and DSP, these are for main RAM: local RAM can only be
   accessed a long at a time, so a byte access there is wrong.  */

#include <stddef.h>

typedef __UINT32_TYPE__ jag_long;

#ifdef __mc68000__
#define LONG_ALIGN_MASK 1
#else
#define LONG_ALIGN_MASK 3
#endif

#define ALIGNED(p) ((((unsigned long) (p)) & LONG_ALIGN_MASK) == 0)

/* Copy and store the long at S or the long V at D.  The DSP is on the
   main bus 16 bits wide, and its long stores there can write the wrong
   data (Atari's "Jaguar Hardware Bugs & Warnings", JDEV0002, GPU/DSP
   item 7): it copies and stores a long as two words.  */
#ifdef __JRISC_DSP__
typedef unsigned short jag_word;
#define COPY_LONG(d, s)							\
  (((jag_word *) (d))[0] = ((const jag_word *) (s))[0],			\
   ((jag_word *) (d))[1] = ((const jag_word *) (s))[1])
#define STORE_LONG(d, v)						\
  (((jag_word *) (d))[0] = (v) >> 16, ((jag_word *) (d))[1] = (v))
#else
#define COPY_LONG(d, s) (*(jag_long *) (d) = *(const jag_long *) (s))
#define STORE_LONG(d, v) (*(jag_long *) (d) = (v))
#endif

void *
memcpy (void *dest, const void *src, size_t n)
{
  unsigned char *d = dest;
  const unsigned char *s = src;

  if (ALIGNED (d) && ALIGNED (s))
    {
      for (; n >= 4; n -= 4, d += 4, s += 4)
	COPY_LONG (d, s);
    }
  while (n--)
    *d++ = *s++;
  return dest;
}

void *
memmove (void *dest, const void *src, size_t n)
{
  unsigned char *d = dest;
  const unsigned char *s = src;

  if (d <= s || d >= s + n)
    return memcpy (dest, src, n);
  d += n;
  s += n;
  while (n--)
    *--d = *--s;
  return dest;
}

void *
memset (void *dest, int c, size_t n)
{
  unsigned char *d = dest;

  if (ALIGNED (d) && n >= 4)
    {
      jag_long l = (unsigned char) c;

      l |= l << 8;
      l |= l << 16;
      for (; n >= 4; n -= 4, d += 4)
	STORE_LONG (d, l);
    }
  while (n--)
    *d++ = (unsigned char) c;
  return dest;
}

int
memcmp (const void *a, const void *b, size_t n)
{
  const unsigned char *p = a, *q = b;

  for (; n; n--, p++, q++)
    if (*p != *q)
      return *p - *q;
  return 0;
}

void *
memchr (const void *s, int c, size_t n)
{
  const unsigned char *p = s;

  for (; n; n--, p++)
    if (*p == (unsigned char) c)
      return (void *) p;
  return NULL;
}

size_t
strlen (const char *s)
{
  const char *p = s;

  while (*p)
    p++;
  return p - s;
}

char *
strcpy (char *dest, const char *src)
{
  char *d = dest;

  while ((*d++ = *src++) != 0)
    ;
  return dest;
}

char *
strncpy (char *dest, const char *src, size_t n)
{
  char *d = dest;

  for (; n && *src; n--)
    *d++ = *src++;
  for (; n; n--)
    *d++ = 0;
  return dest;
}

char *
strcat (char *dest, const char *src)
{
  strcpy (dest + strlen (dest), src);
  return dest;
}

int
strcmp (const char *a, const char *b)
{
  const unsigned char *p = (const unsigned char *) a;
  const unsigned char *q = (const unsigned char *) b;

  while (*p && *p == *q)
    p++, q++;
  return *p - *q;
}

int
strncmp (const char *a, const char *b, size_t n)
{
  const unsigned char *p = (const unsigned char *) a;
  const unsigned char *q = (const unsigned char *) b;

  for (; n; n--, p++, q++)
    if (*p != *q || !*p)
      return *p - *q;
  return 0;
}

char *
strchr (const char *s, int c)
{
  for (;; s++)
    {
      if (*s == (char) c)
	return (char *) s;
      if (!*s)
	return NULL;
    }
}
