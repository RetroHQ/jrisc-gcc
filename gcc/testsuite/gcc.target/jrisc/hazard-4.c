/* A call waits for a load from main RAM into an argument register, and
   a function waits for the load of its value before it returns.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

extern void use (unsigned);
extern unsigned get (void);
extern unsigned *p;

void
f (void)
{
  use (*p);
}

unsigned
g (unsigned *q)
{
  return *q;
}

/* f: the wait is before the (sibling) call.  */
/* { dg-final { scan-assembler {load\t\(r\d+\),r0\n\tmove\tr0,r0\n\tmovei\t#use,r28} } } */
/* g: the wait is before the return, not in its delay slot.  */
/* { dg-final { scan-assembler {load\t\(r0\),r0\n\tmove\tr0,r0\n\tjump\tt,\(r29\)} } } */
