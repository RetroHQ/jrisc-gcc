/* No store guard with -mno-fix-dsp-stores.  */
/* { dg-do compile } */
/* { dg-options "-O2 -mno-fix-dsp-stores" } */

void
f (int *p, int v)
{
  *p = v;
}

/* { dg-final { scan-assembler-not "r28,r28" } } */
