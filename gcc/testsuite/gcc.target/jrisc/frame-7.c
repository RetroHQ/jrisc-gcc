/* Parameters that need a home in memory: chars and shorts (at -O0, or
   whose address is taken) and parameters whose address escapes go in
   the frame in main RAM, never in a narrow slot in local RAM, which can
   only be accessed a word at a time.  */
/* { dg-do compile } */
/* { dg-options "-O0" } */

extern void use_s (short *);
extern void use_c (char *);

int
f (short a, unsigned short b, signed char c, unsigned char d)
{
  return a + b + c + d;
}

/* g is on the stack.  */
int
many (int a, int b, int c, int d, int e, int f, short g, unsigned char h)
{
  return a + g + h;
}

void addr (short a) { use_s (&a); }
void escape (int a) { use_c ((char *) &a); }

/* { dg-final { scan-assembler-not {(store|load)[bw]\t[^\n]*\(r1[45]} } } */
/* { dg-final { scan-assembler-not {store[bw]\tr\d+,\(r1[45]} } } */
/* { dg-final { scan-assembler-not {load[bw]\t\(r1[45]} } } */
