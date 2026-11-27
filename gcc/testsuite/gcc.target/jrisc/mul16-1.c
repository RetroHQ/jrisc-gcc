/* A multiplication of values known to fit in 16 bits is one IMULT or
   MULT, even when the conversions from 16 bits are in a loop's PHIs,
   where GIMPLE no longer shows a widening multiplication.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

int
iterate (int x0, int y0, unsigned n)
{
  short x = 0, y = 0;
  unsigned i;
  for (i = 0; i < n; i++)
    {
      int x2 = (x * x) >> 12, y2 = (y * y) >> 12;
      if ((unsigned) (x2 + y2) > (4u << 12))
	break;
      y = 2 * ((x * y) >> 12) + y0;
      x = x2 - y2 + x0;
    }
  return i;
}

unsigned
uprod (unsigned short *a, unsigned short *b, int n)
{
  unsigned s = 0;
  unsigned pa = 0, pb = 0;
  for (int i = 0; i < n; i++)
    {
      s += pa * pb;
      pa = a[i];
      pb = b[i];
    }
  return s;
}

/* { dg-final { scan-assembler-times "\timult\t" 3 } } */
/* { dg-final { scan-assembler-times "\tmult\t" 1 } } */
