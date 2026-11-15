#define IRIS_TIME_IMPLEMENTATION

#include <errno.h>
#include <elf.h>
#include <fcntl.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/ioctl.h>
#include <time.h>
#include <unistd.h>

#if defined(__linux__) && defined(__x86_64__)
#define IRIS_SYS_READ 0
#define IRIS_SYS_WRITE 1
#define IRIS_SYS_CLOSE 3
#define IRIS_SYS_LSEEK 8
#define IRIS_SYS_GETPID 39
#define IRIS_SYS_IOCTL 16
#define IRIS_SYS_FTRUNCATE 77
#define IRIS_SYS_RT_SIGACTION 13
#define IRIS_SYS_TIME 201
#define IRIS_SYS_EXIT_GROUP 231
#define IRIS_SYS_OPENAT 257

static long linux_syscall0(long number)
{
    long result;

    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(number)
        : "rcx", "r11", "memory");
    return result;
}

static long linux_syscall1(long number, long first)
{
    long result;

    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(number), "D"(first)
        : "rcx", "r11", "memory");
    return result;
}

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

static long linux_syscall4(long number, long first, long second, long third, long fourth)
{
    long result;
    register long fourth_argument __asm__("r10") = fourth;

    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(number), "D"(first), "S"(second), "d"(third), "r"(fourth_argument)
        : "rcx", "r11", "memory");
    return result;
}
#elif defined(__linux__) && defined(__aarch64__)
#define IRIS_SYS_READ 63
#define IRIS_SYS_WRITE 64
#define IRIS_SYS_CLOSE 57
#define IRIS_SYS_LSEEK 62
#define IRIS_SYS_EXIT_GROUP 94
#define IRIS_SYS_GETPID 172
#define IRIS_SYS_IOCTL 29
#define IRIS_SYS_FTRUNCATE 46
#define IRIS_SYS_RT_SIGACTION 134
#define IRIS_SYS_TIME 169
#define IRIS_SYS_OPENAT 56

static long linux_syscall0(long number)
{
    register long result __asm__("x0");
    register long syscall_number __asm__("x8") = number;

    __asm__ volatile("svc 0"
        : "=r"(result)
        : "r"(syscall_number)
        : "memory", "cc");
    return result;
}

static long linux_syscall1(long number, long first)
{
    register long result __asm__("x0") = first;
    register long syscall_number __asm__("x8") = number;

    __asm__ volatile("svc 0"
        : "+r"(result)
        : "r"(syscall_number)
        : "memory", "cc");
    return result;
}

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

static long linux_syscall4(long number, long first, long second, long third, long fourth)
{
    register long result __asm__("x0") = first;
    register long second_argument __asm__("x1") = second;
    register long third_argument __asm__("x2") = third;
    register long fourth_argument __asm__("x3") = fourth;
    register long syscall_number __asm__("x8") = number;

    __asm__ volatile("svc 0"
        : "+r"(result)
        : "r"(second_argument), "r"(third_argument), "r"(fourth_argument),
            "r"(syscall_number)
        : "memory", "cc");
    return result;
}
#else
#error "Iris unistd currently requires Linux on x86-64 or AArch64"
#endif

struct iris_kernel_sigaction {
    sighandler_t handler;
    unsigned long flags;
    void (*restorer)(void);
    unsigned long mask;
};

#if defined(__linux__) && defined(__x86_64__)
__attribute__((naked)) static void iris_signal_restorer(void)
{
    __asm__ volatile("mov $15, %rax\n\tsyscall");
}
#elif defined(__linux__) && defined(__aarch64__)
__attribute__((naked)) static void iris_signal_restorer(void)
{
    __asm__ volatile("mov x8, #139\n\tsvc 0");
}
#endif

typedef time_t (*vdso_time_function)(time_t *timer);

static time_t iris_time_syscall_impl(time_t *timer);
vdso_time_function __iris_time_dispatch = iris_time_syscall_impl;

static int vdso_name_matches(const char *name, const char *target)
{
    while (*name != '\0' && *name == *target) {
        name++;
        target++;
    }
    return *name == *target;
}

static uint32_t vdso_gnu_hash(const char *name)
{
    uint32_t hash = 5381;
    while (*name != '\0') {
        hash = hash * 33U + (unsigned char)*name++;
    }
    return hash;
}

