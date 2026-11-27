/* On the DSP, a long word store that may be to main RAM, through a
   pointer, follows a load of the same location, waited for (the
   JDEV0002 DSP store bug); word and byte stores do not need it.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target jrisc_dsp } */

void
f (int *p, int v, short *h, char *c)
{
  *p = v;
  *h = v;
  *c = v;
}

/* { dg-final { scan-assembler "load\t\\(r0\\),r28\n\tmove\tr28,r28\n\tstore\tr1,\\(r0\\)" } } */
/* { dg-final { scan-assembler "\tstorew\tr1,\\(r2\\)" } } */
/* { dg-final { scan-assembler "\tstoreb\tr1,\\(r3\\)" } } */
/* { dg-final { scan-assembler-times "r28,r28" 1 } } */
