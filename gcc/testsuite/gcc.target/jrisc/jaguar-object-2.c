/* -mjaguar-object with -flto: the global variables are kept for the
   68000 program, and empty .data and .bss do not upset the link.  */
/* { dg-do link } */
/* { dg-require-effective-target lto } */
/* { dg-options "-O2 -flto -mjaguar-object" } */

extern volatile int frame_count;	/* Defined by the 68000 code.  */
int result;				/* Read by the 68000 code.  */

int
main (void)
{
  result = frame_count * 3;
  return 0;
}
