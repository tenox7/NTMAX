/*
 *  dbgrun.c -- run a program of the MIPS NT machine (vm.sh) under a small
 *  debugger, so that a fault of the program is reported on the console
 *  instead of in an application error box on the machine's screen:
 *
 *	dbgrun [-a] PROGRAM [ARGUMENTS...]
 *
 *  It prints each exception the program takes (code, address, parameters),
 *  first chance and last chance, and on the last chance the registers, the
 *  base of the program and of the DLLs it loaded, then ends the program and
 *  returns 1.  Otherwise it returns the exit code of the program, 1 for an
 *  exit code that is an exception code (negative, which IF ERRORLEVEL 1 of
 *  a .cmd file does not see).  With -a it debugs the processes the program
 *  starts as well (nmake, then the midl it runs, then the cl that midl
 *  runs), and a fault of any of them, or a process that goes on taking
 *  exceptions (more than MAXEXCEPTIONS), ends them all: a build stops at the
 *  first program that faults.  Without -a they are not debugged, but they
 *  run with SEM_NOGPFAULTERRORBOX as the program does: a fault ends them
 *  without the box.
 */

#include <windows.h>
#include <stdio.h>
#include <string.h>

#define MAXPROCS 32
#define MAXTHREADS 128
#define MAXDLLS 64
#define MAXEXCEPTIONS 100

static struct proc {
	DWORD id;
	HANDLE h;
	LPVOID image;
	LPVOID dlls[MAXDLLS];
	int ndlls;
	int started;		/* past the breakpoint of the loader */
	int exceptions;		/* exceptions it took */
} procs[MAXPROCS];
static int nprocs;

static struct {
	DWORD pid, id;
	HANDLE h;
} threads[MAXTHREADS];
static int nthreads;

/* the command line after the name of this program and its option */
static char *tail(int *all)
{
	char *p = GetCommandLineA();
	int q = 0;

	while (*p && (q || (*p != ' ' && *p != '\t'))) {
		if (*p == '"')
			q = !q;
		p++;
	}
	while (*p == ' ' || *p == '\t')
		p++;
	*all = 0;
	if (p[0] == '-' && p[1] == 'a' && (p[2] == ' ' || p[2] == '\t')) {
		*all = 1;
		for (p += 2; *p == ' ' || *p == '\t'; p++)
			;
	}
	return p;
}

/*
 * Process and thread ids are used again once their process or thread is
 * gone (cl after cl of a MIDL run): the entries go when it exits.
 */
static struct proc *proc(DWORD id)
{
	int i;

	for (i = 0; i < nprocs; i++)
		if (procs[i].h != NULL && procs[i].id == id)
			return &procs[i];
	return NULL;
}

static struct proc *newproc(DWORD id)
{
	int i;

	for (i = 0; i < nprocs; i++)
		if (procs[i].h == NULL)
			break;
	if (i == nprocs) {
		if (nprocs == MAXPROCS)
			return NULL;
		nprocs++;
	}
	memset(&procs[i], 0, sizeof procs[i]);
	procs[i].id = id;
	return &procs[i];
}

static HANDLE thread(DWORD pid, DWORD id)
{
	int i;

	for (i = 0; i < nthreads; i++)
		if (threads[i].h != NULL && threads[i].pid == pid && threads[i].id == id)
			return threads[i].h;
	return NULL;
}

static void addthread(DWORD pid, DWORD id, HANDLE h)
{
	int i;

	for (i = 0; i < nthreads; i++)
		if (threads[i].h == NULL)
			break;
	if (i == nthreads) {
		if (nthreads == MAXTHREADS)
			return;
		nthreads++;
	}
	threads[i].pid = pid;
	threads[i].id = id;
	threads[i].h = h;
}

/* forget a thread, or all threads of a process (id 0) */
static void delthread(DWORD pid, DWORD id)
{
	int i;

	for (i = 0; i < nthreads; i++)
		if (threads[i].h != NULL && threads[i].pid == pid && (id == 0 || threads[i].id == id))
			threads[i].h = NULL;
}

