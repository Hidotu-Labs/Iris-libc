#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include "bench_common.h"

#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <stdio.h>
#include <stdint.h>
#include <stdlib.h>
#include <sys/auxv.h>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/syscall.h>
#include <sys/stat.h>
#include <termios.h>
#include <unistd.h>

#define IO_ITERATIONS 2000U
#define TIME_ITERATIONS 1000000U
#define CLOCK_ITERATIONS 100000U
#define CALENDAR_ITERATIONS 100000U
#define CLOSE_ITERATIONS 128U
#define FILE_ITERATIONS 128U

extern ssize_t iris_read(int file_descriptor, void *buffer, size_t count);
extern ssize_t iris_write(int file_descriptor, const void *buffer, size_t count);
extern int iris_close(int file_descriptor);
extern int iris_unlink(const char *path);
extern int iris_rmdir(const char *path);
extern int iris_mkstemp(char *template_path);
extern int iris_rename(const char *old_path, const char *new_path);
extern int iris_open(const char *path, int flags, ...);
extern off_t iris_lseek(int file_descriptor, off_t offset, int whence);
extern int iris_ftruncate(int file_descriptor, off_t length);
extern int iris_ioctl(int file_descriptor, unsigned long request, ...);
extern time_t iris_time(time_t *timer);
extern time_t (*__iris_time_dispatch)(time_t *timer);
extern time_t __iris_time_syscall(time_t *timer);
extern int __iris_init_vdso(uintptr_t image_address);
typedef void (*iris_signal_handler)(int);
extern iris_signal_handler iris_signal(int signal_number,
    iris_signal_handler handler);
extern pid_t iris_getpid(void);
extern void *iris_mmap(void *address, size_t length, int protection, int flags,
    int file_descriptor, off_t offset);
extern int iris_munmap(void *address, size_t length);
extern int iris_mprotect(void *address, size_t length, int protection);
struct iris_termios {
    unsigned int c_iflag;
    unsigned int c_oflag;
    unsigned int c_cflag;
    unsigned int c_lflag;
    unsigned char c_line;
    unsigned char c_cc[19];
};
extern int iris_tcgetattr(int file_descriptor, struct iris_termios *settings);
extern int iris_tcsetattr(int file_descriptor, int action,
    const struct iris_termios *settings);
extern int iris_isatty(int file_descriptor);
extern clock_t iris_clock(void);
extern double iris_difftime(time_t end, time_t beginning);
extern struct tm *iris_gmtime(const time_t *timer);
extern struct tm *iris_localtime(const time_t *timer);
extern struct tm *iris_gmtime_r(const time_t *restrict timer,
    struct tm *restrict result);
extern struct tm *iris_localtime_r(const time_t *restrict timer,
    struct tm *restrict result);
extern time_t iris_mktime(struct tm *time_value);
extern size_t iris_strftime(char *restrict buffer, size_t capacity,
    const char *restrict format, const struct tm *restrict time_value);

typedef ssize_t (*read_function)(int, void *, size_t);
typedef ssize_t (*write_function)(int, const void *, size_t);
typedef int (*close_function)(int);
typedef int (*open_function)(const char *, int, ...);
typedef off_t (*lseek_function)(int, off_t, int);
typedef int (*ftruncate_function)(int, off_t);
typedef int (*ioctl_function)(int, unsigned long, ...);
typedef time_t (*time_function)(time_t *);
typedef int (*mprotect_function)(void *, size_t, int);
typedef int (*isatty_function)(int);
typedef int (*tcgetattr_function)(int, void *);
typedef int (*tcsetattr_function)(int, int, const void *);
typedef void (*signal_handler)(int);
typedef signal_handler (*signal_function)(int, signal_handler);
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
static int benchmark_file_descriptor = -1;

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

static double time_read_error(size_t size, size_t iterations,
    io_benchmark_function opaque_function, int unused_file_descriptor)
{
    read_function function = opaque_function.read;
    struct timespec start;
    struct timespec end;

    (void)unused_file_descriptor;
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        errno = 0;
        ssize_t result = function(-1, io_buffer, size);
        benchmark_sink += (size_t)(result == -1 && errno == EBADF);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);

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

