/* Narrow and escaping parameters, accessed through pointers a byte or a
   halfword at a time, at every optimization level.  In local RAM a byte
   or halfword store would overwrite the whole word.  */
/* { dg-do run } */

__attribute__ ((noinline)) void
poke_byte (char *p, int i, char v)
{
  p[i] = v;
}

__attribute__ ((noinline)) void
poke_half (short *p, int i, short v)
{
  p[i] = v;
}

__attribute__ ((noinline)) int
narrow (short a, unsigned short b, signed char c, unsigned char d)
{
  poke_byte ((char *) &a, 1, 0x34);
  poke_byte ((char *) &b, 0, 0x56);
  poke_byte ((char *) &c, 0, -2);
  poke_byte ((char *) &d, 0, -2);	/* 0xfe */
  return a + b + c + d;
}

__attribute__ ((noinline)) int
word (int x)
{
  poke_byte ((char *) &x, 3, 0x78);	/* big-endian: the low byte */
  poke_half ((short *) &x, 0, 0x1234);
  return x;
}

__attribute__ ((noinline)) int
stacked (int a, int b, int c, int d, int e, int f, short g, unsigned char h)
{
  poke_byte ((char *) &g, 1, 0x11);
  poke_byte ((char *) &h, 0, 0x22);
  return a + b + c + d + e + f + g + h;
}

int
main (void)
{
  /* a = 0x1234 (0x12 kept), b = 0x5600 + 0x78, c = -2, d = 0xfe.  */
  if (narrow (0x12ff, 0x9978, 5, 7) != 0x1234 + 0x5678 - 2 + 0xfe)
    __builtin_abort ();
  if (word (0x0a0b0c0d) != 0x12340c78)
    __builtin_abort ();
  if (stacked (1, 2, 3, 4, 5, 6, 0x7700, 9) != 21 + 0x7711 + 0x22)
    __builtin_abort ();
  return 0;
}
