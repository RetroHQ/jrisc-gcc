/* Stores and loads through __local pointers.  */
/* { dg-do run } */
/* { dg-options "-O2 -std=gnu99" } */

extern void abort (void);

__local unsigned buf[8];
__local unsigned init[2] = { 0x12345678, 0x9abcdef0 };
unsigned dram[8];

__attribute__ ((noipa)) void
fill (__local unsigned *p, unsigned v, int n)
{
  for (int i = 0; i < n; i++)
    p[i] = v + i;
}

__attribute__ ((noipa)) unsigned
sum (const unsigned *p, int n)
{
  unsigned s = 0;
  for (int i = 0; i < n; i++)
    s += p[i];
  return s;
}

int
main (void)
{
  fill (buf, 100, 8);
  if (sum (buf, 8) != 8 * 100 + 28)
    abort ();
  fill ((__local unsigned *) &buf[2], 7, 2);
  if (buf[1] != 101 || buf[2] != 7 || buf[3] != 8 || buf[4] != 104)
    abort ();
  if (init[0] != 0x12345678 || init[1] != 0x9abcdef0)
    abort ();
  for (int i = 0; i < 8; i++)
    dram[i] = buf[i];
  if (sum (dram, 8) != sum (buf, 8))
    abort ();
  return 0;
}