/* end every process being debugged: one faulted, or takes exceptions in a loop */
static int endall(HANDLE main, char *why)
{
	int i;

	printf("dbgrun: %s, ending all processes\n", why);
	for (i = 0; i < nprocs; i++)
		if (procs[i].h != NULL)
			TerminateProcess(procs[i].h, 1);
	TerminateProcess(main, 1);
	return 1;
}

/*
 * The MIPS CONTEXT_FULL of NT 4.0 has CONTEXT_EXTENDED_INTEGER (the 64-bit
 * registers, in other fields): the 32-bit registers are CONTEXT_CONTROL and
 * CONTEXT_INTEGER.
 */
#define CONTEXT_REGS (CONTEXT_CONTROL | CONTEXT_INTEGER)

static void registers(HANDLE h)
{
	CONTEXT c;

	c.ContextFlags = CONTEXT_REGS;
	if (h == NULL || !GetThreadContext(h, &c)) {
		printf("dbgrun: no context (%lu)\n", GetLastError());
		return;
	}
#if defined(_M_MRX000)
	printf("  fir %08lx psr %08lx ra %08lx sp %08lx gp %08lx\n",
	    c.Fir, c.Psr, c.IntRa, c.IntSp, c.IntGp);
	printf("  v0 %08lx v1 %08lx a0 %08lx a1 %08lx a2 %08lx a3 %08lx\n",
	    c.IntV0, c.IntV1, c.IntA0, c.IntA1, c.IntA2, c.IntA3);
	printf("  t0 %08lx t1 %08lx t2 %08lx t3 %08lx t4 %08lx t5 %08lx\n",
	    c.IntT0, c.IntT1, c.IntT2, c.IntT3, c.IntT4, c.IntT5);
	printf("  t6 %08lx t7 %08lx t8 %08lx t9 %08lx at %08lx s8 %08lx\n",
	    c.IntT6, c.IntT7, c.IntT8, c.IntT9, c.IntAt, c.IntS8);
	printf("  s0 %08lx s1 %08lx s2 %08lx s3 %08lx s4 %08lx s5 %08lx\n",
	    c.IntS0, c.IntS1, c.IntS2, c.IntS3, c.IntS4, c.IntS5);
	printf("  s6 %08lx s7 %08lx\n", c.IntS6, c.IntS7);
#elif defined(_M_IX86)
	printf("  eip %08lx esp %08lx ebp %08lx eax %08lx ebx %08lx ecx %08lx\n",
	    c.Eip, c.Esp, c.Ebp, c.Eax, c.Ebx, c.Ecx);
#endif
}

/*
 * Continue a thread after a breakpoint it took: a MIPS thread stops with the
 * address of the break instruction (an x86 thread after its int 3), so the
 * debugger steps past it.
 */
static void skipbreak(HANDLE p, HANDLE h)
{
#if defined(_M_MRX000)
	CONTEXT c;
	DWORD insn, n;

	c.ContextFlags = CONTEXT_CONTROL;
	if (h == NULL || !GetThreadContext(h, &c))
		return;
	if (ReadProcessMemory(p, (LPCVOID)c.Fir, &insn, sizeof insn, &n) &&
	    (insn & 0xfc00003f) == 0x0000000d) {
		c.Fir += 4;
		SetThreadContext(h, &c);
	}
#endif
}

