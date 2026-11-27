/* With a bigger -mlocal-frame-limit, the array stays in local RAM.  */
/* { dg-do compile } */
/* { dg-options "-O2 -mlocal-frame-limit=256" } */

extern int get (int);

int
big (int k)
{
  int a[32];
  for (int i = 0; i < 32; i++)
    a[i] = get (i);
  return a[k & 31];
}

/* { dg-final { scan-assembler-not "r27" } } */
