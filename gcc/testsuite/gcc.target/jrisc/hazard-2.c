/* TOM bug 13: a register written while a load from main RAM into it is
   pending.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

extern volatile unsigned vol;

unsigned
discard (void)
{
  vol;
  return 0;
}

/* { dg-final { scan-assembler {load\t\((r\d+)\),\1\n\tmove\t\1,\1\n\tmoveq\t#0,\1} } } */
