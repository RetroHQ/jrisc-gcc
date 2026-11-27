/* Like frame-7.c, with optimization: only the parameters whose address
   is taken need a home.  */
/* { dg-do compile } */
/* { dg-options "-O2" } */

extern void use_s (short *);
extern void use_c (char *);

void addr (short a) { use_s (&a); }
int addr2 (unsigned short a) { use_s ((short *) &a); return a; }

/* escape's a goes in main RAM: its address is passed to a function that
   may access it a byte at a time.  */
void escape (int a) { use_c ((char *) &a); }

/* { dg-final { scan-assembler-not {store[bw]\tr\d+,\(r1[45]\)} } } */
/* { dg-final { scan-assembler-times {move\tr27,r0} 3 } } */
