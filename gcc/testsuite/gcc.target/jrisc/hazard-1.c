/* TOM bug 2: at -O0 a quotient goes straight to its frame slot with an
   indexed store, which does not wait for the divide.  */
/* { dg-do compile } */
/* { dg-options "-O0" } */

unsigned
quotient (unsigned a, unsigned b)
{
  unsigned z = b & 1, q;
  q = a / b;
  return q + z;
}

/* { dg-final { scan-assembler {div\tr\d+,(r\d+)\n\tmove\t\1,\1\n\tstore\t\1,\(r14\+\d+\)} } } */
