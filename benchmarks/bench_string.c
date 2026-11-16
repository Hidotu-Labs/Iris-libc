#define _POSIX_C_SOURCE 200809L

#include "bench_common.h"

#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>

#define MAX_SIZE (1024U * 1024U)
#define TARGET_BYTES (16U * 1024U * 1024U)
#define STREAM_WRITE_ITERATIONS 8192U
#define FORMATTED_IO_ITERATIONS 20000U

extern void *iris_memcpy(void *restrict destination, const void *restrict source, size_t count);
extern void *iris_memmove(void *destination, const void *source, size_t count);
extern void *iris_memset(void *destination, int value, size_t count);
extern int iris_memcmp(const void *left, const void *right, size_t count);
extern size_t iris_strlen(const char *string);
extern int iris_strcmp(const char *left, const char *right);
extern int iris_strncmp(const char *left, const char *right, size_t count);
extern void *iris_memchr(const void *memory, int character, size_t count);
extern char *iris_strcpy(char *restrict destination, const char *restrict source);
extern char *iris_strncpy(char *restrict destination, const char *restrict source,
    size_t count);
extern char *iris_strcat(char *restrict destination, const char *restrict source);
extern char *iris_strncat(char *restrict destination, const char *restrict source,
    size_t count);
extern int iris_strcoll(const char *left, const char *right);
extern char *iris_strchr(const char *string, int character);
extern char *iris_strrchr(const char *string, int character);
extern char *iris_strpbrk(const char *string, const char *accept);
extern size_t iris_strspn(const char *string, const char *accept);
extern size_t iris_strcspn(const char *string, const char *reject);
extern char *iris_strstr(const char *string, const char *substring);
extern char *iris_strerror(int error_number);
extern ssize_t iris_write(int file_descriptor, const void *buffer, size_t count);
extern int iris_snprintf(char *buffer, size_t size, const char *format, ...);
typedef struct iris_FILE iris_FILE;
extern iris_FILE *iris_fopen(const char *path, const char *mode);
extern int iris_fclose(iris_FILE *stream);
extern int iris_fflush(iris_FILE *stream);
extern int iris_fputc(int character, iris_FILE *stream);
extern int iris_fprintf(iris_FILE *stream, const char *format, ...);
extern int iris_vfprintf(iris_FILE *stream, const char *format, va_list arguments);

typedef void *(*copy_function)(void *, const void *, size_t);
typedef void *(*move_function)(void *, const void *, size_t);
typedef void *(*set_function)(void *, int, size_t);
typedef int (*memory_compare_function)(const void *, const void *, size_t);
typedef void *(*memory_search_function)(const void *, int, size_t);
typedef size_t (*length_function)(const char *);
typedef int (*string_compare_function)(const char *, const char *);
typedef int (*string_ncompare_function)(const char *, const char *, size_t);
typedef char *(*string_copy_function)(char *, const char *);
typedef char *(*string_ncopy_function)(char *, const char *, size_t);
typedef char *(*string_concat_function)(char *, const char *);
typedef char *(*string_nconcat_function)(char *, const char *, size_t);
typedef char *(*character_search_function)(const char *, int);
typedef char *(*substring_search_function)(const char *, const char *);
typedef size_t (*string_span_function)(const char *, const char *);
typedef char *(*error_text_function)(int);
typedef ssize_t (*write_function)(int, const void *, size_t);
typedef int (*snprintf_function)(char *, size_t, const char *, ...);
typedef union {
    copy_function copy;
    move_function move;
    set_function set;
    memory_compare_function memory_compare;
    memory_search_function memory_search;
    length_function length;
    string_compare_function string_compare;
    string_ncompare_function string_ncompare;
    string_copy_function string_copy;
    string_ncopy_function string_ncopy;
    string_concat_function string_concat;
    string_nconcat_function string_nconcat;
    character_search_function character_search;
    substring_search_function substring_search;
    string_span_function string_span;
    error_text_function error_text;
    snprintf_function format;
} benchmark_function;




static unsigned char source_data[MAX_SIZE];
static unsigned char destination_data[MAX_SIZE + 1];
static unsigned char compare_data[MAX_SIZE];
static unsigned char memory_search_data[MAX_SIZE];
static char left_text[MAX_SIZE + 1];
static char right_text[MAX_SIZE + 1];
static volatile size_t benchmark_sink;
static char format_buffer[256];
static char format_text[128];
static char string_destinations[FORMATTED_IO_ITERATIONS][64];

static size_t iterations_for(size_t size)
{
    size_t iterations = TARGET_BYTES / size;

    return iterations < 8 ? 8 : iterations;
}

