#define _POSIX_C_SOURCE 200809L

#include "bench_common.h"

#include <stdio.h>
#include <stdlib.h>

#define STDLIB_ITERATIONS 20000U
#define QSORT_ITERATIONS 200U

extern void *iris_malloc(size_t size);
extern void *iris_calloc(size_t count, size_t size);
extern void iris_free(void *pointer);
extern long iris_strtol(const char *restrict string, char **restrict end_pointer, int base);
extern void iris_qsort(void *base, size_t count, size_t size,
    int (*compare)(const void *, const void *));

typedef void *(*malloc_function)(size_t);
typedef void *(*calloc_function)(size_t, size_t);
typedef void (*free_function)(void *);
typedef long (*strtol_function)(const char *restrict, char **restrict, int);
typedef void (*qsort_function)(void *, size_t, size_t,
    int (*)(const void *, const void *));

static volatile size_t benchmark_sink;

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

    puts("+----------+----------+------------------+------------------+------------------+");
    fprintf(stderr, "stdlib benchmark checksum: %zu\n", benchmark_sink);
    return 0;
}
