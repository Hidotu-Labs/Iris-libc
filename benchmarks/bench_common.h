#ifndef IRIS_BENCH_COMMON_H
#define IRIS_BENCH_COMMON_H

#include <stddef.h>
#include <time.h>

#ifndef BENCH_LIBC_NAME
#define BENCH_LIBC_NAME "host libc"
#endif

#define TRIALS 6

static inline double elapsed_nanoseconds(struct timespec start, struct timespec end)
{
    double seconds = (double)(end.tv_sec - start.tv_sec);
    double nanoseconds = (double)(end.tv_nsec - start.tv_nsec);

    return seconds * 1000000000.0 + nanoseconds;
}

static inline double median(double samples[TRIALS])
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

    if (TRIALS % 2 == 0) {
        return (samples[TRIALS / 2 - 1] + samples[TRIALS / 2]) / 2.0;
    }
    return samples[TRIALS / 2];
}

#endif