static uint32_t vdso_elf_hash(const char *name)
{
    uint32_t hash = 0;
    uint32_t high_bits;

    while (*name != '\0') {
        hash = (hash << 4) + (unsigned char)*name++;
        high_bits = hash & 0xf0000000U;
        if (high_bits != 0) {
            hash ^= high_bits >> 24;
            hash &= ~high_bits;
        }
    }
    return hash;
}

static Elf64_Sym *vdso_lookup_gnu(uintptr_t table_address,
    Elf64_Sym *symbols, const char *strings)
{
    const uint32_t *table = (const uint32_t *)table_address;
    uint32_t bucket_count = table[0];
    uint32_t symbol_offset = table[1];
    uint32_t bloom_count = table[2];
    if (bucket_count == 0 || bloom_count == 0) {
        return NULL;
    }

    const Elf64_Xword *bloom = (const Elf64_Xword *)(table + 4);
    const uint32_t *buckets = (const uint32_t *)(bloom + bloom_count);
    const uint32_t *chains = buckets + bucket_count;
    uint32_t hash = vdso_gnu_hash("__vdso_time");
    uint32_t symbol_index = buckets[hash % bucket_count];

    for (uint32_t checked = 0; symbol_index >= symbol_offset &&
        checked < 4096; checked++, symbol_index++) {
        uint32_t chain_hash = chains[symbol_index - symbol_offset];
        Elf64_Sym *symbol = &symbols[symbol_index];
        if ((chain_hash | 1U) == (hash | 1U) &&
            vdso_name_matches(strings + symbol->st_name, "__vdso_time")) {
            return symbol;
        }
        if ((chain_hash & 1U) != 0) {
            break;
        }
    }
    return NULL;
}

static Elf64_Sym *vdso_lookup_sysv(uintptr_t table_address,
    Elf64_Sym *symbols, const char *strings)
{
    const uint32_t *table = (const uint32_t *)table_address;
    uint32_t bucket_count = table[0];
    uint32_t symbol_count = table[1];
    if (bucket_count == 0 || symbol_count == 0) {
        return NULL;
    }

    const uint32_t *buckets = table + 2;
    const uint32_t *chains = buckets + bucket_count;
    uint32_t symbol_index = buckets[vdso_elf_hash("__vdso_time") % bucket_count];
    for (uint32_t checked = 0; symbol_index != STN_UNDEF &&
        symbol_index < symbol_count && checked < symbol_count;
        checked++, symbol_index = chains[symbol_index]) {
        if (vdso_name_matches(strings + symbols[symbol_index].st_name,
                "__vdso_time")) {
            return &symbols[symbol_index];
        }
    }
    return NULL;
}

int __iris_init_vdso(uintptr_t image_address)
{
    __iris_time_dispatch = iris_time_syscall_impl;
    if (image_address == 0) {
        return -1;
    }

    const unsigned char *image = (const unsigned char *)image_address;
    const Elf64_Ehdr *header = (const Elf64_Ehdr *)image;
    if (header->e_ident[EI_MAG0] != ELFMAG0 ||
        header->e_ident[EI_MAG1] != ELFMAG1 ||
        header->e_ident[EI_MAG2] != ELFMAG2 ||
        header->e_ident[EI_MAG3] != ELFMAG3 ||
        header->e_ident[EI_CLASS] != ELFCLASS64 ||
        header->e_phentsize != sizeof(Elf64_Phdr)) {
        return -1;
    }

    const Elf64_Phdr *program_headers =
        (const Elf64_Phdr *)(image + header->e_phoff);
    const Elf64_Dyn *dynamic = NULL;
    size_t dynamic_count = 0;
    for (size_t index = 0; index < header->e_phnum; index++) {
        if (program_headers[index].p_type == PT_DYNAMIC) {
            dynamic = (const Elf64_Dyn *)(image + program_headers[index].p_vaddr);
            dynamic_count = program_headers[index].p_memsz / sizeof(*dynamic);
            break;
        }
    }
    if (dynamic == NULL) {
        return -1;
    }

    uintptr_t symbol_table_address = 0;
    uintptr_t string_table_address = 0;
    uintptr_t gnu_hash_address = 0;
    uintptr_t sysv_hash_address = 0;
    for (size_t index = 0; index < dynamic_count; index++) {
        if (dynamic[index].d_tag == DT_NULL) {
            break;
        }
        uintptr_t address = image_address + dynamic[index].d_un.d_ptr;
        switch (dynamic[index].d_tag) {
        case DT_SYMTAB: symbol_table_address = address; break;
        case DT_STRTAB: string_table_address = address; break;
        case DT_GNU_HASH: gnu_hash_address = address; break;
        case DT_HASH: sysv_hash_address = address; break;
        default: break;
        }
    }
    if (symbol_table_address == 0 || string_table_address == 0) {
        return -1;
    }

    Elf64_Sym *symbols = (Elf64_Sym *)symbol_table_address;
    const char *strings = (const char *)string_table_address;
    Elf64_Sym *symbol = gnu_hash_address != 0
        ? vdso_lookup_gnu(gnu_hash_address, symbols, strings) : NULL;
    if (symbol == NULL && sysv_hash_address != 0) {
        symbol = vdso_lookup_sysv(sysv_hash_address, symbols, strings);
    }
    if (symbol == NULL || symbol->st_shndx == SHN_UNDEF) {
        return -1;
    }

    union {
        uintptr_t address;
        vdso_time_function function;
    } resolved = { .address = image_address + symbol->st_value };
    __iris_time_dispatch = resolved.function;
    return 0;
}

