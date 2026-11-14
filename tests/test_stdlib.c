#include <errno.h>
#include <limits.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int compare_ints(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

int main(void)
{
    char *small = malloc(24);
    if (small == NULL) {
        return 1;
    }
    memcpy(small, "iris-malloc", 12);
    small = realloc(small, 64);
    if (small == NULL || strcmp(small, "iris-malloc") != 0) {
        return 2;
    }
    free(small);

    int *zeroed = calloc(16, sizeof(int));
    if (zeroed == NULL) {
        return 3;
    }
    for (size_t i = 0; i < 16; i++) {
        if (zeroed[i] != 0) {
            return 4;
        }
    }
    free(zeroed);

    char *large = malloc(128 * 1024);
    if (large == NULL) {
        return 5;
    }
    memset(large, 'z', 128 * 1024);
    if (large[0] != 'z' || large[128 * 1024 - 1] != 'z') {
        return 6;
    }
    free(large);

    char *end = NULL;
    if (atoi("  -42") != -42) {
        return 7;
    }
    if (strtol("0x1f", &end, 0) != 31 || *end != '\0') {
        return 8;
    }
    if (strtoul("18446744073709551615", &end, 10) != ULONG_MAX) {
        return 9;
    }

    int values[] = { 9, -3, 7, 1, 5, 0, -8, 4, 2, 6 };
    size_t count = sizeof(values) / sizeof(values[0]);
    qsort(values, count, sizeof(values[0]), compare_ints);
    for (size_t i = 1; i < count; i++) {
        if (values[i - 1] > values[i]) {
            return 10;
        }
    }

    if (abs(-123) != 123 || labs(-123456L) != 123456L) {
        return 11;
    }

    printf("stdlib-nostdlib: ok\n");
    exit(0);
}
