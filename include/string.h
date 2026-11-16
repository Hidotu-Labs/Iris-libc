#ifndef IRIS_STRING_H
#define IRIS_STRING_H

#include <stddef.h>

void *memcpy(void *restrict destination, const void *restrict source, size_t count);
void *memmove(void *destination, const void *source, size_t count);
void *memset(void *destination, int value, size_t count);
int memcmp(const void *left, const void *right, size_t count);
void *memchr(const void *memory, int character, size_t count);
size_t strlen(const char *string);
int strcmp(const char *left, const char *right);
int strncmp(const char *left, const char *right, size_t count);
int strcoll(const char *left, const char *right);
char *strcpy(char *restrict destination, const char *restrict source);
char *strncpy(char *restrict destination, const char *restrict source, size_t count);
char *strcat(char *restrict destination, const char *restrict source);
char *strncat(char *restrict destination, const char *restrict source, size_t count);
char *strchr(const char *string, int character);
char *strrchr(const char *string, int character);
char *strpbrk(const char *string, const char *accept);
size_t strspn(const char *string, const char *accept);
size_t strcspn(const char *string, const char *reject);
char *strstr(const char *string, const char *substring);
char *strerror(int error_number);

#endif