__attribute__((noinline, cold))
static long syscall_error(long result)
{
    errno = (int)-result;
    return -1;
}

static inline long syscall_result(long result)
{
    if (__builtin_expect((unsigned long)result >= (unsigned long)-4095L, 0)) {
        return syscall_error(result);
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
    return (int)syscall_result(linux_syscall1(IRIS_SYS_CLOSE, file_descriptor));
}

int open(const char *path, int flags, ...)
{
    mode_t mode = 0;
    if ((flags & O_CREAT) != 0) {
        va_list arguments;
        va_start(arguments, flags);
        mode = (mode_t)va_arg(arguments, int);
        va_end(arguments);
    }
    return (int)syscall_result(linux_syscall4(IRIS_SYS_OPENAT,
        -100, (long)path, flags, mode));
}

off_t lseek(int file_descriptor, off_t offset, int whence)
{
    return (off_t)syscall_result(linux_syscall3(IRIS_SYS_LSEEK,
        file_descriptor, (long)offset, whence));
}

_Noreturn void _exit(int status)
{
    linux_syscall1(IRIS_SYS_EXIT_GROUP, status);
    for (;;) {
    }
}

pid_t getpid(void)
{
    return (pid_t)syscall_result(linux_syscall0(IRIS_SYS_GETPID));
}

int ftruncate(int file_descriptor, off_t length)
{
    return (int)syscall_result(linux_syscall2(IRIS_SYS_FTRUNCATE,
        file_descriptor, (long)length));
}

int ioctl(int file_descriptor, unsigned long request, ...)
{
    va_list arguments;
    va_start(arguments, request);
    void *argument = va_arg(arguments, void *);
    va_end(arguments);
    return (int)syscall_result(linux_syscall3(IRIS_SYS_IOCTL,
        file_descriptor, (long)request, (long)argument));
}

static time_t iris_time_syscall_impl(time_t *timer)
{
    return (time_t)syscall_result(linux_syscall1(IRIS_SYS_TIME,
        (long)timer));
}

time_t __iris_time_syscall(time_t *timer)
{
    return iris_time_syscall_impl(timer);
}

time_t time(time_t *timer)
{
    return __iris_time_dispatch(timer);
}

sighandler_t signal(int signal_number, sighandler_t handler)
{
    struct iris_kernel_sigaction action = { 0 };
    struct iris_kernel_sigaction previous = { 0 };

    action.handler = handler;
    if (handler != SIG_DFL && handler != SIG_IGN) {
        action.flags = 0x14000000UL;
        action.restorer = iris_signal_restorer;
    }
    long result = syscall_result(linux_syscall4(IRIS_SYS_RT_SIGACTION,
        signal_number, (long)&action, (long)&previous, sizeof(unsigned long)));
    return result < 0 ? SIG_ERR : previous.handler;
}