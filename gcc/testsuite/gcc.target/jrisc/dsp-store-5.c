/* On the DSP, long word stores known to be to main RAM are made as two
   word stores; a volatile one stays a single (guarded) store, and one
   to a hardware register is left alone.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target jrisc_dsp } */

int g;
long long gl;
volatile int vg;

void
f (int v, long long w)
{
  g = v;
  gl = w;
  vg = v;
  *(volatile int *) 0xf1a100 = v;
}

/* g twice, gl four times.  */
/* { dg-final { scan-assembler-times "\tstorew\t" 6 } } */
/* vg and the register.  */
/* { dg-final { scan-assembler-times "\tstore\t" 2 } } */
/* { dg-final { scan-assembler-times "r28,r28" 1 } } */
