#include <errno.h>
#include <sys/mman.h>

#if defined(__linux__) && defined(__x86_64__)
#define IRIS_SYS_MMAP 9
#define IRIS_SYS_MPROTECT 10
#define IRIS_SYS_MUNMAP 11

static long linux_syscall2(long number, long first, long second)
{
    long result;

    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(number), "D"(first), "S"(second)
        : "rcx", "r11", "memory");
    return result;
}

static long linux_syscall3(long number, long first, long second, long third)
{
    long result;

    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(number), "D"(first), "S"(second), "d"(third)
        : "rcx", "r11", "memory");
    return result;
}

static long linux_syscall6(long number, long first, long second, long third,
    long fourth, long fifth, long sixth)
{
    long result;
    register long fourth_argument __asm__("r10") = fourth;
    register long fifth_argument __asm__("r8") = fifth;
    register long sixth_argument __asm__("r9") = sixth;

    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(number), "D"(first), "S"(second), "d"(third),
            "r"(fourth_argument), "r"(fifth_argument), "r"(sixth_argument)
        : "rcx", "r11", "memory");
    return result;
}
#elif defined(__linux__) && defined(__aarch64__)
#define IRIS_SYS_MMAP 222
#define IRIS_SYS_MPROTECT 226
#define IRIS_SYS_MUNMAP 215

static long linux_syscall2(long number, long first, long second)
{
    register long result __asm__("x0") = first;
    register long second_argument __asm__("x1") = second;
    register long syscall_number __asm__("x8") = number;

    __asm__ volatile("svc 0"
        : "+r"(result)
        : "r"(second_argument), "r"(syscall_number)
        : "memory", "cc");
    return result;
}

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

static long linux_syscall6(long number, long first, long second, long third,
    long fourth, long fifth, long sixth)
{
    register long result __asm__("x0") = first;
    register long second_argument __asm__("x1") = second;
    register long third_argument __asm__("x2") = third;
    register long fourth_argument __asm__("x3") = fourth;
    register long fifth_argument __asm__("x4") = fifth;
    register long sixth_argument __asm__("x5") = sixth;
    register long syscall_number __asm__("x8") = number;

    __asm__ volatile("svc 0"
        : "+r"(result)
        : "r"(second_argument), "r"(third_argument), "r"(fourth_argument),
            "r"(fifth_argument), "r"(sixth_argument), "r"(syscall_number)
        : "memory", "cc");
    return result;
}
#else
#error "Iris mman currently requires Linux on x86-64 or AArch64"
#endif

__attribute__((noinline, cold))
static long mman_error(long result)
{
    errno = (int)-result;
    return -1;
}

void *mmap(void *address, size_t length, int protection, int flags,
    int file_descriptor, off_t offset)
{
    long result = linux_syscall6(IRIS_SYS_MMAP, (long)address, (long)length,
        protection, flags, file_descriptor, (long)offset);
    if (__builtin_expect((unsigned long)result >= (unsigned long)-4095L, 0)) {
        mman_error(result);
        return MAP_FAILED;
    }
    return (void *)result;
}

int munmap(void *address, size_t length)
{
    long result = linux_syscall2(IRIS_SYS_MUNMAP, (long)address, (long)length);
    if (__builtin_expect((unsigned long)result >= (unsigned long)-4095L, 0)) {
        return (int)mman_error(result);
    }
    return (int)result;
}

int mprotect(void *address, size_t length, int protection)
{
    long result = linux_syscall3(IRIS_SYS_MPROTECT, (long)address, (long)length,
        protection);
    if (__builtin_expect((unsigned long)result >= (unsigned long)-4095L, 0)) {
        return (int)mman_error(result);
    }
    return (int)result;
}
