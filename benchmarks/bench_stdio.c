#define _POSIX_C_SOURCE 200809L

#include <fcntl.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#ifndef BENCH_LIBC_NAME
#define BENCH_LIBC_NAME "host libc"
#endif

#define TRIALS 6
#define FORMAT_ITERATIONS 10000U
#define STREAM_ITERATIONS 10000U
#define CLOSE_ITERATIONS 64U
#define CLOSE_BATCH 8U
#define STREAM_CHUNK 64U

typedef struct iris_FILE iris_FILE;

extern int iris_printf(const char *format, ...);
extern int iris_vprintf(const char *format, va_list arguments);
extern int iris_fprintf(iris_FILE *stream, const char *format, ...);
extern int iris_vfprintf(iris_FILE *stream, const char *format, va_list arguments);
extern int iris_snprintf(char *buffer, size_t size, const char *format, ...);
extern int iris_vsnprintf(char *buffer, size_t size, const char *format, va_list arguments);
extern iris_FILE *iris_fopen(const char *path, const char *mode);
extern int iris_fclose(iris_FILE *stream);
extern int iris_fflush(iris_FILE *stream);
extern size_t iris_fread(void *buffer, size_t size, size_t count, iris_FILE *stream);
extern size_t iris_fwrite(const void *buffer, size_t size, size_t count, iris_FILE *stream);
extern int iris_fgetc(iris_FILE *stream);
extern int iris_fputc(int character, iris_FILE *stream);
extern int iris_feof(iris_FILE *stream);
extern int iris_ferror(iris_FILE *stream);
extern void iris_clearerr(iris_FILE *stream);
extern iris_FILE *iris_stdout;

typedef int (*stdio_operation)(void *context);

struct host_context {
    FILE *stream;
    unsigned char data[STREAM_CHUNK];
    char text[128];
    char output[256];
};

struct iris_context {
    iris_FILE *stream;
    unsigned char data[STREAM_CHUNK];
    char text[128];
    char output[256];
};

static volatile size_t benchmark_sink;

static double elapsed_nanoseconds(struct timespec start, struct timespec end)
{
    double seconds = (double)(end.tv_sec - start.tv_sec);
    double nanoseconds = (double)(end.tv_nsec - start.tv_nsec);

    return seconds * 1000000000.0 + nanoseconds;
}

static double median(double samples[TRIALS])
{
    for (size_t index = 0; index < TRIALS; index++) {
        for (size_t next = index + 1; next < TRIALS; next++) {
            if (samples[next] < samples[index]) {
                double temporary = samples[index];
                samples[index] = samples[next];
                samples[next] = temporary;
            }
        }
    }
    return samples[TRIALS / 2];
}

static double time_calls(size_t iterations, stdio_operation operation,
    void *context, stdio_operation flush)
{
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = operation(context);
    }
    if (flush != NULL) {
        flush(context);
    }
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result + 1);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static void report(const char *label, size_t iterations,
    stdio_operation host_operation, void *host_context, stdio_operation host_flush,
    stdio_operation iris_operation, void *iris_context, stdio_operation iris_flush)
{
    double host_samples[TRIALS];
    double iris_samples[TRIALS];

    for (size_t trial = 0; trial < TRIALS; trial++) {
        if ((trial & 1U) == 0) {
            host_samples[trial] = time_calls(iterations, host_operation,
                host_context, host_flush);
            iris_samples[trial] = time_calls(iterations, iris_operation,
                iris_context, iris_flush);
        } else {
            iris_samples[trial] = time_calls(iterations, iris_operation,
                iris_context, iris_flush);
            host_samples[trial] = time_calls(iterations, host_operation,
                host_context, host_flush);
        }
    }

    double host_median = median(host_samples);
    double iris_median = median(iris_samples);
    printf("| %-16s | %16.2f | %16.2f | %15.2fx |\n", label,
        host_median, iris_median,
        iris_median > 0.0 ? host_median / iris_median : 0.0);
}