int main(void)
{
	STARTUPINFOA si;
	PROCESS_INFORMATION pi;
	DEBUG_EVENT e;
	EXCEPTION_RECORD *r;
	struct proc *p;
	DWORD cont, code;
	int all, i;
	char *cmd = tail(&all);

	if (*cmd == 0) {
		fprintf(stderr, "usage: dbgrun [-a] PROGRAM [ARGUMENTS...]\n");
		return 1;
	}
	SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX);
	memset(&si, 0, sizeof si);
	si.cb = sizeof si;
	if (!CreateProcessA(NULL, cmd, NULL, NULL, TRUE,
	    all ? DEBUG_PROCESS : DEBUG_ONLY_THIS_PROCESS, NULL, NULL, &si, &pi)) {
		fprintf(stderr, "dbgrun: cannot start %s (%lu)\n", cmd, GetLastError());
		return 1;
	}
	setvbuf(stdout, NULL, _IONBF, 0);
	for (;;) {
		if (!WaitForDebugEvent(&e, INFINITE)) {
			fprintf(stderr, "dbgrun: WaitForDebugEvent (%lu)\n", GetLastError());
			TerminateProcess(pi.hProcess, 1);
			return 1;
		}
		cont = DBG_CONTINUE;
		p = proc(e.dwProcessId);
		switch (e.dwDebugEventCode) {
		case CREATE_PROCESS_DEBUG_EVENT:
			if (e.u.CreateProcessInfo.hFile != NULL)
				CloseHandle(e.u.CreateProcessInfo.hFile);
			if (p != NULL)		/* (an exit that did not come) */
				p->h = NULL;
			delthread(e.dwProcessId, 0);
			p = newproc(e.dwProcessId);
			if (p == NULL) {
				return endall(pi.hProcess, "too many processes");
			}
			p->h = e.u.CreateProcessInfo.hProcess;
			p->image = e.u.CreateProcessInfo.lpBaseOfImage;
			addthread(e.dwProcessId, e.dwThreadId, e.u.CreateProcessInfo.hThread);
			break;
		case CREATE_THREAD_DEBUG_EVENT:
			addthread(e.dwProcessId, e.dwThreadId, e.u.CreateThread.hThread);
			break;
		case EXIT_THREAD_DEBUG_EVENT:
			delthread(e.dwProcessId, e.dwThreadId);
			break;
		case LOAD_DLL_DEBUG_EVENT:
			if (e.u.LoadDll.hFile != NULL)
				CloseHandle(e.u.LoadDll.hFile);
			if (p != NULL && p->ndlls < MAXDLLS)
				p->dlls[p->ndlls++] = e.u.LoadDll.lpBaseOfDll;
			break;
		case EXCEPTION_DEBUG_EVENT:
			r = &e.u.Exception.ExceptionRecord;
			/* (the breakpoint of the loader, before the program runs) */
			if (p != NULL && !p->started && r->ExceptionCode == EXCEPTION_BREAKPOINT) {
				p->started = 1;
				skipbreak(p->h, thread(e.dwProcessId, e.dwThreadId));
				break;
			}
			if (p == NULL || ++p->exceptions > MAXEXCEPTIONS)
				return endall(pi.hProcess, "exception loop");
			printf("dbgrun: process %lu: %s chance exception %08lx at %08lx, thread %lu",
			    e.dwProcessId, e.u.Exception.dwFirstChance ? "first" : "last",
			    r->ExceptionCode, (DWORD)r->ExceptionAddress, e.dwThreadId);
			for (i = 0; i < (int)r->NumberParameters && i < EXCEPTION_MAXIMUM_PARAMETERS; i++)
				printf(" p%d %08lx", i, r->ExceptionInformation[i]);
			printf("\n");
			if (e.u.Exception.dwFirstChance) {
				cont = DBG_EXCEPTION_NOT_HANDLED;
				break;
			}
			registers(thread(e.dwProcessId, e.dwThreadId));
			printf("  image %08lx, DLLs:", (DWORD)p->image);
			for (i = 0; i < p->ndlls; i++)
				printf(" %08lx", (DWORD)p->dlls[i]);
			printf("\n");
			return endall(pi.hProcess, "fault");
		case EXIT_PROCESS_DEBUG_EVENT:
			code = e.u.ExitProcess.dwExitCode;
			if (p != NULL)
				p->h = NULL;
			delthread(e.dwProcessId, 0);
			if (e.dwProcessId == pi.dwProcessId) {
				ContinueDebugEvent(e.dwProcessId, e.dwThreadId, DBG_CONTINUE);
				if ((code & 0x80000000) == 0)
					return (int)code;
				printf("dbgrun: ended with %08lx\n", code);
				return 1;
			}
			/* (a process the program started ended by an exception) */
			if ((code & 0xf0000000) == 0x80000000 || (code & 0xf0000000) == 0xc0000000)
				printf("dbgrun: process %lu ended with %08lx\n", e.dwProcessId, code);
			break;
		}
		ContinueDebugEvent(e.dwProcessId, e.dwThreadId, cont);
	}
}