static double time_mprotect_pair(size_t size, size_t iterations,
    mmap_function map_fn, mprotect_function protect_fn,
    munmap_function unmap_fn)
{
    struct timespec start;
    struct timespec end;
    void *mapping = map_fn(NULL, size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        return 0.0;
    }

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        benchmark_sink += (size_t)(protect_fn(mapping, size, PROT_READ) + 1);
        benchmark_sink += (size_t)(protect_fn(mapping, size,
            PROT_READ | PROT_WRITE) + 1);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    unmap_fn(mapping, size);
    return elapsed_nanoseconds(start, end) / (double)(iterations * 2U);
}

static double time_open_close(size_t iterations, open_function open_fn,
    close_function close_fn)
{
    struct timespec start;
    struct timespec end;
    size_t completed = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        int descriptor = open_fn("/dev/null", O_RDONLY);
        if (descriptor >= 0) {
            close_fn(descriptor);
            completed++;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += completed;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_lseek(size_t iterations, lseek_function function,
    int file_descriptor)
{
    struct timespec start;
    struct timespec end;
    off_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function(file_descriptor, 0, SEEK_SET);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_ftruncate(size_t iterations, ftruncate_function function,
    int file_descriptor)
{
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        off_t length = (index & 1U) == 0 ? 4096 : 0;
        result += function(file_descriptor, length);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result + 1);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_ioctl_error(size_t iterations, ioctl_function function)
{
    struct timespec start;
    struct timespec end;
    struct winsize window_size;
    size_t errors = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        errors += function(-1, TIOCGWINSZ, &window_size) == -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += errors;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_time(size_t iterations, time_function function)
{
    struct timespec start;
    struct timespec end;
    time_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function(NULL);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_signal(size_t iterations, signal_function function)
{
    struct timespec start;
    struct timespec end;
    size_t errors = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        errors += function(SIGWINCH, SIG_IGN) == SIG_ERR;
        errors += function(SIGWINCH, SIG_DFL) == SIG_ERR;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += errors;
    return elapsed_nanoseconds(start, end) / (double)(iterations * 2U);
}

static double time_isatty(size_t iterations, isatty_function function)
{
    struct timespec start;
    struct timespec end;
    size_t results = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        results += (size_t)function(-1);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += results;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static int host_tcgetattr_call(int file_descriptor, void *settings)
{
    return tcgetattr(file_descriptor, settings);
}

static int iris_tcgetattr_call(int file_descriptor, void *settings)
{
    return iris_tcgetattr(file_descriptor, settings);
}

static int host_tcsetattr_call(int file_descriptor, int action,
    const void *settings)
{
    return tcsetattr(file_descriptor, action, settings);
}

static int iris_tcsetattr_call(int file_descriptor, int action,
    const void *settings)
{
    return iris_tcsetattr(file_descriptor, action, settings);
}

static double time_tcgetattr(size_t iterations, tcgetattr_function function,
    void *settings)
{
    struct timespec start;
    struct timespec end;
    size_t errors = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        errors += function(-1, settings) == -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += errors;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_tcsetattr(size_t iterations, tcsetattr_function function,
    const void *settings)
{
    struct timespec start;
    struct timespec end;
    size_t errors = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        errors += function(-1, TCSANOW, settings) == -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += errors;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_tcgetattr(size_t iterations);
static double time_iris_tcgetattr(size_t iterations);
static double time_host_tcsetattr(size_t iterations);
static double time_iris_tcsetattr(size_t iterations);

static double time_host_open_close(size_t iterations)
{
    return time_open_close(iterations, open, close);
}

static double time_iris_open_close(size_t iterations)
{
    return time_open_close(iterations, iris_open, iris_close);
}

static double time_host_lseek(size_t iterations)
{
    return time_lseek(iterations, lseek, benchmark_file_descriptor);
}

static double time_iris_lseek(size_t iterations)
{
    return time_lseek(iterations, iris_lseek, benchmark_file_descriptor);
}

static double time_host_ftruncate(size_t iterations)
{
    return time_ftruncate(iterations, ftruncate, benchmark_file_descriptor);
}

static double time_iris_ftruncate(size_t iterations)
{
    return time_ftruncate(iterations, iris_ftruncate, benchmark_file_descriptor);
}

static double time_host_ioctl(size_t iterations)
{
    return time_ioctl_error(iterations, ioctl);
}

static double time_iris_ioctl(size_t iterations)
{
    return time_ioctl_error(iterations, iris_ioctl);
}

static double time_host_time(size_t iterations)
{
    return time_time(iterations, time);
}

static double time_iris_time_inline(size_t iterations)
{
    return time_time(iterations, __iris_time_dispatch);
}

#define DEFINE_DIRECT_TIME_BENCH(name, function) \
    static double time_##name(size_t iterations) \
    { \
        struct timespec start; \
        struct timespec end; \
        time_t result = 0; \
        clock_gettime(CLOCK_MONOTONIC, &start); \
        for (size_t index = 0; index < iterations; index++) { \
            result += function(NULL); \
        } \
        clock_gettime(CLOCK_MONOTONIC, &end); \
        benchmark_sink += (size_t)result; \
        return elapsed_nanoseconds(start, end) / (double)iterations; \
    }

DEFINE_DIRECT_TIME_BENCH(host_time_symbol, time)
DEFINE_DIRECT_TIME_BENCH(iris_time_symbol, iris_time)

#undef DEFINE_DIRECT_TIME_BENCH

static time_t host_time_syscall(time_t *timer)
{
    return (time_t)syscall(SYS_time, timer);
}

static double time_host_time_syscall(size_t iterations)
{
    return time_time(iterations, host_time_syscall);
}

static double time_iris_time_syscall(size_t iterations)
{
    return time_time(iterations, __iris_time_syscall);
}

static double time_clock(size_t iterations, clock_t (*function)(void))
{
    struct timespec start;
    struct timespec end;
    clock_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function();
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_clock(size_t iterations)
{
    return time_clock(iterations, clock);
}

static double time_iris_clock(size_t iterations)
{
    return time_clock(iterations, iris_clock);
}

static double time_difftime(size_t iterations,
    double (*function)(time_t, time_t))
{
    struct timespec start;
    struct timespec end;
    double result = 0.0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function((time_t)1709164800 + (time_t)index, 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_difftime(size_t iterations)
{
    return time_difftime(iterations, difftime);
}

static double time_iris_difftime(size_t iterations)
{
    return time_difftime(iterations, iris_difftime);
}

static double time_broken_time(size_t iterations,
    struct tm *(*function)(const time_t *))
{
    struct timespec start;
    struct timespec end;
    const time_t timestamp = 1709164800;
    struct tm *result = NULL;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = function(&timestamp);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result != NULL ? (size_t)(result->tm_yday + 1) : 0;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_gmtime(size_t iterations)
{
    return time_broken_time(iterations, gmtime);
}

static double time_iris_gmtime(size_t iterations)
{
    return time_broken_time(iterations, iris_gmtime);
}

static double time_host_localtime(size_t iterations)
{
    return time_broken_time(iterations, localtime);
}

static double time_iris_localtime(size_t iterations)
{
    return time_broken_time(iterations, iris_localtime);
}

static double time_reentrant_time(size_t iterations,
    struct tm *(*function)(const time_t *restrict, struct tm *restrict))
{
    struct timespec start;
    struct timespec end;
    const time_t timestamp = 1709164800;
    struct tm result;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        function(&timestamp, &result);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result.tm_yday + 1);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_gmtime_r(size_t iterations)
{
    return time_reentrant_time(iterations, gmtime_r);
}

static double time_iris_gmtime_r(size_t iterations)
{
    return time_reentrant_time(iterations, iris_gmtime_r);
}

static double time_host_localtime_r(size_t iterations)
{
    return time_reentrant_time(iterations, localtime_r);
}

static double time_iris_localtime_r(size_t iterations)
{
    return time_reentrant_time(iterations, iris_localtime_r);
}

static double time_mktime(size_t iterations, time_t (*function)(struct tm *))
{
    struct timespec start;
    struct timespec end;
    struct tm value = {
        .tm_sec = 56, .tm_min = 34, .tm_hour = 12,
        .tm_mday = 29, .tm_mon = 1, .tm_year = 124
    };
    time_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function(&value);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)result;
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_mktime(size_t iterations)
{
    return time_mktime(iterations, mktime);
}

static double time_iris_mktime(size_t iterations)
{
    return time_mktime(iterations, iris_mktime);
}

static double time_strftime(size_t iterations,
    size_t (*function)(char *restrict, size_t, const char *restrict,
        const struct tm *restrict))
{
    struct timespec start;
    struct timespec end;
    const struct tm value = {
        .tm_sec = 56, .tm_min = 34, .tm_hour = 12,
        .tm_mday = 29, .tm_mon = 1, .tm_year = 124,
        .tm_wday = 4, .tm_yday = 59, .tm_isdst = 0
    };
    char buffer[64];
    size_t result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result += function(buffer, sizeof(buffer), "%Y-%m-%d %H:%M:%S", &value);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += result + (unsigned char)buffer[0];
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_strftime(size_t iterations)
{
    return time_strftime(iterations, strftime);
}

static double time_iris_strftime(size_t iterations)
{
    return time_strftime(iterations, iris_strftime);
}

static double time_mkstemp(size_t iterations, int (*function)(char *))
{
    struct timespec start;
    struct timespec end;
    char paths[FILE_ITERATIONS][64];
    int descriptors[FILE_ITERATIONS];

    if (iterations > FILE_ITERATIONS) {
        iterations = FILE_ITERATIONS;
    }
    for (size_t index = 0; index < iterations; index++) {
        snprintf(paths[index], sizeof(paths[index]),
            "/tmp/iris-mkstemp-bench-%ld-%zu-XXXXXX", (long)getpid(), index);
        descriptors[index] = -1;
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        descriptors[index] = function(paths[index]);
        benchmark_sink += (size_t)(descriptors[index] >= 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    for (size_t index = 0; index < iterations; index++) {
        if (descriptors[index] >= 0) {
            close(descriptors[index]);
            unlink(paths[index]);
        }
    }
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_host_mkstemp(size_t iterations)
{
    return time_mkstemp(iterations, mkstemp);
}

static double time_iris_mkstemp(size_t iterations)
{
    return time_mkstemp(iterations, iris_mkstemp);
}

static double time_unlink(size_t iterations, int (*function)(const char *))
{
    struct timespec start;
    struct timespec end;
    char paths[FILE_ITERATIONS][64];
    size_t prepared = 0;

    if (iterations > FILE_ITERATIONS) {
        iterations = FILE_ITERATIONS;
    }
    for (; prepared < iterations; prepared++) {
        snprintf(paths[prepared], sizeof(paths[prepared]),
            "/tmp/iris-unlink-bench-%ld-%zu-XXXXXX", (long)getpid(), prepared);
        int descriptor = mkstemp(paths[prepared]);
        if (descriptor < 0 || close(descriptor) != 0) {
            if (descriptor >= 0) close(descriptor);
            break;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < prepared; index++) {
        benchmark_sink += (size_t)(function(paths[index]) == 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    for (size_t index = 0; index < prepared; index++) {
        unlink(paths[index]);
    }
    return prepared == 0 ? 0.0 :
        elapsed_nanoseconds(start, end) / (double)prepared;
}

static double time_host_unlink(size_t iterations)
{
    return time_unlink(iterations, unlink);
}

static double time_iris_unlink(size_t iterations)
{
    return time_unlink(iterations, iris_unlink);
}

static double time_rmdir(size_t iterations, int (*function)(const char *))
{
    struct timespec start;
    struct timespec end;
    char paths[FILE_ITERATIONS][64];
    size_t prepared = 0;

    if (iterations > FILE_ITERATIONS) {
        iterations = FILE_ITERATIONS;
    }
    for (; prepared < iterations; prepared++) {
        snprintf(paths[prepared], sizeof(paths[prepared]),
            "/tmp/iris-rmdir-bench-%ld-%zu", (long)getpid(), prepared);
        if (mkdir(paths[prepared], 0700) != 0) {
            break;
        }
    }
    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < prepared; index++) {
        benchmark_sink += (size_t)(function(paths[index]) == 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    for (size_t index = 0; index < prepared; index++) {
        rmdir(paths[index]);
    }
    return prepared == 0 ? 0.0 :
        elapsed_nanoseconds(start, end) / (double)prepared;
}

static double time_host_rmdir(size_t iterations)
{
    return time_rmdir(iterations, rmdir);
}

static double time_iris_rmdir(size_t iterations)
{
    return time_rmdir(iterations, iris_rmdir);
}

static double time_rename(size_t iterations, int (*function)(const char *, const char *))
{
    struct timespec start;
    struct timespec end;
    char source[] = "/tmp/iris-rename-bench-XXXXXX";
    char destination[sizeof(source) + 8];
    int descriptor = mkstemp(source);
    if (descriptor < 0) {
        return 0.0;
    }
    close(descriptor);
    snprintf(destination, sizeof(destination), "%s.moved", source);

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        benchmark_sink += (size_t)(function(source, destination) == 0);
        benchmark_sink += (size_t)(function(destination, source) == 0);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    unlink(source);
    unlink(destination);
    return elapsed_nanoseconds(start, end) / (double)(iterations * 2U);
}

static double time_host_rename(size_t iterations)
{
    return time_rename(iterations, rename);
}

static double time_iris_rename(size_t iterations)
{
    return time_rename(iterations, iris_rename);
}

static double time_host_signal(size_t iterations)
{
    return time_signal(iterations, signal);
}

static double time_iris_signal(size_t iterations)
{
    return time_signal(iterations, iris_signal);
}

static double time_host_isatty(size_t iterations)
{
    return time_isatty(iterations, isatty);
}

static double time_iris_isatty(size_t iterations)
{
    return time_isatty(iterations, iris_isatty);
}

static double time_host_mprotect(size_t iterations)
{
    return time_mprotect_pair(4096, iterations, mmap, mprotect, munmap);
}

static double time_iris_mprotect(size_t iterations)
{
    return time_mprotect_pair(4096, iterations, iris_mmap, iris_mprotect,
        iris_munmap);
}

static double time_host_tcgetattr_bench(size_t iterations)
{
    return time_host_tcgetattr(iterations);
}

static double time_iris_tcgetattr_bench(size_t iterations)
{
    return time_iris_tcgetattr(iterations);
}

static double time_host_tcsetattr_bench(size_t iterations)
{
    return time_host_tcsetattr(iterations);
}

static double time_iris_tcsetattr_bench(size_t iterations)
{
    return time_iris_tcsetattr(iterations);
}

static void report_simple(const char *label, size_t iterations,
    double (*host_function)(size_t), double (*iris_function)(size_t))
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
    printf("| %-8s | %8zu | %16.2f | %16.2f | %15.2fx |\n", label,
        (size_t)0, host_median, iris_median,
        iris_median > 0.0 ? host_median / iris_median : 0.0);
}

static double time_host_tcgetattr(size_t iterations)
{
    struct termios settings;
    return time_tcgetattr(iterations, host_tcgetattr_call, &settings);
}

static double time_iris_tcgetattr(size_t iterations)
{
    struct iris_termios settings;
    return time_tcgetattr(iterations, iris_tcgetattr_call, &settings);
}

static double time_host_tcsetattr(size_t iterations)
{
    struct termios settings = { 0 };
    return time_tcsetattr(iterations, host_tcsetattr_call, &settings);
}

static double time_iris_tcsetattr(size_t iterations)
{
    struct iris_termios settings = { 0 };
    return time_tcsetattr(iterations, iris_tcsetattr_call, &settings);
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
    char temporary_path[] = "/tmp/iris-unistd-bench-XXXXXX";
    benchmark_file_descriptor = mkstemp(temporary_path);

    if (read_descriptor < 0 || write_descriptor < 0 ||
        benchmark_file_descriptor < 0) {
        if (read_descriptor >= 0) close(read_descriptor);
        if (write_descriptor >= 0) close(write_descriptor);
        if (benchmark_file_descriptor >= 0) close(benchmark_file_descriptor);
        return 1;
    }
    unlink(temporary_path);
    if (ftruncate(benchmark_file_descriptor, 4096) != 0) {
        close(read_descriptor);
        close(write_descriptor);
        close(benchmark_file_descriptor);
        return 1;
    }

    printf("Linux syscalls vs %s; median of %d trials\n",
        BENCH_LIBC_NAME, TRIALS);
    puts("Calendar caveat: Iris clock is monotonic; Iris localtime/mktime use UTC.");
    if (__iris_init_vdso((uintptr_t)getauxval(AT_SYSINFO_EHDR)) != 0) {
        fprintf(stderr, "VDSO time unavailable; Iris time() will use syscall fallback\n");
    }
    puts("time-libc measures the header fast path; time-symbol measures the ABI wrapper; time-syscall compares direct syscalls");
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
    report_io("readerr", sizeof(io_buffer), IO_ITERATIONS, time_read_error,
        (io_benchmark_function){ .read = read },
        (io_benchmark_function){ .read = iris_read }, -1);
    report_io("close", 0, CLOSE_ITERATIONS, time_close,
        (io_benchmark_function){ .close = close },
        (io_benchmark_function){ .close = iris_close }, -1);
    report_io("getpid", 0, IO_ITERATIONS, time_getpid,
        (io_benchmark_function){ .getpid = getpid },
        (io_benchmark_function){ .getpid = iris_getpid }, 0);
    report_simple("open+close", IO_ITERATIONS,
        time_host_open_close, time_iris_open_close);
    report_simple("mkstemp", FILE_ITERATIONS,
        time_host_mkstemp, time_iris_mkstemp);
    report_simple("unlink", FILE_ITERATIONS,
        time_host_unlink, time_iris_unlink);
    report_simple("rmdir", FILE_ITERATIONS,
        time_host_rmdir, time_iris_rmdir);
    report_simple("rename", FILE_ITERATIONS,
        time_host_rename, time_iris_rename);
    report_simple("lseek", IO_ITERATIONS, time_host_lseek, time_iris_lseek);
    report_simple("ftruncate", IO_ITERATIONS,
        time_host_ftruncate, time_iris_ftruncate);
    report_simple("ioctl-EBADF", IO_ITERATIONS,
        time_host_ioctl, time_iris_ioctl);
    report_simple("time-libc", TIME_ITERATIONS,
        time_host_time, time_iris_time_inline);
    report_simple("time-symbol", TIME_ITERATIONS,
        time_host_time_symbol, time_iris_time_symbol);
    report_simple("time-syscall", TIME_ITERATIONS,
        time_host_time_syscall, time_iris_time_syscall);
    report_simple("clock", CLOCK_ITERATIONS,
        time_host_clock, time_iris_clock);
    report_simple("difftime", TIME_ITERATIONS,
        time_host_difftime, time_iris_difftime);
    report_simple("gmtime", CALENDAR_ITERATIONS,
        time_host_gmtime, time_iris_gmtime);
    report_simple("localtime", CALENDAR_ITERATIONS,
        time_host_localtime, time_iris_localtime);
    report_simple("gmtime_r", CALENDAR_ITERATIONS,
        time_host_gmtime_r, time_iris_gmtime_r);
    report_simple("localtime_r", CALENDAR_ITERATIONS,
        time_host_localtime_r, time_iris_localtime_r);
    report_simple("mktime", CALENDAR_ITERATIONS,
        time_host_mktime, time_iris_mktime);
    report_simple("strftime", CALENDAR_ITERATIONS,
        time_host_strftime, time_iris_strftime);
    report_simple("signal", IO_ITERATIONS, time_host_signal, time_iris_signal);
    report_simple("isatty", IO_ITERATIONS, time_host_isatty, time_iris_isatty);
    report_simple("tcgetattr", IO_ITERATIONS,
        time_host_tcgetattr_bench, time_iris_tcgetattr_bench);
    report_simple("tcsetattr", IO_ITERATIONS,
        time_host_tcsetattr_bench, time_iris_tcsetattr_bench);
    report_simple("mprotect", CLOSE_ITERATIONS,
        time_host_mprotect, time_iris_mprotect);
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
    close(benchmark_file_descriptor);
    puts("+----------+----------+------------------+------------------+------------------+");
    fprintf(stderr, "unistd benchmark checksum: %zu\n", benchmark_sink);
    return 0;
}
