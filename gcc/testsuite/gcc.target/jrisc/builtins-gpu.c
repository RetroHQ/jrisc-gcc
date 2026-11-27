/* The GPU's built-in functions, against C versions.  */
/* { dg-do run } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target jrisc_gpu } */

extern void abort (void);

static unsigned
ref_pack (unsigned x)
{
  return ((x >> 10) & 0xf000) | ((x >> 5) & 0x0f00) | (x & 0xff);
}

static unsigned
ref_unpack (unsigned x)
{
  return ((x & 0xf000) << 10) | ((x & 0x0f00) << 5) | (x & 0xff);
}

static unsigned
ref_sat (int x, unsigned max)
{
  return x < 0 ? 0 : (unsigned) x > max ? max : (unsigned) x;
}

static int
ref_normi (unsigned x)
{
  int n = -32;
  if (x)
    for (n = -23; x >>= 1; n++)
      ;
  return n;
}

/* The mantissa of an IEEE single with its leading 1, negated if the
   sign is set.  */
static unsigned
ref_mtoi (unsigned x)
{
  unsigned m = (x & 0x007fffff) | 0x00800000;
  return x >> 31 ? -m : m;
}

volatile unsigned vals[] = { 0, 1, 0x7f, 0xff, 0x100, 0xffff, 0x10000,
			     0xffffff, 0x1000000, 0x7fffffff, 0x80000000,
			     0xffffffff, 0x12345678, 0xcafe, 0x3c00a5 };
unsigned long long phrase[2] __attribute__ ((aligned (8)));

int
main (void)
{
  for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++)
    {
      unsigned x = vals[i];
      if (__builtin_jrisc_pack (x) != ref_pack (x)
	  || __builtin_jrisc_unpack (x) != ref_unpack (x)
	  || __builtin_jrisc_pack (__builtin_jrisc_unpack (x & 0xffff))
	     != (x & 0xffff)
	  || __builtin_jrisc_sat8 ((int) x) != ref_sat ((int) x, 0xff)
	  || __builtin_jrisc_sat16 ((int) x) != ref_sat ((int) x, 0xffff)
	  || __builtin_jrisc_sat24 ((int) x) != ref_sat ((int) x, 0xffffff)
	  || __builtin_jrisc_normi (x) != ref_normi (x)
	  || (unsigned) __builtin_jrisc_mtoi ((int) x) != ref_mtoi (x))
	abort ();
    }

  /* The average of two CRY pixels, a field at a time, as the manual
     shows it.  */
  for (unsigned i = 0; i < sizeof vals / sizeof vals[0]; i++)
    for (unsigned j = 0; j < sizeof vals / sizeof vals[0]; j++)
      {
	unsigned a = vals[i] & 0xffff, b = (vals[j] * 0x9e37u) & 0xffff;
	unsigned avg = __builtin_jrisc_pack ((__builtin_jrisc_unpack (a)
					      + __builtin_jrisc_unpack (b))
					     >> 1);
	unsigned want = ((((a >> 12) + (b >> 12)) >> 1) << 12)
			| (((((a >> 8) & 15) + ((b >> 8) & 15)) >> 1) << 8)
			| (((a & 0xff) + (b & 0xff)) >> 1);
	if (avg != want)
	  abort ();
      }

  __builtin_jrisc_storep (&phrase[0], 0x0123456789abcdefull);
  if (phrase[0] != 0x0123456789abcdefull)
    abort ();
  phrase[1] = 0xfedcba9876543210ull;
  if (__builtin_jrisc_loadp (&phrase[1]) != 0xfedcba9876543210ull)
    abort ();
  return 0;
}
