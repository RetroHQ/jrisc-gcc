/* A store to a local variable (in local RAM) is not guarded.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target jrisc_dsp } */

__attribute__ ((local)) int lv[4];

void
f (int i, int v)
{
  lv[i & 3] = v;
}

/* { dg-final { scan-assembler "store\t" } } */
/* { dg-final { scan-assembler-not "r28,r28" } } */
