/* Locals of every kind, aggregates passed by value, alloca, variable-sized
   arguments and __builtin_setjmp with the split frame.  */
/* { dg-do run } */
/* { dg-options "-O2" } */
/* { dg-require-effective-target alloca } */

struct pt { char tag; short x; int y; };
struct w { int a, b, c; };
struct big { int v[20]; };

__attribute__ ((noinline)) int
sum_bytes (const char *p, int n)
{
  int s = 0;
  while (n--)
    s += *p++;
  return s;
}

__attribute__ ((noinline)) int
by_val (struct pt p)
{
  p.x += 1;
  return p.tag + p.x + p.y;
}

__attribute__ ((noinline)) int
big_val (struct big b)
{
  b.v[3] = 100;
  return b.v[0] + b.v[3] + b.v[19];
}

__attribute__ ((noinline)) struct w
make_w (int k)
{
  struct w v = { k, k + 1, k + 2 };
  return v;
}

__attribute__ ((noinline)) int
bump (int *p)
{
  return ++*p;
}

__attribute__ ((noinline)) int
words (int k)
{
  struct w v = make_w (k);
  int arr[4];
  for (int i = 0; i < 4; i++)
    arr[i] = v.a * i;
  return arr[k & 3] + v.c;
}

__attribute__ ((noinline)) int
vla (int n)
{
  char buf[n];
  for (int i = 0; i < n; i++)
    buf[i] = i;
  char *b = __builtin_alloca (n);
  for (int i = 0; i < n; i++)
    b[i] = buf[i] * 2;
  return sum_bytes (b, n);
}

__attribute__ ((noinline)) void
jump (void **buf)
{
  __builtin_longjmp (buf, 1);
}

__attribute__ ((noinline)) _Complex char
cplx (int x)
{
  _Complex char r;
  __real__ r = x + 1;
  __imag__ r = x - 1;
  return r;
}

int
main (void)
{
  char buf[13];
  int x = 41;
  struct pt p = { 2, 30, 400 };
  struct big b;
  void *jb[5];
  int r = 0;

  for (int i = 0; i < 13; i++)
    buf[i] = i * 3;
  for (int i = 0; i < 20; i++)
    b.v[i] = i;
  if (sum_bytes (buf, 13) != 234)
    __builtin_abort ();
  if (bump (&x) != 42 || x != 42)
    __builtin_abort ();
  if (by_val (p) != 433 || p.x != 30)
    __builtin_abort ();
  if (big_val (b) != 119 || b.v[3] != 3)
    __builtin_abort ();
  if (words (5) != 12)
    __builtin_abort ();
  if (vla (10) != 90)
    __builtin_abort ();
  if (__real__ cplx (5) != 6 || __imag__ cplx (5) != 4)
    __builtin_abort ();
  if (__builtin_setjmp (jb) == 0)
    jump (jb);
  else
    r = 6;
  if (r != 6 || buf[12] != 36)
    __builtin_abort ();
  return 0;
}
