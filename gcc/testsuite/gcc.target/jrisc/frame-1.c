/* Byte and halfword locals, and locals whose address is taken, go in the
   frame in main RAM (r27), never on the stack in local RAM (r15).  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

extern void use (void *);
extern int get (void);

int
f (void)
{
  char buf[13];
  short h[3];
  int x = get ();

  use (buf);
  use (h);
  use (&x);
  return buf[1] + h[2] + x;
}

/* { dg-final { scan-assembler "subq\t#\[0-9\]+,r27|sub\tr28,r27" } } */
/* { dg-final { scan-assembler "store\tr27,\\(r15" } } */
/* { dg-final { scan-assembler "load\t\\(r15\[^)\]*\\),r27" } } */
/* { dg-final { scan-assembler-not "(load|store)\[bw\]\t\[^\n\]*\\(r1\[45\]" } } */
