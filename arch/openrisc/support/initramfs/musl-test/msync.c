#include <stdio.h>

int main(void)
{
	printf("issuing l.msync\n");
	__asm__ __volatile__("l.msync");
	printf("done\n");

	printf("issuing l.psync\n");
	__asm__ __volatile__("l.psync");
	printf("done\n");

	printf("issuing l.csync\n");
	__asm__ __volatile__("l.csync");
	printf("done\n");

	return 0;
}
