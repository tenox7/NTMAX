/*
 *  int.c -- integer code: division, multiply, 64-bit, unaligned, switch,
 *  back-to-back loads (the R3000 load delay)
 */

typedef unsigned long ULONG;
typedef struct _BIG { ULONG a[9]; } BIG;

int g_counter;
BIG g_big;

int divs(int a, int b) { return a / b; }
unsigned divu(unsigned a, unsigned b) { return a % b; }
int mul(int a, int b) { return a * b; }
int muladd(int a, int b, int c) { return a * b + c / a; }
__int64 add64(__int64 a, __int64 b) { return a + b; }
__int64 mul64(__int64 a, __int64 b) { return a * b; }
__int64 div64(__int64 a, __int64 b) { return a / b; }
__int64 shl64(__int64 a, int n) { return a << n; }
ULONG unaligned_load(ULONG __unaligned *p) { return *p; }
void unaligned_store(ULONG __unaligned *p, ULONG v) { *p = v; }
void copybig(BIG *d, BIG *s) { *d = *s; }

int sw(int x)
{
	switch (x) {
	case 0: return 10;
	case 1: return 20;
	case 2: return 31;
	case 3: return 47;
	case 4: return 5;
	case 5: return 77;
	default: return -1;
	}
}

int loop(int *p, int n)
{
	int s = 0, i;

	for (i = 0; i < n; i++)
		s += p[i] * 3;
	return s;
}

int chain(int **p) { return p[p[0][0]][0]; }
int global(void) { return g_counter + g_big.a[3]; }
