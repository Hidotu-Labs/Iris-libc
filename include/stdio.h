#ifndef IRIS_STDIO_H
#define IRIS_STDIO_H

#include <stdarg.h>
#include <stddef.h>
#include <sys/types.h>

typedef struct iris_FILE FILE;

#define EOF (-1)
#define BUFSIZ 8192
#define SEEK_SET 0
#define SEEK_CUR 1
#define SEEK_END 2
#define _IOFBF 0
#define _IOLBF 1
#define _IONBF 2
#define L_tmpnam 64

extern FILE *stdin;
extern FILE *stdout;
extern FILE *stderr;

int printf(const char *format, ...);
int vprintf(const char *format, va_list arguments);
int fprintf(FILE *stream, const char *format, ...);
int vfprintf(FILE *stream, const char *format, va_list arguments);
int snprintf(char *buffer, size_t size, const char *format, ...);
int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
int sprintf(char *restrict buffer, const char *restrict format, ...);
int vsprintf(char *restrict buffer, const char *restrict format, va_list arguments);
FILE *fopen(const char *path, const char *mode);
FILE *freopen(const char *restrict path, const char *restrict mode, FILE *restrict stream);
FILE *tmpfile(void);
char *tmpnam(char *buffer);
int fclose(FILE *stream);
int fflush(FILE *stream);
int remove(const char *path);
int rename(const char *old_path, const char *new_path);
int setvbuf(FILE *restrict stream, char *restrict buffer, int mode, size_t size);
int fseek(FILE *stream, long offset, int whence);
long ftell(FILE *stream);
void rewind(FILE *stream);
size_t fread(void *buffer, size_t size, size_t count, FILE *stream);
size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream);
int fgetc(FILE *stream);
int getc(FILE *stream);
int ungetc(int character, FILE *stream);
int fputc(int character, FILE *stream);
char *fgets(char *buffer, int count, FILE *stream);
int fputs(const char *string, FILE *stream);
int puts(const char *string);
int feof(FILE *stream);
int ferror(FILE *stream);
void clearerr(FILE *stream);
ssize_t getline(char **restrict line, size_t *restrict capacity,
	FILE *restrict stream);
int vsscanf(const char *restrict input, const char *restrict format,
	va_list arguments);
int sscanf(const char *restrict input, const char *restrict format, ...);
void perror(const char *message);

#endif
