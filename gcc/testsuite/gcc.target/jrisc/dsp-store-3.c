/* No store guard on the GPU, which does not have the DSP's bug.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target jrisc_gpu } */

void
f (int *p, int v)
{
  *p = v;
}

/* { dg-final { scan-assembler-not "r28,r28" } } */
