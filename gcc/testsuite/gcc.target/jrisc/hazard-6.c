/* -mno-fix-hazards: no waits.  */
/* { dg-do compile } */
/* { dg-options "-O0 -mno-fix-hazards" } */

unsigned
quotient (unsigned a, unsigned b)
{
  unsigned z = b & 1, q;
  q = a / b;
  return q + z;
}

/* { dg-final { scan-assembler-not {move\t(r\d+),\1\n} } } */
