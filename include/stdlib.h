#ifndef IRIS_STDLIB_H
#define IRIS_STDLIB_H

#include <stddef.h>

#define EXIT_SUCCESS 0
#define EXIT_FAILURE 1

void *malloc(size_t size);
void *calloc(size_t count, size_t size);
void *realloc(void *pointer, size_t size);
void free(void *pointer);
int atexit(void (*function)(void));

_Noreturn void exit(int status);
_Noreturn void _Exit(int status);
_Noreturn void abort(void);

int abs(int value);
long labs(long value);
int atoi(const char *string);
char *getenv(const char *name);
int system(const char *command);
long strtol(const char *restrict string, char **restrict end_pointer, int base);
unsigned long strtoul(const char *restrict string, char **restrict end_pointer,
    int base);
double strtod(const char *restrict string, char **restrict end_pointer);

void qsort(void *base, size_t count, size_t size,
    int (*compare)(const void *, const void *));

#endif
