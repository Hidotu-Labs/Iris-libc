#include <errno.h>
#include <unistd.h>

#if defined(__linux__) && defined(__x86_64__)
#define IRIS_SYS_READ 0
#define IRIS_SYS_WRITE 1
#define IRIS_SYS_CLOSE 3

static long linux_syscall3(long number, long first, long second, long third)
{
    long result;

    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(number), "D"(first), "S"(second), "d"(third)
        : "rcx", "r11", "memory");
    return result;
}
#elif defined(__linux__) && defined(__aarch64__)
#define IRIS_SYS_READ 63
#define IRIS_SYS_WRITE 64
#define IRIS_SYS_CLOSE 57

static long linux_syscall3(long number, long first, long second, long third)
{
    register long result __asm__("x0") = first;
    register long second_argument __asm__("x1") = second;
    register long third_argument __asm__("x2") = third;
    register long syscall_number __asm__("x8") = number;

    __asm__ volatile("svc 0"
        : "+r"(result)
        : "r"(second_argument), "r"(third_argument), "r"(syscall_number)
        : "memory", "cc");
    return result;
}
#else
#error "Iris unistd currently requires Linux on x86-64 or AArch64"
#endif

static long syscall_result(long result)
{
    if (result < 0 && result >= -4095) {
        errno = (int)-result;
        return -1;
    }
    return result;
}

ssize_t read(int file_descriptor, void *buffer, size_t count)
{
    return (ssize_t)syscall_result(linux_syscall3(IRIS_SYS_READ,
        file_descriptor, (long)buffer, (long)count));
}

ssize_t write(int file_descriptor, const void *buffer, size_t count)
{
    return (ssize_t)syscall_result(linux_syscall3(IRIS_SYS_WRITE,
        file_descriptor, (long)buffer, (long)count));
}

int close(int file_descriptor)
{
    return (int)syscall_result(linux_syscall3(IRIS_SYS_CLOSE,
        file_descriptor, 0, 0));
}