/* The DSP's built-in functions, against C versions.  */
/* { dg-do run } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target jrisc_dsp } */

extern void abort (void);

#define D_MOD (*(volatile unsigned *) 0xf1a118)

static unsigned
ref_mirror (unsigned x)
{
  unsigned r = 0;
  for (int i = 0; i < 32; i++, x >>= 1)
    r = (r << 1) | (x & 1);
  return r;
}

volatile unsigned vals[] = { 0, 1, 0x7fff, 0x8000, 0xffff8000, 0xffff7fff,
			     0x7fffffff, 0x80000000, 0xffffffff, 0x12345678 };

int
main (void)
{
  for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++)
    {
      unsigned x = vals[i];
      int s = (int) x;
      int sat = s < -32768 ? -32768 : s > 32767 ? 32767 : s;
      if (__builtin_jrisc_sat16s (s) != sat
	  || __builtin_jrisc_mirror (x) != ref_mirror (x))
	abort ();
    }

  /* A circular buffer of 256 bytes: the bits above bit 7 are kept.  */
  D_MOD = ~0xffu;
  if (__builtin_jrisc_addqmod (0x1000fc, 8) != 0x100004
      || __builtin_jrisc_subqmod (0x100004, 8) != 0x1000fc
      || __builtin_jrisc_addqmod (0x100010, 32) != 0x100030)
    abort ();
  return 0;
}
