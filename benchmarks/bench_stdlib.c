#define _POSIX_C_SOURCE 200809L

#include "bench_common.h"

#include <ctype.h>
#include <errno.h>
#include <fcntl.h>
#include <locale.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/prctl.h>
#include <sys/resource.h>
#include <sys/wait.h>
#include <unistd.h>

#define STDLIB_ITERATIONS 20000U
#define QSORT_ITERATIONS 200U
#define GETLINE_ITERATIONS 1000U
#define TERMINATION_ITERATIONS 24U
#define SYSTEM_QUERY_ITERATIONS 32U

extern void *iris_malloc(size_t size);
extern void *iris_calloc(size_t count, size_t size);
extern void *iris_realloc(void *pointer, size_t size);
extern void iris_free(void *pointer);
extern int iris_abs(int value);
extern long iris_labs(long value);
extern int iris_atoi(const char *string);
extern char *iris_getenv(const char *name);
extern int iris_system(const char *command);
extern double iris_strtod(const char *restrict string,
    char **restrict end_pointer);
extern long iris_strtol(const char *restrict string, char **restrict end_pointer, int base);
extern unsigned long iris_strtoul(const char *restrict string,
    char **restrict end_pointer, int base);
extern void iris_qsort(void *base, size_t count, size_t size,
    int (*compare)(const void *, const void *));
extern char *iris_setlocale(int category, const char *locale);
extern struct lconv *iris_localeconv(void);
extern int iris_isalnum(int character);
extern int iris_isalpha(int character);
extern int iris_isblank(int character);
extern int iris_iscntrl(int character);
extern int iris_isdigit(int character);
extern int iris_isgraph(int character);
extern int iris_islower(int character);
extern int iris_isprint(int character);
extern int iris_ispunct(int character);
extern int iris_isspace(int character);
extern int iris_isupper(int character);
extern int iris_isxdigit(int character);
extern int iris_tolower(int character);
extern int iris_toupper(int character);
extern int iris_atexit(void (*function)(void));
typedef struct iris_FILE iris_FILE;
extern iris_FILE *iris_fopen(const char *path, const char *mode);
extern int iris_fclose(iris_FILE *stream);
extern void iris_rewind(iris_FILE *stream);
extern ssize_t iris_getline(char **line, size_t *capacity, iris_FILE *stream);
extern int iris_sscanf(const char *input, const char *format, ...);
extern int iris_vsscanf(const char *restrict input,
    const char *restrict format, va_list arguments);
extern void iris_perror(const char *message);
extern int iris_fflush(iris_FILE *stream);
extern _Noreturn void iris_exit(int status);
extern _Noreturn void iris__Exit(int status);
extern _Noreturn void iris_abort(void);
extern _Noreturn void iris__exit(int status);

typedef void *(*malloc_function)(size_t);
typedef void *(*calloc_function)(size_t, size_t);
typedef void *(*realloc_function)(void *, size_t);
typedef void (*free_function)(void *);
typedef long (*strtol_function)(const char *restrict, char **restrict, int);
typedef unsigned long (*strtoul_function)(const char *restrict,
    char **restrict, int);
typedef void (*qsort_function)(void *, size_t, size_t,
    int (*)(const void *, const void *));
typedef int (*ctype_function)(int);
typedef double (*benchmark_function)(size_t iterations);
typedef int (*scanf_function)(const char *, const char *, ...);
typedef void (*perror_function)(const char *);

#define DEFINE_HOST_CTYPE_WRAPPER(name) \
    static int host_##name(int character) { return name(character); }

