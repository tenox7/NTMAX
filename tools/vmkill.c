/*
 *  vmkill.c -- end processes of the MIPS NT machine (vm.sh) by process id,
 *  for a program that runs away (the machine has no kill command):
 *
 *	vmkill PID...
 */

#include <windows.h>
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
	HANDLE h;
	DWORD pid;
	int i, rc = 0;

	for (i = 1; i < argc; i++) {
		pid = strtoul(argv[i], NULL, 0);
		h = OpenProcess(PROCESS_TERMINATE, FALSE, pid);
		if (h == NULL || !TerminateProcess(h, 1)) {
			printf("vmkill: %lu: error %lu\n", pid, GetLastError());
			rc = 1;
		} else
			printf("vmkill: %lu: ended\n", pid);
		if (h != NULL)
			CloseHandle(h);
	}
	return rc;
}
