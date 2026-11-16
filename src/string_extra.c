#include <errno.h>
#include <stdint.h>
#include <string.h>

#if defined(__x86_64__)
#include <immintrin.h>

/* A 16-byte load starting here cannot cross a page boundary. */
static inline int load16_in_page(const void *address)
{
    return ((uintptr_t)address & 4095U) <= 4096U - 16U;
}

/* Copies 1..16 bytes using at most two overlapping moves. */
static inline void copy_short(char *destination, const char *source, size_t count)
{
    if (count >= 8) {
        uint64_t head;
        uint64_t tail;

        __builtin_memcpy(&head, source, 8);
        __builtin_memcpy(&tail, source + count - 8, 8);
        __builtin_memcpy(destination, &head, 8);
        __builtin_memcpy(destination + count - 8, &tail, 8);
    } else if (count >= 4) {
        uint32_t head;
        uint32_t tail;

        __builtin_memcpy(&head, source, 4);
        __builtin_memcpy(&tail, source + count - 4, 4);
        __builtin_memcpy(destination, &head, 4);
        __builtin_memcpy(destination + count - 4, &tail, 4);
    } else {
        destination[0] = source[0];
        destination[count / 2] = source[count / 2];
        destination[count - 1] = source[count - 1];
    }
}
#endif

#if !defined(__x86_64__)
__attribute__((aligned(64)))
void *memchr(const void *memory, int character, size_t count)
{
    const unsigned char *bytes = (const unsigned char *)memory;
    unsigned char target = (unsigned char)character;

    for (size_t index = 0; index < count; index++) {
        if (bytes[index] == target) {
            return (void *)(bytes + index);
        }
    }
    return NULL;
}
#endif

/* Length of the string, but never scans past count bytes. */
static size_t bounded_length(const char *string, size_t count)
{
    size_t index = 0;

#if defined(__x86_64__)
    const __m128i zero_vector = _mm_setzero_si128();

    while (count - index >= 16U) {
        if (!load16_in_page(string + index)) {
            if (string[index] == '\0') {
                return index;
            }
            index++;
            continue;
        }
        unsigned int terminators = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)(string + index)), zero_vector));

        if (terminators != 0) {
            return index + (size_t)__builtin_ctz(terminators);
        }
        index += 16U;
    }
#endif
    while (index < count && string[index] != '\0') {
        index++;
    }
    return index;
}

__attribute__((aligned(64)))
char *strcpy(char *restrict destination, const char *restrict source)
{
    char *result = destination;

#if defined(__x86_64__)
    const __m128i zero_vector = _mm_setzero_si128();

    for (;;) {
        if (((uintptr_t)source & 4095U) > 4096U - 32U) {
            char character = *source++;

            *destination++ = character;
            if (character == '\0') {
                return result;
            }
            continue;
        }
        __m128i first = _mm_loadu_si128((const __m128i *)source);
        __m128i second = _mm_loadu_si128((const __m128i *)(source + 16));
        unsigned int first_zero = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(first, zero_vector));

        if (first_zero != 0) {
            copy_short(destination, source, (size_t)__builtin_ctz(first_zero) + 1U);
            return result;
        }
        unsigned int second_zero = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(second, zero_vector));

        if (second_zero != 0) {
            /* The last 16 bytes of the copy overlap the first store; the bytes match. */
            size_t end = (size_t)__builtin_ctz(second_zero) + 1U;

            _mm_storeu_si128((__m128i *)destination, first);
            _mm_storeu_si128((__m128i *)(destination + end),
                _mm_loadu_si128((const __m128i *)(source + end)));
            return result;
        }
        _mm_storeu_si128((__m128i *)destination, first);
        _mm_storeu_si128((__m128i *)(destination + 16), second);
        source += 32;
        destination += 32;
    }
#else
    while ((*destination++ = *source++) != '\0') {
    }
    return result;
#endif
}

/* Copies count bytes; reads and writes stay within [0, count). */
static inline void copy_block(char *destination, const char *source, size_t count)
{
#if defined(__x86_64__)
    if (count < 16U) {
        if (count != 0) {
            copy_short(destination, source, count);
        }
        return;
    }
    size_t index = 0;

    for (; count - index > 16U; index += 16U) {
        _mm_storeu_si128((__m128i *)(destination + index),
            _mm_loadu_si128((const __m128i *)(source + index)));
    }
    /* The last vector overlaps bytes already copied; the copy is identical there. */
    _mm_storeu_si128((__m128i *)(destination + count - 16U),
        _mm_loadu_si128((const __m128i *)(source + count - 16U)));
#else
    memcpy(destination, source, count);
#endif
}

