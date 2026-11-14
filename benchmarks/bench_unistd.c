#define _POSIX_C_SOURCE 200809L

#include "bench_common.h"

#include <fcntl.h>
#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

#define IO_ITERATIONS 2000U
#define CLOSE_ITERATIONS 128U

extern ssize_t iris_read(int file_descriptor, void *buffer, size_t count);
extern ssize_t iris_write(int file_descriptor, const void *buffer, size_t count);
extern int iris_close(int file_descriptor);
extern pid_t iris_getpid(void);
extern void *iris_mmap(void *address, size_t length, int protection, int flags,
    int file_descriptor, off_t offset);
extern int iris_munmap(void *address, size_t length);

typedef ssize_t (*read_function)(int, void *, size_t);
typedef ssize_t (*write_function)(int, const void *, size_t);
typedef int (*close_function)(int);
typedef pid_t (*getpid_function)(void);
typedef void *(*mmap_function)(void *, size_t, int, int, int, off_t);
typedef int (*munmap_function)(void *, size_t);

typedef union {
    read_function read;
    write_function write;
    close_function close;
    getpid_function getpid;
} io_benchmark_function;

typedef double (*io_timer_function)(size_t, size_t, io_benchmark_function, int);

static volatile size_t benchmark_sink;
static unsigned char io_buffer[4096];

static double time_read(size_t size, size_t iterations,
    io_benchmark_function opaque_function, int file_descriptor)
{
    read_function function = opaque_function.read;
    struct timespec start;
    struct timespec end;
    ssize_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(file_descriptor, io_buffer, size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_write(size_t size, size_t iterations,
    io_benchmark_function opaque_function, int file_descriptor)
{
    write_function function = opaque_function.write;
    struct timespec start;
    struct timespec end;
    ssize_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(file_descriptor, io_buffer, size);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_close(size_t size, size_t iterations,
    io_benchmark_function opaque_function, int unused_file_descriptor)
{
    close_function function = opaque_function.close;
    int file_descriptors[CLOSE_ITERATIONS];
    struct timespec start;
    struct timespec end;

    (void)size;
    (void)unused_file_descriptor;
    for (size_t index = 0; index < iterations; index++) {
        file_descriptors[index] = open("/dev/null", O_RDONLY);
        if (file_descriptors[index] < 0) {
            for (size_t opened = 0; opened < index; opened++) {
                close(file_descriptors[opened]);
            }
            return 0.0;
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        benchmark_sink += (size_t)(function(file_descriptors[index]) + 1);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_getpid(size_t size, size_t iterations,
    io_benchmark_function opaque_function, int unused_file_descriptor)
{
    getpid_function function = opaque_function.getpid;
    struct timespec start;
    struct timespec end;
    pid_t result = 0;

    (void)size;
    (void)unused_file_descriptor;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function();
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_mmap_pair(size_t size, size_t iterations,
    mmap_function map_fn, munmap_function unmap_fn)
{
    struct timespec start;
    struct timespec end;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        void *mapping = map_fn(NULL, size, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping != MAP_FAILED) {
            benchmark_sink += (size_t)unmap_fn(mapping, size);
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static void report_io(const char *label, size_t size, size_t iterations,
    io_timer_function timer, io_benchmark_function system_function,
    io_benchmark_function iris_function, int file_descriptor)
{
    double system_samples[TRIALS];
    double iris_samples[TRIALS];

    for (size_t trial = 0; trial < TRIALS; trial++) {
        if ((trial & 1U) == 0) {
            system_samples[trial] = timer(size, iterations, system_function, file_descriptor);
            iris_samples[trial] = timer(size, iterations, iris_function, file_descriptor);
        } else {
            iris_samples[trial] = timer(size, iterations, iris_function, file_descriptor);
            system_samples[trial] = timer(size, iterations, system_function, file_descriptor);
        }
    }

    double system_median = median(system_samples);
    double iris_median = median(iris_samples);
    printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", label, size,
        system_median, iris_median,
        iris_median > 0.0 ? system_median / iris_median : 0.0);
}

int main(void)
{
    int read_descriptor = open("/dev/zero", O_RDONLY);
    int write_descriptor = open("/dev/null", O_WRONLY);

    if (read_descriptor < 0 || write_descriptor < 0) {
        if (read_descriptor >= 0) close(read_descriptor);
        if (write_descriptor >= 0) close(write_descriptor);
        return 1;
    }

    printf("Linux syscalls vs %s; median of %d trials\n",
        BENCH_LIBC_NAME, TRIALS);
    puts("+----------+----------+------------------+------------------+------------------+");
    printf("| %-8s | %-8s | %-16s | %-16s | %-16s |\n",
        "Function", "Size (B)", "libc ns/op", "Iris ns/op", "speedup");
    puts("+----------+----------+------------------+------------------+------------------+");
    for (size_t size = 1; size <= sizeof(io_buffer); size *= 64) {
        report_io("read", size, IO_ITERATIONS, time_read,
            (io_benchmark_function){ .read = read },
            (io_benchmark_function){ .read = iris_read }, read_descriptor);
        report_io("write", size, IO_ITERATIONS, time_write,
            (io_benchmark_function){ .write = write },
            (io_benchmark_function){ .write = iris_write }, write_descriptor);
    }
    report_io("close", 0, CLOSE_ITERATIONS, time_close,
        (io_benchmark_function){ .close = close },
        (io_benchmark_function){ .close = iris_close }, -1);
    report_io("getpid", 0, IO_ITERATIONS, time_getpid,
        (io_benchmark_function){ .getpid = getpid },
        (io_benchmark_function){ .getpid = iris_getpid }, 0);
    {
        double host_samples[TRIALS];
        double iris_samples[TRIALS];
        for (size_t trial = 0; trial < TRIALS; trial++) {
            if ((trial & 1U) == 0) {
                host_samples[trial] = time_mmap_pair(4096, CLOSE_ITERATIONS, mmap, munmap);
                iris_samples[trial] = time_mmap_pair(4096, CLOSE_ITERATIONS, iris_mmap, iris_munmap);
            } else {
                iris_samples[trial] = time_mmap_pair(4096, CLOSE_ITERATIONS, iris_mmap, iris_munmap);
                host_samples[trial] = time_mmap_pair(4096, CLOSE_ITERATIONS, mmap, munmap);
            }
        }
        double host_median = median(host_samples);
        double iris_median = median(iris_samples);
        printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", "mmap+un", (size_t)4096,
            host_median, iris_median,
            iris_median > 0.0 ? host_median / iris_median : 0.0);
    }
    close(read_descriptor);
    close(write_descriptor);
    puts("+----------+----------+------------------+------------------+------------------+");
    fprintf(stderr, "unistd benchmark checksum: %zu\n", benchmark_sink);
    return 0;
}
