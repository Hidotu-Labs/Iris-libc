#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

extern ssize_t __iris_getline_impl(char **restrict line,
    size_t *restrict capacity, FILE *restrict stream,
    void *(*allocate)(size_t), void *(*resize)(void *, size_t));

ssize_t getline(char **restrict line, size_t *restrict capacity,
    FILE *restrict stream)
{
    return __iris_getline_impl(line, capacity, stream, malloc, realloc);
}

int vsscanf(const char *restrict input, const char *restrict format,
    va_list arguments)
{
    int assignments = 0;

    while (*format != '\0') {
        if (isspace((unsigned char)*format)) {
            while (isspace((unsigned char)*format)) format++;
            while (isspace((unsigned char)*input)) input++;
            continue;
        }
        if (*format != '%') {
            if (*input == '\0') {
                return assignments == 0 ? EOF : assignments;
            }
            if (*input != *format) {
                return assignments;
            }
            input++;
            format++;
            continue;
        }

        format++;
        if (*format == '%') {
            if (*input != '%') {
                return assignments;
            }
            input++;
            format++;
            continue;
        }
        if (*format != 'd') {
            errno = EINVAL;
            return assignments;
        }

        while (isspace((unsigned char)*input)) input++;
        if (*input == '\0') {
            return assignments == 0 ? EOF : assignments;
        }
        char *end = NULL;
        long value = strtol(input, &end, 10);
        if (end == input) {
            return assignments;
        }
        *va_arg(arguments, int *) = (int)value;
        assignments++;
        input = end;
        format++;
    }
    return assignments;
}

int sscanf(const char *restrict input, const char *restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsscanf(input, format, arguments);
    va_end(arguments);
    return result;
}

void perror(const char *message)
{
    int saved_errno = errno;
    if (message != NULL && message[0] != '\0') {
        fprintf(stderr, "%s: %s\n", message, strerror(saved_errno));
    } else {
        fprintf(stderr, "%s\n", strerror(saved_errno));
    }
    errno = saved_errno;
}