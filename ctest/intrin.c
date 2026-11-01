/*
 *  intrin.c -- the compiler's built-in functions in R3000 mode:
 *  _enable/_disable (mfc0/mtc0 of the status register) and sqrt (sqrt.d).
 *  With -DCALLS they are made real calls (#pragma function).
 */

void _enable(void);
void _disable(void);
double sqrt(double);

#if defined(CALLS)
#pragma function(_enable, _disable, sqrt)
#endif

int g;

void irq(int *p)
{
	_disable();
	*p = g + 1;
	_enable();
}

double root(double x)
{
	return sqrt(x);
}