static inline void zero_block(char *destination, size_t count)
{
#if defined(__x86_64__)
    const __m128i zero_vector = _mm_setzero_si128();
    size_t index = 0;

    if (count < 16U) {
        if (count >= 8U) {
            uint64_t zero = 0;

            __builtin_memcpy(destination, &zero, 8);
            __builtin_memcpy(destination + count - 8U, &zero, 8);
        } else if (count >= 4U) {
            uint32_t zero = 0;

            __builtin_memcpy(destination, &zero, 4);
            __builtin_memcpy(destination + count - 4U, &zero, 4);
        } else {
            for (; index < count; index++) {
                destination[index] = '\0';
            }
        }
        return;
    }
    for (; count - index >= 16U; index += 16U) {
        _mm_storeu_si128((__m128i *)(destination + index), zero_vector);
    }
    _mm_storeu_si128((__m128i *)(destination + count - 16U), zero_vector);
#else
    memset(destination, 0, count);
#endif
}

__attribute__((aligned(64)))
char *strncpy(char *restrict destination, const char *restrict source, size_t count)
{
#if defined(__x86_64__)
    /* Strings that end inside the first 32 bytes are finished in registers. */
    if (count >= 32U && ((uintptr_t)source & 4095U) <= 4096U - 32U) {
        const __m128i zero_vector = _mm_setzero_si128();
        __m128i first = _mm_loadu_si128((const __m128i *)source);
        __m128i second = _mm_loadu_si128((const __m128i *)(source + 16));
        unsigned int first_zero = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(first, zero_vector));

        if (first_zero != 0) {
            size_t length = (size_t)__builtin_ctz(first_zero);

            copy_block(destination, source, length);
            zero_block(destination + length, count - length);
            return destination;
        }
        unsigned int second_zero = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(second, zero_vector));

        if (second_zero != 0) {
            /* Bytes past the terminator are overwritten by the zero fill that follows. */
            size_t length = 16U + (size_t)__builtin_ctz(second_zero);

            _mm_storeu_si128((__m128i *)destination, first);
            _mm_storeu_si128((__m128i *)(destination + 16), second);
            zero_block(destination + length, count - length);
            return destination;
        }
    }
#endif
    size_t length = bounded_length(source, count);

    copy_block(destination, source, length);
    zero_block(destination + length, count - length);
    return destination;
}

__attribute__((aligned(64)))
char *strcat(char *restrict destination, const char *restrict source)
{
#if defined(__x86_64__)
    /* Short strings: one vector each for the destination end and the source. */
    if (load16_in_page(destination) && load16_in_page(source)) {
        const __m128i zero_vector = _mm_setzero_si128();
        unsigned int end_zero = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(
            _mm_loadu_si128((const __m128i *)destination), zero_vector));
        unsigned int source_zero = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(
            _mm_loadu_si128((const __m128i *)source), zero_vector));

        if (end_zero != 0 && source_zero != 0 && __builtin_ctz(source_zero) < 16) {
            size_t length = (size_t)__builtin_ctz(source_zero) + 1U;

            copy_short(destination + __builtin_ctz(end_zero), source, length);
            return destination;
        }
    }
#endif
    strcpy(destination + bounded_length(destination, SIZE_MAX), source);
    return destination;
}

__attribute__((aligned(64)))
char *strncat(char *restrict destination, const char *restrict source, size_t count)
{
#if defined(__x86_64__)
    /* Short counts: one vector each for the destination end and the source. */
    if (count <= 16U && load16_in_page(destination) && load16_in_page(source)) {
        const __m128i zero_vector = _mm_setzero_si128();
        unsigned int end_zero = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(
            _mm_loadu_si128((const __m128i *)destination), zero_vector));

        if (end_zero != 0) {
            /* Bit count stops the copy at the count limit when no terminator comes first. */
            unsigned int source_zero = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(
                _mm_loadu_si128((const __m128i *)source), zero_vector)) | (1U << count);
            size_t length = (size_t)__builtin_ctz(source_zero);
            char *end = destination + __builtin_ctz(end_zero);

            if (length != 0) {
                copy_short(end, source, length);
            }
            end[length] = '\0';
            return destination;
        }
    }
#endif
    char *end = destination + bounded_length(destination, SIZE_MAX);
    size_t length = bounded_length(source, count);

    copy_block(end, source, length);
    end[length] = '\0';
    return destination;
}

/* Scalar membership test that needs no length; for deciding the first byte. */
static inline int string_contains_char(const char *characters, char character)
{
    for (; *characters != '\0'; characters++) {
        if (*characters == character) {
            return 1;
        }
    }
    return 0;
}

/* 256-bit membership set; built once per call, then one bit test per byte. */
static void build_char_set(uint64_t set[4], const char *characters)
{
    set[0] = 0;
    set[1] = 0;
    set[2] = 0;
    set[3] = 0;
    for (const unsigned char *character = (const unsigned char *)characters;
        *character != '\0'; character++) {
        set[*character >> 6] |= (uint64_t)1 << (*character & 63U);
    }
}

