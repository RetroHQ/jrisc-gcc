/* Multiplications of 16-bit values through PHIs give the right
   products, signed and unsigned (mul16-1.c checks the code).  */
/* { dg-do run } */
/* { dg-options "-O2" } */

extern void abort (void);

__attribute__ ((noipa)) int
sprod (const short *a, const short *b, int n)
{
  int s = 0, pa = 0, pb = 0;
  for (int i = 0; i < n; i++)
    {
      s += pa * pb;
      pa = a[i];
      pb = b[i];
    }
  return s + pa * pb;
}

__attribute__ ((noipa)) unsigned
uprod (const unsigned short *a, const unsigned short *b, int n)
{
  unsigned s = 0, pa = 0, pb = 0;
  for (int i = 0; i < n; i++)
    {
      s += pa * pb;
      pa = a[i];
      pb = b[i];
    }
  return s + pa * pb;
}

/* Not 16-bit: the full product.  */
__attribute__ ((noipa)) int
wide (const int *a, int n)
{
  int s = 0, p = 1;
  for (int i = 0; i < n; i++)
    {
      s += p * p;
      p = a[i];
    }
  return s + p * p;
}

short sa[] = { -32768, 32767, -1, 1, 12345, -23456 };
short sb[] = { -32768, -32768, -1, 32767, -3, 7 };
unsigned short ua[] = { 65535, 1, 40000, 2, 65535, 300 };
unsigned short ub[] = { 65535, 65535, 50000, 3, 2, 400 };
int wa[] = { 70000, -100000, 3 };

int
main (void)
{
  int s = 0;
  for (int i = 0; i < 6; i++)
    s += sa[i] * sb[i];
  if (sprod (sa, sb, 6) != s)
    abort ();
  unsigned u = 0;
  for (int i = 0; i < 6; i++)
    u += (unsigned) ua[i] * ub[i];
  if (uprod (ua, ub, 6) != u)
    abort ();
  if (wide (wa, 3) != (int) (1u + 70000u * 70000u + 100000u * 100000u + 9u))
    abort ();
  return 0;
}
