/*
 *  fp.c -- floating point: double loads/stores (ldc1/sdc1 are MIPS II),
 *  conversions (trunc.w.d is MIPS II), compare + branch, sqrt
 */

double sqrt(double);

double g_d;
float g_f;

double dadd(double a, double b) { return a + b; }
double dload(double *p) { return p[1] * 2.0; }
void dstore(double *p, double v) { p[2] = v; }
void dcopy(double *d, double *s, int n) { while (n--) *d++ = *s++; }
int d2i(double d) { return (int)d; }
unsigned d2u(double d) { return (unsigned)d; }
double i2d(int i) { return i; }
int fcmp(double a, double b) { return a < b ? 1 : 2; }
float fmul(float a, float b) { return a * b + g_f; }
double dsqrt(double x) { return sqrt(x); }
