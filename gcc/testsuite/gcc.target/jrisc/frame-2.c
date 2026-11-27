/* A word-only local no bigger than -mlocal-frame-limit stays on the stack
   in local RAM; a bigger one goes in main RAM.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

extern int get (int);

int
small (int k)
{
  int a[8];
  for (int i = 0; i < 8; i++)
    a[i] = get (i);
  return a[k & 7];
}

int
big (int k)
{
  int a[32];
  for (int i = 0; i < 32; i++)
    a[i] = get (i);
  return a[k & 31];
}

/* { dg-final { scan-assembler-times "subq\t#\[0-9\]+,r27|sub\tr28,r27" 1 } } */
