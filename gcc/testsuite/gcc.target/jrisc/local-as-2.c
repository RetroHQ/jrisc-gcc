/* An ordinary pointer converts to a __local one only with a cast, and
   __local is for variables with static storage only.  */
/* { dg-do compile } */
/* { dg-options "-std=gnu99" } */

void fill_local (__local unsigned *, unsigned, int);

unsigned *
to_generic (__local unsigned *p)
{
  return p;		/* No cast needed.  */
}

void
f (unsigned *q)
{
  fill_local (q, 1, 1);	/* { dg-error "non-enclosed address space" } */
  fill_local ((__local unsigned *) q, 1, 1);
}

int
g (void)
{
  __local int x = 1;	/* { dg-error "'__local' specified for auto variable" } */
  return x;
}
