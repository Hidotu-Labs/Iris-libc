#include <errno.h>
#include <stdint.h>
#include <string.h>

#if defined(__x86_64__)
#include <immintrin.h>
#endif

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