static inline int char_set_contains(const uint64_t set[4], unsigned char character)
{
    return (int)((set[character >> 6] >> (character & 63U)) & 1U);
}

__attribute__((aligned(64)))
#if !defined(__x86_64__)
char *strrchr(const char *string, int character)
{
    unsigned char target = (unsigned char)character;
    const char *match = NULL;

    for (;;) {
        if ((unsigned char)*string == target) {
            match = string;
        }
        if (*string == '\0') {
            return (char *)match;
        }
        string++;
    }
}
#endif

#if defined(__x86_64__) && (defined(__GNUC__) || defined(__clang__))
#define IRIS_HAVE_SSE42 1
#include <stdatomic.h>

__attribute__((noinline, cold))
static int detect_sse42(void)
{
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;

    __asm__ volatile("cpuid" : "=a"(eax), "=b"(ebx), "=c"(ecx), "=d"(edx) : "a"(1U), "c"(0U));
    return (ecx & (1U << 20)) != 0;
}

static inline int cpu_has_sse42(void)
{
    static atomic_int cached_support = ATOMIC_VAR_INIT(-1);
    int supported = atomic_load_explicit(&cached_support, memory_order_relaxed);

    if (__builtin_expect(supported < 0, 0)) {
        supported = detect_sse42();
        atomic_store_explicit(&cached_support, supported, memory_order_relaxed);
    }
    return supported;
}

/* Only a set that terminates within its first 16 bytes fits one vector. */
static inline int sse42_set_usable(const char *accept)
{
    if (!load16_in_page(accept) || !cpu_has_sse42()) {
        return 0;
    }
    return _mm_movemask_epi8(_mm_cmpeq_epi8(_mm_loadu_si128((const __m128i *)accept),
        _mm_setzero_si128())) != 0;
}

static inline unsigned int chunk_zero_mask(__m128i chunk)
{
    return (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, _mm_setzero_si128()));
}

/* Bit i is set when byte i of chunk is in the set; the set's terminator is ignored. */
__attribute__((target("sse4.2")))
static inline unsigned int sse42_members(__m128i chunk, __m128i set)
{
    __m128i mask = _mm_cmpistrm(set, chunk,
        _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_BIT_MASK);

    return (unsigned int)_mm_cvtsi128_si32(mask) & 0xffffU;
}

__attribute__((target("sse4.2")))
static size_t strspn_sse42(const char *string, const char *accept)
{
    __m128i set = _mm_loadu_si128((const __m128i *)accept);
    size_t index = 0;

    for (;;) {
        if (!load16_in_page(string + index)) {
            if (string[index] == '\0' || !string_contains_char(accept, string[index])) {
                return index;
            }
            index++;
            continue;
        }
        __m128i chunk = _mm_loadu_si128((const __m128i *)(string + index));
        /* The terminator is never a member, so it ends the span too. */
        unsigned int stops = ~sse42_members(chunk, set) & 0xffffU;

        if (stops != 0) {
            return index + (size_t)__builtin_ctz(stops);
        }
        index += 16U;
    }
}

__attribute__((target("sse4.2")))
static size_t strcspn_sse42(const char *string, const char *reject)
{
    __m128i set = _mm_loadu_si128((const __m128i *)reject);
    size_t index = 0;

    for (;;) {
        if (!load16_in_page(string + index)) {
            if (string[index] == '\0' || string_contains_char(reject, string[index])) {
                return index;
            }
            index++;
            continue;
        }
        __m128i chunk = _mm_loadu_si128((const __m128i *)(string + index));
        unsigned int stops = sse42_members(chunk, set) | chunk_zero_mask(chunk);

        if (stops != 0) {
            return index + (size_t)__builtin_ctz(stops);
        }
        index += 16U;
    }
}

__attribute__((target("sse4.2")))
static char *strpbrk_sse42(const char *string, const char *accept)
{
    __m128i set = _mm_loadu_si128((const __m128i *)accept);
    size_t index = 0;

    for (;;) {
        if (!load16_in_page(string + index)) {
            if (string[index] == '\0') {
                return NULL;
            }
            if (string_contains_char(accept, string[index])) {
                return (char *)(string + index);
            }
            index++;
            continue;
        }
        __m128i chunk = _mm_loadu_si128((const __m128i *)(string + index));
        /* Index of the first member of the chunk, or 16 when there is none. */
        unsigned int member = (unsigned int)_mm_cmpistri(set, chunk,
            _SIDD_UBYTE_OPS | _SIDD_CMP_EQUAL_ANY | _SIDD_LEAST_SIGNIFICANT);
        unsigned int terminator = (chunk_zero_mask(chunk) | 0x10000U);
        unsigned int nul = (unsigned int)__builtin_ctz(terminator);

        if (member < nul) {
            return (char *)(string + index + member);
        }
        if (nul < 16U) {
            return NULL;
        }
        index += 16U;
    }
}
#endif

