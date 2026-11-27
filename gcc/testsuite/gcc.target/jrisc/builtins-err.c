/* A built-in function of the other processor is an error.  */
/* { dg-do compile } */

unsigned
f (unsigned x)
{
#ifdef __JRISC_GPU__
  return __builtin_jrisc_mirror (x);	/* { dg-error "only available on the DSP" "" { target jrisc_gpu } } */
#else
  return __builtin_jrisc_pack (x);	/* { dg-error "only available on the GPU" "" { target jrisc_dsp } } */
#endif
}

unsigned
g (unsigned x, int n)
{
#ifdef __JRISC_DSP__
  return __builtin_jrisc_addqmod (x, n);	/* { dg-error "must be a constant" "" { target jrisc_dsp } } */
#else
  return x;
#endif
}
