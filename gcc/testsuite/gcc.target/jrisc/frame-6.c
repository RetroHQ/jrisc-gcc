/* Like frame-5.c, without optimization.  */
/* { dg-do compile } */
/* { dg-options "-O0" } */

extern int get (int);
extern void use (char *, int, int, int, int, int, int, int, int, int,
		 int, int, int, int, int, int);

int
pressure (int n)
{
  int a = get (1), b = get (2), c = get (3), d = get (4), e = get (5);
  int f = get (6), g = get (7), h = get (8), i = get (9), j = get (10);
  int k = get (11), l = get (12), m = get (13), o = get (14), p = get (15);
  char *buf = __builtin_alloca (n);
  use (buf, a, b, c, d, e, f, g, h, i, j, k, l, m, o, p);
  return a + b + c + d + e + f + g + h + i + j + k + l + m + o + p;
}

/* { dg-final { scan-assembler-not "r3\[01\]" } } */
