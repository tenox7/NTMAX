/*
 *  seh.c -- structured exception handling, as the NT kernel uses it
 */

int g_counter;

int seh_except(int *p)
{
	int r;

	__try {
		r = *p;
	} __except (1) {
		r = -1;
	}
	return r;
}

void seh_finally(int *p)
{
	__try {
		*p = 1;
	} __finally {
		g_counter++;
	}
}
