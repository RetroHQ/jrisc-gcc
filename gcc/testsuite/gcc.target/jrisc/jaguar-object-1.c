/* -mjaguar-object leaves symbols the program does not define, such as
   the 68000 program's variables, to the 68000 link.  Without it, this
   link fails.  */
/* { dg-do link } */
/* { dg-options "-O2 -mjaguar-object" } */

extern volatile int frame_count;	/* Defined by the 68000 code.  */
int result;

int
main (void)
{
  result = frame_count * 3;
  return 0;
}