static int host_snprintf(void *opaque)
{
    struct host_context *context = opaque;
    return snprintf(context->output, sizeof(context->output),
        "%s:%08d:%#x", context->text, 1234567, 0xabcdeU);
}

static int iris_snprintf_call(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_snprintf(context->output, sizeof(context->output),
        "%s:%08d:%#x", context->text, 1234567, 0xabcdeU);
}

static int host_vsnprintf_call(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

static int iris_vsnprintf_call(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = iris_vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

static int host_vsnprintf_operation(void *opaque)
{
    struct host_context *context = opaque;
    return host_vsnprintf_call(context->output, sizeof(context->output),
        "%s:%08d:%#x", context->text, 1234567, 0xabcdeU);
}

static int iris_vsnprintf_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_vsnprintf_call(context->output, sizeof(context->output),
        "%s:%08d:%#x", context->text, 1234567, 0xabcdeU);
}

static int host_fprintf(void *opaque)
{
    struct host_context *context = opaque;
    return fprintf(context->stream, "%s:%08d:%#x",
        context->text, 1234567, 0xabcdeU);
}

static int iris_fprintf_call(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_fprintf(context->stream, "%s:%08d:%#x",
        context->text, 1234567, 0xabcdeU);
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

static int host_vfprintf_operation(void *opaque)
{
    struct host_context *context = opaque;
    return host_vfprintf_call(context->stream, "%s:%08d:%#x",
        context->text, 1234567, 0xabcdeU);
}

static int iris_vfprintf_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_vfprintf_call(context->stream, "%s:%08d:%#x",
        context->text, 1234567, 0xabcdeU);
}

static int host_fread_operation(void *opaque)
{
    struct host_context *context = opaque;
    return (int)fread(context->data, 1, sizeof(context->data), context->stream);
}

static int iris_fread_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return (int)iris_fread(context->data, 1, sizeof(context->data), context->stream);
}

static int host_fwrite_operation(void *opaque)
{
    struct host_context *context = opaque;
    return (int)fwrite(context->data, 1, sizeof(context->data), context->stream);
}

static int iris_fwrite_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return (int)iris_fwrite(context->data, 1, sizeof(context->data), context->stream);
}

static int host_fgetc_operation(void *opaque)
{
    struct host_context *context = opaque;
    return fgetc(context->stream);
}

static int iris_fgetc_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_fgetc(context->stream);
}

static int host_fputc_operation(void *opaque)
{
    struct host_context *context = opaque;
    return fputc('x', context->stream);
}

static int iris_fputc_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_fputc('x', context->stream);
}

static int host_fflush_operation(void *opaque)
{
    struct host_context *context = opaque;
    return fflush(context->stream);
}

static int iris_fflush_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_fflush(context->stream);
}

static int host_feof_operation(void *opaque)
{
    struct host_context *context = opaque;
    return feof(context->stream);
}

static int iris_feof_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_feof(context->stream);
}

static int host_ferror_operation(void *opaque)
{
    struct host_context *context = opaque;
    return ferror(context->stream);
}

static int iris_ferror_operation(void *opaque)
{
    struct iris_context *context = opaque;
    return iris_ferror(context->stream);
}

static int host_clearerr_operation(void *opaque)
{
    struct host_context *context = opaque;
    clearerr(context->stream);
    return 0;
}

static int iris_clearerr_operation(void *opaque)
{
    struct iris_context *context = opaque;
    iris_clearerr(context->stream);
    return 0;
}

static int host_open_close_operation(void *unused)
{
    (void)unused;
    FILE *stream = fopen("/dev/null", "w");
    return stream != NULL && fclose(stream) == 0 ? 1 : -1;
}

static int iris_open_close_operation(void *unused)
{
    (void)unused;
    iris_FILE *stream = iris_fopen("/dev/null", "w");
    return stream != NULL && iris_fclose(stream) == 0 ? 1 : -1;
}