char *strpbrk(const char *string, const char *accept)
{
#if defined(IRIS_HAVE_SSE42)
    if (sse42_set_usable(accept)) {
        return strpbrk_sse42(string, accept);
    }
#endif
    if (string[0] == '\0') {
        return NULL;
    }
    if (string_contains_char(accept, string[0])) {
        return (char *)string;
    }
    uint64_t set[4];

    build_char_set(set, accept);
    for (; *string != '\0'; string++) {
        if (char_set_contains(set, (unsigned char)*string)) {
            return (char *)string;
        }
    }
    return NULL;
}

__attribute__((aligned(64)))
size_t strspn(const char *string, const char *accept)
{
    if (string[0] == '\0' || !string_contains_char(accept, string[0])) {
        return 0;
    }
#if defined(IRIS_HAVE_SSE42)
    if (sse42_set_usable(accept)) {
        return strspn_sse42(string, accept);
    }
#endif
    uint64_t set[4];
    size_t length = 0;

    build_char_set(set, accept);
    while (string[length] != '\0' && char_set_contains(set, (unsigned char)string[length])) {
        length++;
    }
    return length;
}

__attribute__((aligned(64)))
size_t strcspn(const char *string, const char *reject)
{
    if (string[0] == '\0' || string_contains_char(reject, string[0])) {
        return 0;
    }
#if defined(IRIS_HAVE_SSE42)
    if (sse42_set_usable(reject)) {
        return strcspn_sse42(string, reject);
    }
#endif
    uint64_t set[4];
    size_t length = 0;

    build_char_set(set, reject);
    while (string[length] != '\0' && !char_set_contains(set, (unsigned char)string[length])) {
        length++;
    }
    return length;
}

int strcoll(const char *left, const char *right)
{
    return strcmp(left, right);
}

char *strchr(const char *string, int character)
{
    unsigned char target = (unsigned char)character;

#if defined(__x86_64__)
    const __m128i target_vector = _mm_set1_epi8((char)target);
    const __m128i zero_vector = _mm_setzero_si128();

    while ((uintptr_t)string % 16U != 0) {
        if ((unsigned char)*string == target) {
            return (char *)string;
        }
        if (*string == '\0') {
            return NULL;
        }
        string++;
    }

    for (;;) {
        __m128i bytes = _mm_load_si128((const __m128i *)string);
        unsigned int matches = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(bytes, target_vector));
        unsigned int terminators = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(bytes, zero_vector));
        unsigned int candidates = matches | terminators;

        if (candidates != 0) {
            unsigned int first = (unsigned int)__builtin_ctz(candidates);
            return (matches & (1U << first)) != 0 ? (char *)string + first : NULL;
        }
        string += 16;
    }
#else
    for (;;) {
        if ((unsigned char)*string == target) {
            return (char *)string;
        }
        if (*string == '\0') {
            return NULL;
        }
        string++;
    }
#endif
}

char *strstr(const char *string, const char *substring)
{
    if (*substring == '\0') {
        return (char *)string;
    }

    unsigned char first = (unsigned char)*substring;
    for (const char *candidate = string; *candidate != '\0'; candidate++) {
        if ((unsigned char)*candidate != first) {
            continue;
        }

        const char *left = candidate + 1;
        const char *right = substring + 1;
        while (*right != '\0' && *left == *right) {
            left++;
            right++;
        }
        if (*right == '\0') {
            return (char *)candidate;
        }
    }
    return NULL;
}

char *strerror(int error_number)
{
    switch (error_number) {
    case EPERM: return "Operation not permitted";
    case ENOENT: return "No such file or directory";
    case ESRCH: return "No such process";
    case EINTR: return "Interrupted system call";
    case EIO: return "Input/output error";
    case EBADF: return "Bad file descriptor";
    case EAGAIN: return "Resource temporarily unavailable";
    case ENOMEM: return "Cannot allocate memory";
    case EACCES: return "Permission denied";
    case EFAULT: return "Bad address";
    case EBUSY: return "Device or resource busy";
    case EEXIST: return "File exists";
    case ENODEV: return "No such device";
    case ENOTDIR: return "Not a directory";
    case EISDIR: return "Is a directory";
    case EINVAL: return "Invalid argument";
    case EMFILE: return "Too many open files";
    case ENOSPC: return "No space left on device";
    case ESPIPE: return "Illegal seek";
    case EROFS: return "Read-only file system";
    case EPIPE: return "Broken pipe";
    case ERANGE: return "Numerical result out of range";
    case ENOSYS: return "Function not implemented";
    case ENOTEMPTY: return "Directory not empty";
    case ENOTTY: return "Inappropriate ioctl for device";
    default: return "Unknown error";
    }
}