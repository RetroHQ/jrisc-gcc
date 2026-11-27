/* A static "local" variable without an initializer is in .lbss, not in
   main RAM's .bss through .local/.comm.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

__attribute__ ((local)) static int buf[16];
static int dram[16];

int *
f (int i)
{
  return i ? buf : dram;
}

/* { dg-final { scan-assembler-symbol-section {^buf$} {^\.lbss$} } } */
/* { dg-final { scan-assembler-not "\\.comm\[ \t\]+buf" } } */
/* { dg-final { scan-assembler "\\.comm\[ \t\]+dram" } } */