static int host_vprintf_call(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

static int iris_vprintf_call(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = iris_vprintf(format, arguments);
    va_end(arguments);
    return result;
}

static double time_host_stdout(size_t iterations, int use_vprintf)
{
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = use_vprintf
            ? host_vprintf_call("%s:%08d:%#x", "iris", 1234567, 0xabcdeU)
            : printf("%s:%08d:%#x", "iris", 1234567, 0xabcdeU);
    }
    fflush(stdout);
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result + 1);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static double time_iris_stdout(size_t iterations, int use_vprintf)
{
    struct timespec start;
    struct timespec end;
    int result = 0;

    clock_gettime(CLOCK_MONOTONIC, &start);
    for (size_t index = 0; index < iterations; index++) {
        result = use_vprintf
            ? iris_vprintf_call("%s:%08d:%#x", "iris", 1234567, 0xabcdeU)
            : iris_printf("%s:%08d:%#x", "iris", 1234567, 0xabcdeU);
    }
    iris_fflush(iris_stdout);
    clock_gettime(CLOCK_MONOTONIC, &end);
    benchmark_sink += (size_t)(result + 1);
    return elapsed_nanoseconds(start, end) / (double)iterations;
}

static int report_stdout_functions(int saved_stdout, int null_descriptor)
{
    double host_printf[TRIALS];
    double iris_printf_samples[TRIALS];
    double host_vprintf[TRIALS];
    double iris_vprintf_samples[TRIALS];

    if (fflush(stdout) != 0 || iris_fflush(iris_stdout) != 0 ||
        dup2(null_descriptor, STDOUT_FILENO) < 0) {
        dup2(saved_stdout, STDOUT_FILENO);
        return -1;
    }

    for (size_t trial = 0; trial < TRIALS; trial++) {
        if ((trial & 1U) == 0) {
            host_printf[trial] = time_host_stdout(FORMAT_ITERATIONS, 0);
            iris_printf_samples[trial] = time_iris_stdout(FORMAT_ITERATIONS, 0);
            host_vprintf[trial] = time_host_stdout(FORMAT_ITERATIONS, 1);
            iris_vprintf_samples[trial] = time_iris_stdout(FORMAT_ITERATIONS, 1);
        } else {
            iris_printf_samples[trial] = time_iris_stdout(FORMAT_ITERATIONS, 0);
            host_printf[trial] = time_host_stdout(FORMAT_ITERATIONS, 0);
            iris_vprintf_samples[trial] = time_iris_stdout(FORMAT_ITERATIONS, 1);
            host_vprintf[trial] = time_host_stdout(FORMAT_ITERATIONS, 1);
        }
    }
    fflush(stdout);
    iris_fflush(iris_stdout);
    if (dup2(saved_stdout, STDOUT_FILENO) < 0) {
        return -1;
    }

    puts("printf/vprintf to redirected stdout; buffered output flushed per batch");
    puts("+------------------+------------------+------------------+------------------+");
    printf("| %-16s | %-16s | %-16s | %-16s |\n",
        "Function", "libc ns/op", "Iris ns/op", "speedup");
    puts("+------------------+------------------+------------------+------------------+");
    double host_median = median(host_printf);
    double iris_median = median(iris_printf_samples);
    printf("| %-16s | %16.2f | %16.2f | %15.2fx |\n", "printf",
        host_median, iris_median, host_median / iris_median);
    host_median = median(host_vprintf);
    iris_median = median(iris_vprintf_samples);
    printf("| %-16s | %16.2f | %16.2f | %15.2fx |\n", "vprintf",
        host_median, iris_median, host_median / iris_median);
    puts("+------------------+------------------+------------------+------------------+");
    return 0;
}

