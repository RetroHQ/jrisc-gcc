/* On the DSP, stores through pointers that can only point to main RAM
   (by points-to information, through induction variables, and to the
   function's result in memory, whose slot is in main RAM) are made as
   word stores; so are block copies to main RAM.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target jrisc_dsp } */

struct big { unsigned a[40]; };
struct big gb;
extern void use (struct big *);

struct big
fill (unsigned h)
{
  struct big b;
  for (int i = 0; i < 40; i++)
    b.a[i] = h + i;
  return b;
}

void
copy (unsigned h)
{
  struct big b1, b2;
  b1 = gb;
  b2 = b1;
  b2.a[h & 31] ^= 1;
  use (&b2);
}

/* { dg-final { scan-assembler "\tstorew\t" } } */
/* { dg-final { scan-assembler-not "r28,r28" } } */
