/* On the DSP, a store through a pointer to the __local address space
   needs no guard (dsp-store-*.c check that one through an ordinary
   pointer does).  __local variables are in local RAM.  */
/* { dg-do compile } */
/* { dg-options "-O2 -std=gnu99" } */
/* { dg-require-effective-target jrisc_dsp } */

__local unsigned buf[16];
static __local unsigned sbuf[4];
__local unsigned init[2] = { 1, 2 };

void
fill_local (__local unsigned *p, unsigned v, int n)
{
  for (int i = 0; i < n; i++)
    p[i] = v;
}

void
use (void)
{
  fill_local (buf, 1, 16);
  fill_local (sbuf, 2, 4);
}

/* { dg-final { scan-assembler-not "move\tr28,r28" } } */
/* { dg-final { scan-assembler-symbol-section {^buf$} {^\.lbss$} } } */
/* { dg-final { scan-assembler-symbol-section {^sbuf$} {^\.lbss$} } } */
/* { dg-final { scan-assembler-symbol-section {^init$} {^\.ldata$} } } */