static double time_host_fclose(size_t iterations)
{
    double total_nanoseconds = 0.0;
    size_t closed = 0;
    while (closed < iterations) {
        FILE *streams[CLOSE_BATCH];
        size_t batch = iterations - closed;
        if (batch > CLOSE_BATCH) batch = CLOSE_BATCH;
        for (size_t index = 0; index < batch; index++) {
            streams[index] = fopen("/dev/null", "w");
            if (streams[index] == NULL) return 0.0;
        }
        struct timespec start;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t index = 0; index < batch; index++) {
            fclose(streams[index]);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        total_nanoseconds += elapsed_nanoseconds(start, end);
        closed += batch;
    }
    return total_nanoseconds / (double)iterations;
}

static double time_iris_fclose(size_t iterations)
{
    double total_nanoseconds = 0.0;
    size_t closed = 0;
    while (closed < iterations) {
        iris_FILE *streams[CLOSE_BATCH];
        size_t batch = iterations - closed;
        if (batch > CLOSE_BATCH) batch = CLOSE_BATCH;
        for (size_t index = 0; index < batch; index++) {
            streams[index] = iris_fopen("/dev/null", "w");
            if (streams[index] == NULL) return 0.0;
        }
        struct timespec start;
        struct timespec end;
        clock_gettime(CLOCK_MONOTONIC, &start);
        for (size_t index = 0; index < batch; index++) {
            iris_fclose(streams[index]);
        }
        clock_gettime(CLOCK_MONOTONIC, &end);
        total_nanoseconds += elapsed_nanoseconds(start, end);
        closed += batch;
    }
    return total_nanoseconds / (double)iterations;
}

static void report_fclose(void)
{
    double host_samples[TRIALS];
    double iris_samples[TRIALS];
    for (size_t trial = 0; trial < TRIALS; trial++) {
        if ((trial & 1U) == 0) {
            host_samples[trial] = time_host_fclose(CLOSE_ITERATIONS);
            iris_samples[trial] = time_iris_fclose(CLOSE_ITERATIONS);
        } else {
            iris_samples[trial] = time_iris_fclose(CLOSE_ITERATIONS);
            host_samples[trial] = time_host_fclose(CLOSE_ITERATIONS);
        }
    }
    double host_median = median(host_samples);
    double iris_median = median(iris_samples);
    printf("| %-16s | %16.2f | %16.2f | %15.2fx |\n", "fclose",
        host_median, iris_median, host_median / iris_median);
}