DEFINE_HOST_CTYPE_WRAPPER(isalnum)
DEFINE_HOST_CTYPE_WRAPPER(isalpha)
DEFINE_HOST_CTYPE_WRAPPER(isblank)
DEFINE_HOST_CTYPE_WRAPPER(iscntrl)
DEFINE_HOST_CTYPE_WRAPPER(isdigit)
DEFINE_HOST_CTYPE_WRAPPER(isgraph)
DEFINE_HOST_CTYPE_WRAPPER(islower)
DEFINE_HOST_CTYPE_WRAPPER(isprint)
DEFINE_HOST_CTYPE_WRAPPER(ispunct)
DEFINE_HOST_CTYPE_WRAPPER(isspace)
DEFINE_HOST_CTYPE_WRAPPER(isupper)
DEFINE_HOST_CTYPE_WRAPPER(isxdigit)
DEFINE_HOST_CTYPE_WRAPPER(tolower)
DEFINE_HOST_CTYPE_WRAPPER(toupper)

#undef DEFINE_HOST_CTYPE_WRAPPER

struct ctype_bench_case {
    const char *name;
    ctype_function host_function;
    ctype_function iris_function;
};

static const struct ctype_bench_case ctype_cases[] = {
    { "isalnum", host_isalnum, iris_isalnum },
    { "isalpha", host_isalpha, iris_isalpha },
    { "isblank", host_isblank, iris_isblank },
    { "iscntrl", host_iscntrl, iris_iscntrl },
    { "isdigit", host_isdigit, iris_isdigit },
    { "isgraph", host_isgraph, iris_isgraph },
    { "islower", host_islower, iris_islower },
    { "isprint", host_isprint, iris_isprint },
    { "ispunct", host_ispunct, iris_ispunct },
    { "isspace", host_isspace, iris_isspace },
    { "isupper", host_isupper, iris_isupper },
    { "isxdigit", host_isxdigit, iris_isxdigit },
    { "tolower", host_tolower, iris_tolower },
    { "toupper", host_toupper, iris_toupper },
};

static volatile size_t benchmark_sink;
static FILE *host_line_stream;
static iris_FILE *iris_line_stream;