static void prepare_text(size_t size)
{
    for (size_t index = 0; index < size; index++) {
        char value = (char)('a' + index % 26U);
        left_text[index] = value;
        right_text[index] = value;
    }
    left_text[size] = '\0';
    right_text[size] = '\0';
}

static double time_copy(size_t size, size_t iterations, benchmark_function opaque_function)
{
    copy_function function = opaque_function.copy;
    struct timespec start;
    struct timespec end;
    void *result = NULL;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(destination_data, source_data, size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += ((unsigned char *)result)[size - 1];

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_move(size_t size, size_t iterations, benchmark_function opaque_function)
{
    move_function function = opaque_function.move;
    struct timespec start;
    struct timespec end;
    void *result = NULL;

    for (size_t index = 0; index <= size; index++) {
        destination_data[index] = (unsigned char)index;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(destination_data + 1, destination_data, size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += ((unsigned char *)result)[size - 1];

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_set(size_t size, size_t iterations, benchmark_function opaque_function)
{
    set_function function = opaque_function.set;
    struct timespec start;
    struct timespec end;
    void *result = NULL;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(destination_data, 0x5a, size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += ((unsigned char *)result)[size - 1];

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_memory_compare(size_t size, size_t iterations, benchmark_function opaque_function)
{
    memory_compare_function function = opaque_function.memory_compare;
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(source_data, compare_data, size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result + 1);

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_memory_search(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    memory_search_function function = opaque_function.memory_search;
    struct timespec start;
    struct timespec end;
    void *result = NULL;

    memset(memory_search_data, 'a', size);
    if (size != 0) {
        memory_search_data[size - 1] = 'z';
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(memory_search_data, 'z', size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL
        ? (size_t)((unsigned char *)result - memory_search_data) + 1U : 0U;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_length(size_t size, size_t iterations, benchmark_function opaque_function)
{
    length_function function = opaque_function.length;
    struct timespec start;
    struct timespec end;
    size_t result = 0;

    prepare_text(size);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(left_text);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result;

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_string_compare(size_t size, size_t iterations, benchmark_function opaque_function)
{
    string_compare_function function = opaque_function.string_compare;
    struct timespec start;
    struct timespec end;
    int result = 0;

    prepare_text(size);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(left_text, right_text);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result + 1);

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_string_ncompare(size_t size, size_t iterations, benchmark_function opaque_function)
{
    string_ncompare_function function = opaque_function.string_ncompare;
    struct timespec start;
    struct timespec end;
    int result = 0;

    prepare_text(size);
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(left_text, right_text, size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result + 1);

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_string_copy(size_t size, size_t iterations,
    benchmark_function opaque_function, int bounded)
{
    struct timespec start;
    struct timespec end;
    char *result = NULL;
    const char *source = "Iris libc benchmark string";

    if (bounded) {
        string_ncopy_function function = opaque_function.string_ncopy;
        size_t count = size < 32 ? size : 32;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t index = 0; index < iterations; index++) {
            result = function(string_destinations[index], source, count);
        }
    } else {
        string_copy_function function = opaque_function.string_copy;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t index = 0; index < iterations; index++) {
            result = function(string_destinations[index], source);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL ? (unsigned char)result[0] : 0;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_strcpy(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    return time_string_copy(size, iterations, opaque_function, 0);
}

static double time_strncpy(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    return time_string_copy(size, iterations, opaque_function, 1);
}

static double time_string_concat(size_t iterations,
    benchmark_function opaque_function, int bounded)
{
    struct timespec start;
    struct timespec end;
    char *result = NULL;
    static const char prefix[] = "iris";
    static const char suffix[] = "-libc";

    for (size_t index = 0; index < iterations; index++) {
        memcpy(string_destinations[index], prefix, sizeof(prefix));
    }
    if (bounded) {
        string_nconcat_function function = opaque_function.string_nconcat;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t index = 0; index < iterations; index++) {
            result = function(string_destinations[index], suffix, 3);
        }
    } else {
        string_concat_function function = opaque_function.string_concat;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t index = 0; index < iterations; index++) {
            result = function(string_destinations[index], suffix);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL ? (unsigned char)result[0] : 0;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_strcat(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    (void)size;
    return time_string_concat(iterations, opaque_function, 0);
}

static double time_strncat(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    (void)size;
    return time_string_concat(iterations, opaque_function, 1);
}

static double time_character_search(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    character_search_function function = opaque_function.character_search;
    static const char text[] = "Iris libc Kilo compatibility benchmark";
    struct timespec start;
    struct timespec end;
    char *result = NULL;

    (void)size;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(text, 'b');
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL ? (size_t)(result - text) : 0;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_substring_search(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    substring_search_function function = opaque_function.substring_search;
    static const char text[] = "Iris libc Kilo compatibility benchmark";
    static const char substring[] = "benchmark";
    struct timespec start;
    struct timespec end;
    char *result = NULL;

    (void)size;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(text, substring);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL ? (size_t)(result - text) : 0;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_string_span(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    string_span_function function = opaque_function.string_span;
    static const char text[] = "Iris libc Kilo compatibility benchmark";
    static const char set[] = "Iris cblK";
    struct timespec start;
    struct timespec end;
    size_t result = 0;

    (void)size;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(text, set);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_string_prefix_search(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    substring_search_function function = opaque_function.substring_search;
    static const char text[] = "Iris libc Kilo compatibility benchmark";
    static const char accept[] = "xyzK";
    struct timespec start;
    struct timespec end;
    char *result = NULL;

    (void)size;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(text, accept);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL ? (size_t)(result - text) + 1U : 0U;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_error_text(size_t size, size_t iterations,
    benchmark_function opaque_function)
{
    error_text_function function = opaque_function.error_text;
    struct timespec start;
    struct timespec end;
    char *result = NULL;

    (void)size;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(ENOENT);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL ? (unsigned char)result[0] : 0;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_format(size_t size, size_t iterations, benchmark_function opaque_function)
{
    snprintf_function function = opaque_function.format;
    struct timespec start;
    struct timespec end;
    int result = 0;

    format_text[size] = '\0';
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(format_buffer, sizeof(format_buffer), "%s:%08d:%#x",
            format_text, 1234567, 0xabcdeU);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result + (unsigned char)format_buffer[0];

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_direct_byte_write(size_t iterations, int file_descriptor,
    write_function function)
{
    static const char byte = 'x';
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        benchmark_sink += (size_t)function(file_descriptor, &byte, 1);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_buffered_fputc(size_t iterations, iris_FILE *stream)
{
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        benchmark_sink += (size_t)(iris_fputc('x', stream) + 1);
    }
    iris_fflush(stream);
    clock_gettime(CLOCK_MONOTONIC, &end);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static int host_vfprintf_call(FILE *stream, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int iris_vfprintf_call(iris_FILE *stream, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = iris_vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static double time_host_formatted_output(size_t iterations, FILE *stream,
    int use_vfprintf)
{
    static const char *text = "iris";
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = use_vfprintf
            ? host_vfprintf_call(stream, "%s:%08d:%#x", text, 1234567, 0xabcdeU)
            : fprintf(stream, "%s:%08d:%#x", text, 1234567, 0xabcdeU);
    }
    fflush(stream);
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_iris_formatted_output(size_t iterations, iris_FILE *stream,
    int use_vfprintf)
{
    static const char *text = "iris";
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = use_vfprintf
            ? iris_vfprintf_call(stream, "%s:%08d:%#x", text, 1234567, 0xabcdeU)
            : iris_fprintf(stream, "%s:%08d:%#x", text, 1234567, 0xabcdeU);
    }
    iris_fflush(stream);
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static void report_formatted_stream(const char *label, FILE *host_stream,
    iris_FILE *iris_stream, int use_vfprintf)
{
    double host_samples[TRIALS];
    double iris_samples[TRIALS];

    for (size_t trial = 0; trial < TRIALS; trial++) {
        if ((trial & 1U) == 0) {
            host_samples[trial] = time_host_formatted_output(
                FORMATTED_IO_ITERATIONS, host_stream, use_vfprintf);
            iris_samples[trial] = time_iris_formatted_output(
                FORMATTED_IO_ITERATIONS, iris_stream, use_vfprintf);
        } else {
            iris_samples[trial] = time_iris_formatted_output(
                FORMATTED_IO_ITERATIONS, iris_stream, use_vfprintf);
            host_samples[trial] = time_host_formatted_output(
                FORMATTED_IO_ITERATIONS, host_stream, use_vfprintf);
        }
    }

    double host_median = median(host_samples);
    double iris_median = median(iris_samples);
    printf("| %-8s | %16.2f | %16.2f | %15.2fx |\n", label,
        host_median, iris_median,
        iris_median > 0.0 ? host_median / iris_median : 0.0);
}

#define REPORT(label, size, iterations, timer, member, system_function, iris_function) \
    do { \
        double system_samples[TRIALS]; \
        double iris_samples[TRIALS]; \
        benchmark_function system_call = { .member = system_function }; \
        benchmark_function iris_call = { .member = iris_function }; \
        for (size_t trial = 0; trial < TRIALS; trial++) { \
            if ((trial & 1U) == 0) { \
                system_samples[trial] = timer(size, iterations, system_call); \
                iris_samples[trial] = timer(size, iterations, iris_call); \
            } else { \
                iris_samples[trial] = timer(size, iterations, iris_call); \
                system_samples[trial] = timer(size, iterations, system_call); \
            } \
        } \
        double system_median = median(system_samples); \
        double iris_median = median(iris_samples); \
        printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", #label, size, \
            system_median, iris_median, \
            iris_median > 0.0 ? system_median / iris_median : 0.0); \
    } while (0)

int main(void)
{
    const size_t sizes[] = { 16, 256, 4096, MAX_SIZE };
    const size_t size_count = sizeof(sizes) / sizeof(sizes[0]);

    for (size_t index = 0; index < MAX_SIZE; index++) {
        source_data[index] = (unsigned char)(index * 31U + 7U);
        compare_data[index] = source_data[index];
        memory_search_data[index] = 'a';
        left_text[index] = (char)('a' + index % 26U);
        right_text[index] = left_text[index];
    }

    for (size_t index = 0; index < sizeof(format_text) - 1; index++) {
        format_text[index] = (char)('a' + index % 26U);
    }

    char host_format_result[256];
    char iris_format_result[256];
    const size_t format_sizes[] = { 8, 32, 96 };
    for (size_t index = 0; index < sizeof(format_sizes) / sizeof(format_sizes[0]); index++) {
        size_t size = format_sizes[index];
        format_text[size] = '\0';
        int host_length = snprintf(host_format_result, sizeof(host_format_result),
            "%s:%08d:%#x", format_text, 1234567, 0xabcdeU);
        int iris_length = iris_snprintf(iris_format_result, sizeof(iris_format_result),
            "%s:%08d:%#x", format_text, 1234567, 0xabcdeU);
        if (host_length != iris_length || strcmp(host_format_result, iris_format_result) != 0) {
            fprintf(stderr, "snprintf output mismatch at text size %zu\n", size);
            return 1;
        }
    }

    printf("Iris libc vs %s; median of %d trials\n", BENCH_LIBC_NAME, TRIALS);
    puts("+----------+----------+------------------+------------------+------------------+");
    printf("| %-8s | %8s | %-16s | %-16s | %-16s |\n",
        "Function", "Size (B)", "libc ns/op", "Iris ns/op", "speedup");
    puts("+----------+----------+------------------+------------------+------------------+");

    for (size_t index = 0; index < size_count; index++) {
        size_t size = sizes[index];
        size_t iterations = iterations_for(size);

        REPORT(memcpy, size, iterations, time_copy, copy, memcpy, iris_memcpy);
        REPORT(memmove, size, iterations, time_move, move, memmove, iris_memmove);
        REPORT(memset, size, iterations, time_set, set, memset, iris_memset);
        REPORT(memcmp, size, iterations, time_memory_compare, memory_compare, memcmp, iris_memcmp);
        REPORT(strlen, size, iterations, time_length, length, strlen, iris_strlen);
        REPORT(strcmp, size, iterations, time_string_compare, string_compare, strcmp, iris_strcmp);
        REPORT(strncmp, size, iterations, time_string_ncompare, string_ncompare, strncmp, iris_strncmp);
            REPORT(memchr, size, iterations, time_memory_search, memory_search,
                memchr, iris_memchr);
    }

    REPORT(strchr, 0, FORMATTED_IO_ITERATIONS, time_character_search,
        character_search, strchr, iris_strchr);
    REPORT(strrchr, 0, FORMATTED_IO_ITERATIONS, time_character_search,
        character_search, strrchr, iris_strrchr);
    REPORT(strstr, 0, FORMATTED_IO_ITERATIONS, time_substring_search,
        substring_search, strstr, iris_strstr);
    REPORT(strpbrk, 0, FORMATTED_IO_ITERATIONS, time_string_prefix_search,
        substring_search, strpbrk, iris_strpbrk);
    REPORT(strspn, 0, FORMATTED_IO_ITERATIONS, time_string_span,
        string_span, strspn, iris_strspn);
    REPORT(strcspn, 0, FORMATTED_IO_ITERATIONS, time_string_span,
        string_span, strcspn, iris_strcspn);
    REPORT(strcoll, 32, FORMATTED_IO_ITERATIONS, time_string_compare,
        string_compare, strcoll, iris_strcoll);
    REPORT(strcpy, 0, FORMATTED_IO_ITERATIONS, time_strcpy,
        string_copy, strcpy, iris_strcpy);
    REPORT(strncpy, 32, FORMATTED_IO_ITERATIONS, time_strncpy,
        string_ncopy, strncpy, iris_strncpy);
    REPORT(strcat, 0, FORMATTED_IO_ITERATIONS, time_strcat,
        string_concat, strcat, iris_strcat);
    REPORT(strncat, 3, FORMATTED_IO_ITERATIONS, time_strncat,
        string_nconcat, strncat, iris_strncat);
    REPORT(strerror, 0, FORMATTED_IO_ITERATIONS, time_error_text,
        error_text, strerror, iris_strerror);

    puts("snprintf formatting vs libc; median of 6 trials");
    puts("+----------+----------+------------------+------------------+------------------+");
    printf("| %-8s | %8s | %-16s | %-16s | %-16s |\n",
        "Function", "Text (B)", "libc ns/op", "Iris ns/op", "speedup");
    puts("+----------+----------+------------------+------------------+------------------+");
    for (size_t index = 0; index < sizeof(format_sizes) / sizeof(format_sizes[0]); index++) {
        size_t size = format_sizes[index];
        REPORT(snprintf, size, 50000, time_format, format, snprintf, iris_snprintf);
    }
    puts("+----------+----------+------------------+------------------+------------------+");

    FILE *host_formatted_stream = fopen("/dev/null", "w");
    iris_FILE *iris_formatted_stream = iris_fopen("/dev/null", "w");
    if (host_formatted_stream == NULL || iris_formatted_stream == NULL) {
        perror("open formatted output benchmark stream");
        if (host_formatted_stream != NULL) fclose(host_formatted_stream);
        if (iris_formatted_stream != NULL) iris_fclose(iris_formatted_stream);
        return 1;
    }
    puts("fprintf/vfprintf stream formatting vs libc; median of 6 trials");
    puts("+----------+------------------+------------------+------------------+");
    printf("| %-8s | %-16s | %-16s | %-16s |\n",
        "Function", "libc ns/op", "Iris ns/op", "speedup");
    puts("+----------+------------------+------------------+------------------+");
    report_formatted_stream("fprintf", host_formatted_stream,
        iris_formatted_stream, 0);
    report_formatted_stream("vfprintf", host_formatted_stream,
        iris_formatted_stream, 1);
    puts("+----------+------------------+------------------+------------------+");
    fclose(host_formatted_stream);
    iris_fclose(iris_formatted_stream);

    int direct_write_descriptor = open("/dev/null", O_WRONLY);
    iris_FILE *buffered_stream = iris_fopen("/dev/null", "w");
    if (direct_write_descriptor < 0 || buffered_stream == NULL) {
        perror("open stdio benchmark device");
        if (direct_write_descriptor >= 0) close(direct_write_descriptor);
        if (buffered_stream != NULL) iris_fclose(buffered_stream);
        return 1;
    }

    double direct_samples[TRIALS];
    double buffered_samples[TRIALS];
    for (size_t trial = 0; trial < TRIALS; trial++) {
        if ((trial & 1U) == 0) {
            direct_samples[trial] = time_direct_byte_write(STREAM_WRITE_ITERATIONS,
                direct_write_descriptor, write);
            buffered_samples[trial] = time_buffered_fputc(STREAM_WRITE_ITERATIONS,
                buffered_stream);
        } else {
            buffered_samples[trial] = time_buffered_fputc(STREAM_WRITE_ITERATIONS,
                buffered_stream);
            direct_samples[trial] = time_direct_byte_write(STREAM_WRITE_ITERATIONS,
                direct_write_descriptor, write);
        }
    }
    double direct_median = median(direct_samples);
    double buffered_median = median(buffered_samples);
    printf("Buffered FILE output vs one-byte write calls (%u bytes)\n",
        STREAM_WRITE_ITERATIONS);
    puts("+------------------+------------------+------------------+");
    printf("| %-16s | %-16s | %-16s |\n", "write ns/byte",
        "fputc ns/byte", "speedup");
    puts("+------------------+------------------+------------------+");
    printf("| %16.2f | %16.2f | %15.2fx |\n", direct_median,
        buffered_median, buffered_median > 0.0 ? direct_median / buffered_median : 0.0);
    puts("+------------------+------------------+------------------+");
    iris_fclose(buffered_stream);
    close(direct_write_descriptor);

    fprintf(stderr, "benchmark checksum: %zu\n", benchmark_sink);
    return 0;
}
