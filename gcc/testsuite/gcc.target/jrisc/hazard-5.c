/* Code full of divides, loads from main RAM and stores to the frame
   must assemble: gas rejects the hazards (-mpipeline-hazards=error).  */
/* { dg-do assemble } */
/* { dg-options "-O0" } */

typedef unsigned short u16;
typedef unsigned char v4qi __attribute__ ((vector_size (4)));
extern volatile unsigned vol;
extern unsigned use (unsigned, unsigned);

unsigned
mix (unsigned *a, unsigned n, unsigned d)
{
  unsigned s = 0, t[8];
  for (unsigned i = 0; i < n; i++)
    {
      t[i & 7] = a[i] / d;
      s += t[i & 7] % (d + 1);
      (void) vol;
      s = use (s / 3, a[i] / 5);
    }
  return s + t[n & 7] / (d + 2);
}

v4qi
vdiv (v4qi x, v4qi y)
{
  return x / y;
}

unsigned
pair (const u16 *a, u16 d)
{
  return (unsigned) (a[0] / d) << 16 | (unsigned) (a[1] / d);
}