static int compare_bench_ints(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

static double time_alloc_free(size_t size, size_t iterations,
    malloc_function alloc_fn, free_function free_fn)
{
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        unsigned char *ptr = (unsigned char *)alloc_fn(size);
        if (ptr != NULL) {
            ptr[0] = (unsigned char)index;
            benchmark_sink += ptr[0];
            free_fn(ptr);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_calloc_free(size_t size, size_t iterations,
    calloc_function calloc_fn, free_function free_fn)
{
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        unsigned char *ptr = (unsigned char *)calloc_fn(1, size);
        if (ptr != NULL) {
            benchmark_sink += ptr[0];
            free_fn(ptr);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_strtol(size_t iterations, strtol_function fn)
{
    static const char *samples[] = { "  -123456789", "0x7fffffff", "42", "987654321" };
    struct timespec start;
    struct timespec end;
    long sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        char *end_ptr = NULL;
        sum += fn(samples[index & 3U], &end_ptr, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_qsort(size_t iterations, qsort_function fn)
{
    int data[256];
    struct timespec start;
    struct timespec end;
    double total = 0.0;

    for (size_t index = 0; index < iterations; index++) {
        for (size_t i = 0; i < 256; i++) {
            data[i] = (int)((i * 37U + index) & 255U);
        }
        clock_gettime(CLOCK_MONOTONIC, &start);
        fn(data, 256, sizeof(int), compare_bench_ints);
        clock_gettime(CLOCK_MONOTONIC, &end);
        total += elapsed_nanoseconds(start, end);
        benchmark_sink += (size_t)data[0] + (size_t)data[255];
    }
    return total / (double)iterations;
}

static double time_ctype(size_t iterations, ctype_function fn)
{
    static const int samples[] = {
        -1, 0, '\t', ' ', '0', '9', 'A', 'F', 'Z', 'a', 'f', 'z', '_', '~', 0x80
    };
    struct timespec start;
    struct timespec end;
    int sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        sum += fn(samples[index % (sizeof(samples) / sizeof(samples[0]))]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static int no_op_exit_handler(void)
{
    return 0;
}

static double time_host_atexit(size_t iterations)
{
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += atexit((void (*)(void))no_op_exit_handler);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_iris_atexit(size_t iterations)
{
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += iris_atexit((void (*)(void))no_op_exit_handler);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_sscanf(size_t iterations)
{
    struct timespec start;
    struct timespec end;
    int sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        int rows = 0;
        int columns = 0;
        sum += sscanf("24;80", "%d;%d", &rows, &columns) + rows + columns;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_iris_sscanf(size_t iterations)
{
    struct timespec start;
    struct timespec end;
    int sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        int rows = 0;
        int columns = 0;
        sum += iris_sscanf("24;80", "%d;%d", &rows, &columns) + rows + columns;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static int host_vsscanf_call(const char *input, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsscanf(input, format, arguments);
    va_end(arguments);
    return result;
}

static int iris_vsscanf_call(const char *input, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = iris_vsscanf(input, format, arguments);
    va_end(arguments);
    return result;
}

static double time_vsscanf(size_t iterations, scanf_function function)
{
    struct timespec start;
    struct timespec end;
    int sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        int rows = 0;
        int columns = 0;
        sum += function("24;80", "%d;%d", &rows, &columns) + rows + columns;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_vsscanf(size_t iterations)
{
    return time_vsscanf(iterations, host_vsscanf_call);
}

static double time_iris_vsscanf(size_t iterations)
{
    return time_vsscanf(iterations, iris_vsscanf_call);
}

static double time_realloc(size_t iterations, realloc_function realloc_fn,
    malloc_function malloc_fn, free_function free_fn)
{
    struct timespec start;
    struct timespec end;
    unsigned char *block = malloc_fn(64);
    if (block == NULL) {
        return 0.0;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        size_t size = (index & 1U) == 0 ? 4096 : 64;
        unsigned char *updated = realloc_fn(block, size);
        if (updated == NULL) {
            free_fn(block);
            return 0.0;
        }
        block = updated;
        block[0] = (unsigned char)index;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += block[0];
    free_fn(block);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_realloc(size_t iterations)
{
    return time_realloc(iterations, realloc, malloc, free);
}

static double time_iris_realloc(size_t iterations)
{
    return time_realloc(iterations, iris_realloc, iris_malloc, iris_free);
}

static double time_int_conversion(size_t iterations, int (*function)(int))
{
    struct timespec start;
    struct timespec end;
    int sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        sum += function((int)(index % 201U) - 100);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_abs(size_t iterations)
{
    return time_int_conversion(iterations, abs);
}

static double time_iris_abs(size_t iterations)
{
    return time_int_conversion(iterations, iris_abs);
}

static double time_long_conversion(size_t iterations, long (*function)(long))
{
    struct timespec start;
    struct timespec end;
    long sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        sum += function((long)(index % 2001U) - 1000);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_labs(size_t iterations)
{
    return time_long_conversion(iterations, labs);
}

static double time_iris_labs(size_t iterations)
{
    return time_long_conversion(iterations, iris_labs);
}

static double time_atoi(size_t iterations, int (*function)(const char *))
{
    static const char *samples[] = { "-12345", "0", "67890", "-42" };
    struct timespec start;
    struct timespec end;
    int sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        sum += function(samples[index & 3U]);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_atoi(size_t iterations)
{
    return time_atoi(iterations, atoi);
}

static double time_iris_atoi(size_t iterations)
{
    return time_atoi(iterations, iris_atoi);
}

static double time_strtoul(size_t iterations, strtoul_function function)
{
    static const char *samples[] = { "4294967295", "0xffff", "0777", "42" };
    struct timespec start;
    struct timespec end;
    unsigned long sum = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        char *end_pointer = NULL;
        sum += function(samples[index & 3U], &end_pointer, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_strtoul(size_t iterations)
{
    return time_strtoul(iterations, strtoul);
}

static double time_iris_strtoul(size_t iterations)
{
    return time_strtoul(iterations, iris_strtoul);
}

static double time_strtod(size_t iterations,
    double (*function)(const char *restrict, char **restrict))
{
    static const char *samples[] = { "-12.5", "0x1.8p+2", ".125e2", "1e-3" };
    struct timespec start;
    struct timespec end;
    double sum = 0.0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        char *end_pointer = NULL;
        sum += function(samples[index & 3U], &end_pointer);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(sum * 1000.0);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_strtod(size_t iterations)
{
    return time_strtod(iterations, strtod);
}

static double time_iris_strtod(size_t iterations)
{
    return time_strtod(iterations, iris_strtod);
}

static double time_getenv(size_t iterations, char *(*function)(const char *))
{
    struct timespec start;
    struct timespec end;
    size_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        const char *name = (index & 1U) == 0 ? "PATH" : "HOME";
        char *value = function(name);
        result += value != NULL ? strlen(value) : 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_getenv(size_t iterations)
{
    return time_getenv(iterations, getenv);
}

static double time_iris_getenv(size_t iterations)
{
    return time_getenv(iterations, iris_getenv);
}

static double time_system_query(size_t iterations, int (*function)(const char *))
{
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function(NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_system_query(size_t iterations)
{
    return time_system_query(iterations, system);
}

static double time_iris_system_query(size_t iterations)
{
    return time_system_query(iterations, iris_system);
}

static double time_localeconv(size_t iterations, struct lconv *(*function)(void))
{
    struct timespec start;
    struct timespec end;
    size_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        struct lconv *conventions = function();
        result += conventions != NULL && conventions->decimal_point != NULL
            ? (unsigned char)conventions->decimal_point[0] : 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_localeconv(size_t iterations)
{
    return time_localeconv(iterations, localeconv);
}

static double time_iris_localeconv(size_t iterations)
{
    return time_localeconv(iterations, iris_localeconv);
}

static double time_setlocale(size_t iterations,
    char *(*function)(int, const char *))
{
    struct timespec start;
    struct timespec end;
    size_t successes = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        const char *name = (index & 1U) == 0 ? "C" : "POSIX";
        successes += function(LC_CTYPE, name) != NULL;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += successes;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_setlocale(size_t iterations)
{
    return time_setlocale(iterations, setlocale);
}

static double time_iris_setlocale(size_t iterations)
{
    return time_setlocale(iterations, iris_setlocale);
}

static void host_exit_case(void) { exit(EXIT_SUCCESS); }
static void iris_exit_case(void) { iris_exit(EXIT_SUCCESS); }
static void host__Exit_case(void) { _Exit(EXIT_SUCCESS); }
static void iris__Exit_case(void) { iris__Exit(EXIT_SUCCESS); }
static void host_abort_case(void) { abort(); }
static void iris_abort_case(void) { iris_abort(); }
static void host__exit_case(void) { _exit(EXIT_SUCCESS); }
static void iris__exit_case(void) { iris__exit(EXIT_SUCCESS); }

static double time_termination(size_t iterations, void (*function)(void))
{
    struct timespec start;
    struct timespec end;

    fflush(NULL);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        pid_t child = fork();
        if (child == 0) {
            if (prctl(PR_SET_DUMPABLE, 0, 0, 0, 0) != 0) {
                _exit(126);
            }
            function();
            _exit(127);
        }
        if (child < 0) {
            return 0.0;
        }
        int status = 0;
        if (waitpid(child, &status, 0) != child) {
            return 0.0;
        }
        if (WIFEXITED(status) && WEXITSTATUS(status) == 126) {
            return 0.0;
        }
        benchmark_sink += (size_t)status;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

#define DEFINE_TERMINATION_BENCH(name, function) \
    static double time_##name(size_t iterations) \
    { \
        return time_termination(iterations, function); \
    }

DEFINE_TERMINATION_BENCH(host_exit, host_exit_case)
DEFINE_TERMINATION_BENCH(iris_exit, iris_exit_case)
DEFINE_TERMINATION_BENCH(host__Exit, host__Exit_case)
DEFINE_TERMINATION_BENCH(iris__Exit, iris__Exit_case)
DEFINE_TERMINATION_BENCH(host_abort, host_abort_case)
DEFINE_TERMINATION_BENCH(iris_abort, iris_abort_case)
DEFINE_TERMINATION_BENCH(host__exit, host__exit_case)
DEFINE_TERMINATION_BENCH(iris__exit, iris__exit_case)

#undef DEFINE_TERMINATION_BENCH

static double time_host_getline(size_t iterations)
{
    struct timespec start;
    struct timespec end;
    char *line = NULL;
    size_t capacity = 0;
    size_t sum = 0;

    rewind(host_line_stream);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        ssize_t length = getline(&line, &capacity, host_line_stream);
        sum += length > 0 ? (size_t)length : 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    free(line);
    benchmark_sink += sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_iris_getline(size_t iterations)
{
    struct timespec start;
    struct timespec end;
    char *line = NULL;
    size_t capacity = 0;
    size_t sum = 0;

    iris_rewind(iris_line_stream);
    iris_rewind(iris_line_stream);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        ssize_t length = iris_getline(&line, &capacity, iris_line_stream);
        sum += length > 0 ? (size_t)length : 0;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    iris_free(line);
    benchmark_sink += sum;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_perror(size_t iterations, perror_function function)
{
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        errno = ENOENT;
        function("bench");
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static void report_benchmark(const char *name, size_t iterations,
    benchmark_function host_function, benchmark_function iris_function)
{
    double host_samples[TRIALS];
    double iris_samples[TRIALS];

    for (size_t trial = 0; trial < TRIALS; trial++) {
        if ((trial & 1U) == 0) {
            host_samples[trial] = host_function(iterations);
            iris_samples[trial] = iris_function(iterations);
        } else {
            iris_samples[trial] = iris_function(iterations);
            host_samples[trial] = host_function(iterations);
        }
    }
    double host_median = median(host_samples);
    double iris_median = median(iris_samples);
    printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", name, (size_t)0,
        host_median, iris_median,
        iris_median > 0.0 ? host_median / iris_median : 0.0);
}

static double host_perror_benchmark(size_t iterations)
{
    return time_perror(iterations, perror);
}

static double iris_perror_benchmark(size_t iterations)
{
    return time_perror(iterations, iris_perror);
}

int main(void)
{
    printf("stdlib functions vs %s; median of %d trials\n",
        BENCH_LIBC_NAME, TRIALS);
    puts("+----------+----------+------------------+------------------+------------------+");
    printf("| %-8s | %-8s | %-16s | %-16s | %-16s |\n",
        "Function", "Size (B)", "libc ns/op", "Iris ns/op", "speedup");
    puts("+----------+----------+------------------+------------------+------------------+");

    const size_t alloc_sizes[] = { 64, 4096 };
    for (size_t i = 0; i < 2; i++) {
        double host_samples[TRIALS];
        double iris_samples[TRIALS];
        for (size_t trial = 0; trial < TRIALS; trial++) {
            if ((trial & 1U) == 0) {
                host_samples[trial] = time_alloc_free(alloc_sizes[i], STDLIB_ITERATIONS, malloc, free);
                iris_samples[trial] = time_alloc_free(alloc_sizes[i], STDLIB_ITERATIONS, iris_malloc, iris_free);
            } else {
                iris_samples[trial] = time_alloc_free(alloc_sizes[i], STDLIB_ITERATIONS, iris_malloc, iris_free);
                host_samples[trial] = time_alloc_free(alloc_sizes[i], STDLIB_ITERATIONS, malloc, free);
            }
        }
        double host_median = median(host_samples);
        double iris_median = median(iris_samples);
        printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", "malloc", alloc_sizes[i],
            host_median, iris_median, iris_median > 0.0 ? host_median / iris_median : 0.0);
    }

    {
        double host_samples[TRIALS];
        double iris_samples[TRIALS];
        for (size_t trial = 0; trial < TRIALS; trial++) {
            if ((trial & 1U) == 0) {
                host_samples[trial] = time_calloc_free(64, STDLIB_ITERATIONS, calloc, free);
                iris_samples[trial] = time_calloc_free(64, STDLIB_ITERATIONS, iris_calloc, iris_free);
            } else {
                iris_samples[trial] = time_calloc_free(64, STDLIB_ITERATIONS, iris_calloc, iris_free);
                host_samples[trial] = time_calloc_free(64, STDLIB_ITERATIONS, calloc, free);
            }
        }
        double host_median = median(host_samples);
        double iris_median = median(iris_samples);
        printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", "calloc", (size_t)64,
            host_median, iris_median, iris_median > 0.0 ? host_median / iris_median : 0.0);
    }

    {
        double host_samples[TRIALS];
        double iris_samples[TRIALS];
        for (size_t trial = 0; trial < TRIALS; trial++) {
            if ((trial & 1U) == 0) {
                host_samples[trial] = time_strtol(STDLIB_ITERATIONS, strtol);
                iris_samples[trial] = time_strtol(STDLIB_ITERATIONS, iris_strtol);
            } else {
                iris_samples[trial] = time_strtol(STDLIB_ITERATIONS, iris_strtol);
                host_samples[trial] = time_strtol(STDLIB_ITERATIONS, strtol);
            }
        }
        double host_median = median(host_samples);
        double iris_median = median(iris_samples);
        printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", "strtol", (size_t)0,
            host_median, iris_median, iris_median > 0.0 ? host_median / iris_median : 0.0);
    }

    {
        double host_samples[TRIALS];
        double iris_samples[TRIALS];
        for (size_t trial = 0; trial < TRIALS; trial++) {
            if ((trial & 1U) == 0) {
                host_samples[trial] = time_qsort(QSORT_ITERATIONS, qsort);
                iris_samples[trial] = time_qsort(QSORT_ITERATIONS, iris_qsort);
            } else {
                iris_samples[trial] = time_qsort(QSORT_ITERATIONS, iris_qsort);
                host_samples[trial] = time_qsort(QSORT_ITERATIONS, qsort);
            }
        }
        double host_median = median(host_samples);
        double iris_median = median(iris_samples);
        printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", "qsort", (size_t)256,
            host_median, iris_median, iris_median > 0.0 ? host_median / iris_median : 0.0);
    }

    char line_path[96];
    snprintf(line_path, sizeof(line_path), "/tmp/iris-getline-bench-%ld",
        (long)getpid());
    FILE *line_output = fopen(line_path, "w");
    if (line_output == NULL) {
        perror("open getline benchmark input");
        return 1;
    }
    char benchmark_line[2049];
    memset(benchmark_line, 'x', sizeof(benchmark_line) - 1);
    benchmark_line[sizeof(benchmark_line) - 1] = '\n';
    for (size_t index = 0; index < GETLINE_ITERATIONS; index++) {
        if (fwrite(benchmark_line, sizeof(benchmark_line), 1,
                line_output) != 1) {
            fclose(line_output);
            unlink(line_path);
            return 1;
        }
    }
    if (fclose(line_output) != 0) {
        unlink(line_path);
        return 1;
    }
    host_line_stream = fopen(line_path, "r");
    iris_line_stream = iris_fopen(line_path, "r");
    if (host_line_stream == NULL || iris_line_stream == NULL) {
        if (host_line_stream != NULL) fclose(host_line_stream);
        if (iris_line_stream != NULL) iris_fclose(iris_line_stream);
        unlink(line_path);
        perror("open getline benchmark reader");
        return 1;
    }
    report_benchmark("getline", GETLINE_ITERATIONS,
        time_host_getline, time_iris_getline);
    fclose(host_line_stream);
    iris_fclose(iris_line_stream);
    unlink(line_path);

    report_benchmark("sscanf", STDLIB_ITERATIONS,
        time_host_sscanf, time_iris_sscanf);
    report_benchmark("vsscanf", STDLIB_ITERATIONS,
        time_host_vsscanf, time_iris_vsscanf);
    report_benchmark("realloc", STDLIB_ITERATIONS,
        time_host_realloc, time_iris_realloc);
    report_benchmark("abs", STDLIB_ITERATIONS, time_host_abs, time_iris_abs);
    report_benchmark("labs", STDLIB_ITERATIONS, time_host_labs, time_iris_labs);
    report_benchmark("atoi", STDLIB_ITERATIONS, time_host_atoi, time_iris_atoi);
    report_benchmark("strtoul", STDLIB_ITERATIONS,
        time_host_strtoul, time_iris_strtoul);
    report_benchmark("strtod", STDLIB_ITERATIONS,
        time_host_strtod, time_iris_strtod);
    report_benchmark("getenv", STDLIB_ITERATIONS,
        time_host_getenv, time_iris_getenv);
    report_benchmark("setlocale", STDLIB_ITERATIONS,
        time_host_setlocale, time_iris_setlocale);
    report_benchmark("localeconv", STDLIB_ITERATIONS,
        time_host_localeconv, time_iris_localeconv);
    puts("system(NULL) capability query; host and Iris shell availability may differ");
    report_benchmark("system(NULL)", SYSTEM_QUERY_ITERATIONS,
        time_host_system_query, time_iris_system_query);

    int saved_stderr = dup(STDERR_FILENO);
    int null_descriptor = open("/dev/null", O_WRONLY);
    if (saved_stderr < 0 || null_descriptor < 0 ||
        dup2(null_descriptor, STDERR_FILENO) < 0) {
        if (saved_stderr >= 0) close(saved_stderr);
        if (null_descriptor >= 0) close(null_descriptor);
        return 1;
    }
    report_benchmark("perror", 2000,
        host_perror_benchmark, iris_perror_benchmark);
    fflush(stderr);
    iris_fflush(NULL);
    dup2(saved_stderr, STDERR_FILENO);
    close(saved_stderr);
    close(null_descriptor);

    report_benchmark("atexit", 1, time_host_atexit, time_iris_atexit);

    struct rlimit core_limit = { 0, 0 };
    (void)setrlimit(RLIMIT_CORE, &core_limit);
    report_benchmark("exit+child", TERMINATION_ITERATIONS,
        time_host_exit, time_iris_exit);
    report_benchmark("_Exit+child", TERMINATION_ITERATIONS,
        time_host__Exit, time_iris__Exit);
    report_benchmark("abort+child", TERMINATION_ITERATIONS,
        time_host_abort, time_iris_abort);
    report_benchmark("_exit+child", TERMINATION_ITERATIONS,
        time_host__exit, time_iris__exit);

    for (size_t function_index = 0;
        function_index < sizeof(ctype_cases) / sizeof(ctype_cases[0]);
        function_index++) {
        double host_samples[TRIALS];
        double iris_samples[TRIALS];
        for (size_t trial = 0; trial < TRIALS; trial++) {
            if ((trial & 1U) == 0) {
                host_samples[trial] = time_ctype(STDLIB_ITERATIONS,
                    ctype_cases[function_index].host_function);
                iris_samples[trial] = time_ctype(STDLIB_ITERATIONS,
                    ctype_cases[function_index].iris_function);
            } else {
                iris_samples[trial] = time_ctype(STDLIB_ITERATIONS,
                    ctype_cases[function_index].iris_function);
                host_samples[trial] = time_ctype(STDLIB_ITERATIONS,
                    ctype_cases[function_index].host_function);
            }
        }
        double host_median = median(host_samples);
        double iris_median = median(iris_samples);
        printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n",
            ctype_cases[function_index].name, (size_t)0, host_median,
            iris_median, iris_median > 0.0 ? host_median / iris_median : 0.0);
    }

    puts("+----------+----------+------------------+------------------+------------------+");
    fprintf(stderr, "stdlib benchmark checksum: %zu\n", benchmark_sink);
    return 0;
}