int main(void)
{
    struct host_context host_format = { 0 };
    struct iris_context iris_format = { 0 };
    memcpy(host_format.text, "benchmark", 10);
    memcpy(iris_format.text, "benchmark", 10);

    int saved_stdout = dup(STDOUT_FILENO);
    int null_descriptor = open("/dev/null", O_WRONLY);
    if (saved_stdout < 0 || null_descriptor < 0) {
        perror("open stdio benchmark device");
        if (saved_stdout >= 0) close(saved_stdout);
        if (null_descriptor >= 0) close(null_descriptor);
        return 1;
    }
    if (report_stdout_functions(saved_stdout, null_descriptor) != 0) {
        perror("redirect stdio benchmark output");
        close(saved_stdout);
        close(null_descriptor);
        return 1;
    }
    close(saved_stdout);
    close(null_descriptor);

    FILE *host_format_stream = fopen("/dev/null", "w");
    iris_FILE *iris_format_stream = iris_fopen("/dev/null", "w");
    FILE *host_input = fopen("/dev/zero", "r");
    iris_FILE *iris_input = iris_fopen("/dev/zero", "r");
    FILE *host_output = fopen("/dev/null", "w");
    iris_FILE *iris_output = iris_fopen("/dev/null", "w");
    FILE *host_eof = fopen("/dev/null", "r");
    iris_FILE *iris_eof = iris_fopen("/dev/null", "r");
    if (host_format_stream == NULL || iris_format_stream == NULL ||
        host_input == NULL || iris_input == NULL || host_output == NULL ||
        iris_output == NULL || host_eof == NULL || iris_eof == NULL) {
        perror("open stdio benchmark streams");
        return 1;
    }

    struct host_context host_format_output = { .stream = host_format_stream };
    struct iris_context iris_format_output = { .stream = iris_format_stream };
    struct host_context host_reader = { .stream = host_input };
    struct iris_context iris_reader = { .stream = iris_input };
    struct host_context host_writer = { .stream = host_output };
    struct iris_context iris_writer = { .stream = iris_output };
    struct host_context host_eof_context = { .stream = host_eof };
    struct iris_context iris_eof_context = { .stream = iris_eof };
    memset(host_writer.data, 'x', sizeof(host_writer.data));
    memset(iris_writer.data, 'x', sizeof(iris_writer.data));
    fgetc(host_eof);
    iris_fgetc(iris_eof);

    printf("All declared stdio.h functions vs %s; median of %d trials\n",
        BENCH_LIBC_NAME, TRIALS);
    puts("+------------------+------------------+------------------+------------------+");
    printf("| %-16s | %-16s | %-16s | %-16s |\n",
        "Function", "libc ns/op", "Iris ns/op", "speedup");
    puts("+------------------+------------------+------------------+------------------+");
    report("snprintf", FORMAT_ITERATIONS, host_snprintf, &host_format, NULL,
        iris_snprintf_call, &iris_format, NULL);
    report("vsnprintf", FORMAT_ITERATIONS, host_vsnprintf_operation, &host_format,
        NULL, iris_vsnprintf_operation, &iris_format, NULL);
    report("fprintf", FORMAT_ITERATIONS, host_fprintf, &host_format_output,
        (stdio_operation)host_fflush_operation, iris_fprintf_call, &iris_format_output,
        (stdio_operation)iris_fflush_operation);
    report("vfprintf", FORMAT_ITERATIONS, host_vfprintf_operation, &host_format_output,
        (stdio_operation)host_fflush_operation, iris_vfprintf_operation, &iris_format_output,
        (stdio_operation)iris_fflush_operation);
    report("fread", STREAM_ITERATIONS, host_fread_operation, &host_reader, NULL,
        iris_fread_operation, &iris_reader, NULL);
    report("fwrite", STREAM_ITERATIONS, host_fwrite_operation, &host_writer,
        (stdio_operation)host_fflush_operation, iris_fwrite_operation, &iris_writer,
        (stdio_operation)iris_fflush_operation);
    report("fgetc", STREAM_ITERATIONS, host_fgetc_operation, &host_reader, NULL,
        iris_fgetc_operation, &iris_reader, NULL);
    report("fputc", STREAM_ITERATIONS, host_fputc_operation, &host_writer,
        (stdio_operation)host_fflush_operation, iris_fputc_operation, &iris_writer,
        (stdio_operation)iris_fflush_operation);
    report("fflush", STREAM_ITERATIONS, host_fflush_operation, &host_writer, NULL,
        iris_fflush_operation, &iris_writer, NULL);
    report("feof", STREAM_ITERATIONS, host_feof_operation, &host_eof_context, NULL,
        iris_feof_operation, &iris_eof_context, NULL);
    report("ferror", STREAM_ITERATIONS, host_ferror_operation, &host_reader, NULL,
        iris_ferror_operation, &iris_reader, NULL);
    report("clearerr", STREAM_ITERATIONS, host_clearerr_operation, &host_eof_context,
        NULL, iris_clearerr_operation, &iris_eof_context, NULL);
    report("fopen+close", STREAM_ITERATIONS, host_open_close_operation,
        NULL, NULL, iris_open_close_operation, NULL, NULL);
    report_fclose();
    puts("+------------------+------------------+------------------+------------------+");

    fclose(host_format_stream);
    iris_fclose(iris_format_stream);
    fclose(host_input);
    iris_fclose(iris_input);
    fclose(host_output);
    iris_fclose(iris_output);
    fclose(host_eof);
    iris_fclose(iris_eof);
    fprintf(stderr, "stdio benchmark checksum: %zu\n", benchmark_sink);
    return 0;
}