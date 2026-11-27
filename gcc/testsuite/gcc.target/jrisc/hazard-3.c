/* TOM bug 25: a divide while the previous quotient is unread.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

typedef unsigned char v4qi __attribute__ ((vector_size (4)));

v4qi
vdiv (v4qi x, v4qi y)
{
  return x / y;
}

/* { dg-final { scan-assembler-times "\tdiv\t" 4 } } */
/* The first quotient is still unread at the second divide; the others
   are read (shifted into place) before the next one.  */
/* { dg-final { scan-assembler-times {move\t(r\d+),\1\n\tdiv\t} 1 } } */
