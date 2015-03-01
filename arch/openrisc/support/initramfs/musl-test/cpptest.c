// work: or1k-linux-musl-gcc hello.c
// work: or1k-linux-musl-g++ --static hello.c
// doesn't work: or1k-linux-musl-g++ hello.c

#include<stdio.h>

int main()
{
        printf("Hello world\n");
        return 0;
}
