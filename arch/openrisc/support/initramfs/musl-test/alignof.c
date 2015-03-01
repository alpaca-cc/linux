typedef struct {
	long long __max_align_ll __attribute__((__aligned__(__alignof__(long long))));
	long double __max_align_ld __attribute__((__aligned__(__alignof__(long double))));
} max_align_t;

#include <stdio.h>

int main(void)
{
	printf("%zu %zu\n", _Alignof(max_align_t), sizeof(max_align_t));

	return 0;
}
