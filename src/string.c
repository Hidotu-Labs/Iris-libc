#if !defined(__x86_64__)

#include <limits.h>
#include <stdint.h>
#include <string.h>

static size_t load_word(const unsigned char *source)
{
    size_t value;

    __builtin_memcpy(&value, source, sizeof(value));
    return value;
}

static int has_zero_byte(size_t value)
{
    const size_t ones = SIZE_MAX / UCHAR_MAX;
    const size_t high_bits = ones * (UCHAR_MAX / 2U + 1U);

    return ((value - ones) & ~value & high_bits) != 0;
}

static int same_word_alignment(const void *left, const void *right)
{
    return (uintptr_t)left % _Alignof(size_t) == (uintptr_t)right % _Alignof(size_t);
}

static int compare_strings(const unsigned char *left, const unsigned char *right,
    size_t count, int bounded)
{
    if (bounded && count == 0) {
        return 0;
    }

    if (same_word_alignment(left, right)) {
        while ((!bounded || count > 0) && (uintptr_t)left % _Alignof(size_t) != 0) {
            if (*left != *right) {
                return (int)*left - (int)*right;
            }
            if (*left == '\0') {
                return 0;
            }
            left++;
            right++;
            if (bounded) {
                count--;
            }
        }

        while (!bounded || count >= sizeof(size_t)) {
            size_t left_word = load_word(left);
            size_t right_word = load_word(right);

            if (left_word != right_word || has_zero_byte(left_word)) {
                for (size_t index = 0; index < sizeof(size_t); index++) {
                    if (left[index] != right[index]) {
                        return (int)left[index] - (int)right[index];
                    }
                    if (left[index] == '\0') {
                        return 0;
                    }
                }
            }

            left += sizeof(size_t);
            right += sizeof(size_t);
            if (bounded) {
                count -= sizeof(size_t);
            }
        }
    }

    while (!bounded || count > 0) {
        if (*left != *right) {
            return (int)*left - (int)*right;
        }
        if (*left == '\0') {
            return 0;
        }
        left++;
        right++;
        if (bounded) {
            count--;
        }
    }

    return 0;
}

size_t strlen(const char *string)
{
    const unsigned char *start = (const unsigned char *)string;
    const unsigned char *cursor = start;

    while (*cursor != '\0' && (uintptr_t)cursor % _Alignof(size_t) != 0) {
        cursor++;
    }
    while (!has_zero_byte(load_word(cursor))) {
        cursor += sizeof(size_t);
    }
    while (*cursor != '\0') {
        cursor++;
    }

    return (size_t)(cursor - start);
}

int strcmp(const char *left, const char *right)
{
    return compare_strings((const unsigned char *)left, (const unsigned char *)right,
        SIZE_MAX, 0);
}

int strncmp(const char *left, const char *right, size_t count)
{
    return compare_strings((const unsigned char *)left, (const unsigned char *)right,
        count, 1);
}

#endif
