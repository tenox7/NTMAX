/*
 *  chello.c -- the first C900 program for the DECstation: compiled for the
 *  R3000 (/QMR3000), booted by the PROM like hello/hello.S, prints through
 *  the PROM's printf and halts.  No run-time library, no gp data (-Gt0).
 */

typedef int (*PROM_PRINTF)(char *Format, ...);
typedef void (*PROM_REX)(int Command);

#define REX_MAGIC	0x30464354
#define CB_PRINTF	(0x30 / 4)
#define CB_REX		(0xac / 4)

static int fib(int n)
{
	return n < 2 ? n : fib(n - 1) + fib(n - 2);
}

static unsigned div10(unsigned v)
{
	return v / 10;
}

static double area(double r)
{
	return 3.14159265358979 * r * r;
}

void entry(int argc, char **argv, unsigned magic, void **cv)
{
	PROM_PRINTF printf;
	int i;

	if (magic != REX_MAGIC)
		for (;;)
			;
	printf = (PROM_PRINTF)cv[CB_PRINTF];
	printf("Hello from C900 (/QMR3000) on the DECstation, argc %d\n", argc);
	for (i = 0; i < argc; i++)
		printf("  argv[%d] = %s\n", i, argv[i]);
	printf("fib(20) = %d, 1234567 / 10 = %d", fib(20), div10(1234567));
	printf(", area(2.0) * 1000 = %d\n", (int)(area(2.0) * 1000.0));
	((PROM_REX)cv[CB_REX])('h');
}
