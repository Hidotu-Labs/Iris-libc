#include <stdio.h>

int main(void)
{
    if (printf("Hello, world!\n") < 0 || fflush(stdout) != 0) {
        return 1;
    }
    return 0;
}