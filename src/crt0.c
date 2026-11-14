#include <stdint.h>
#include <stdio.h>
#include <unistd.h>

extern int main(int argc, char **argv, char **envp);

static int iris_errno = 0;

#if defined(__linux__) && defined(__x86_64__)
struct iris_tcb {
    void *self;
    uintptr_t dtv;
    void *thread_self;
    int multiple_threads;
    int gscope_flag;
    uintptr_t sysinfo;
    uintptr_t stack_guard;
};

static struct iris_tcb iris_initial_tcb;

static void init_tls(void)
{
    iris_initial_tcb.self = &iris_initial_tcb;
    iris_initial_tcb.thread_self = &iris_initial_tcb;
    iris_initial_tcb.stack_guard = (uintptr_t)0x00000aff0a0d00ffULL;
    long result;
    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(158L), "D"(0x1002L), "S"(&iris_initial_tcb)
        : "rcx", "r11", "memory");
    (void)result;
}
#endif

int *__errno_location(void)
{
    return &iris_errno;
}

_Noreturn void __stack_chk_fail(void)
{
    _exit(134);
}

_Noreturn void _start_c(long *stack)
{
#if defined(__linux__) && defined(__x86_64__)
    init_tls();
#endif
    int argc = (int)stack[0];
    char **argv = (char **)(stack + 1);
    char **envp = argv + argc + 1;
    int status = main(argc, argv, envp);

    fflush(NULL);
    _exit(status);
}

#if defined(__linux__) && defined(__x86_64__)
__attribute__((naked)) void _start(void)
{
    __asm__ volatile(
        "xor %ebp, %ebp\n"
        "mov %rsp, %rdi\n"
        "and $-16, %rsp\n"
        "call _start_c\n");
}
#elif defined(__linux__) && defined(__aarch64__)
__attribute__((naked)) void _start(void)
{
    __asm__ volatile(
        "mov x29, #0\n"
        "mov x30, #0\n"
        "mov x0, sp\n"
        "and sp, x0, #-16\n"
        "bl _start_c\n");
}
#else
#error "Iris crt0 currently requires Linux on x86-64 or AArch64"
#endif
