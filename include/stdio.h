#ifndef IRIS_STDIO_H
#define IRIS_STDIO_H

#include <stdarg.h>
#include <stddef.h>

typedef struct iris_FILE FILE;

#define EOF (-1)
#define BUFSIZ 8192
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int vprintf(const char *format, va_list arguments);
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list arguments);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
FILE *fopen(const char *path, const char *mode);
int fclose(FILE *stream);
int fflush(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fgetc(FILE *stream);
int fputc(int character, FILE *stream);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);

#endif
