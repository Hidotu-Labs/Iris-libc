#include <stdint.h>
#include <string.h>

#if !defined(__x86_64__)

static size_t load_word(const unsigned char *source)
{
    size_t value;

    __builtin_memcpy(&value, source, sizeof(value));
    return value;
}

static void store_word(unsigned char *destination, size_t value)
{
    __builtin_memcpy(destination, &value, sizeof(value));
}

static int compare_bytes(const unsigned char *left, const unsigned char *right, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (left[index] != right[index]) {
            return (int)left[index] - (int)right[index];
        }
    }

    return 0;
}

void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;

    for (size_t index = 0; index < count; index++) {
        output[index] = input[index];
    }

    return destination;
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;

    if (count == 0 || output == input) {
        return destination;
    }
    if ((uintptr_t)output < (uintptr_t)input) {
        while (count > 0 && (uintptr_t)output % _Alignof(size_t) != 0) {
            *output++ = *input++;
            count--;
        }
        while (count >= sizeof(size_t)) {
            store_word(output, load_word(input));
            output += sizeof(size_t);
            input += sizeof(size_t);
            count -= sizeof(size_t);
        }
        while (count > 0) {
            *output++ = *input++;
            count--;
        }
    } else {
        output += count;
        input += count;
        while (count > 0 && (uintptr_t)output % _Alignof(size_t) != 0) {
            output--;
            input--;
            count--;
            *output = *input;
        }
        while (count >= sizeof(size_t)) {
            output -= sizeof(size_t);
            input -= sizeof(size_t);
            count -= sizeof(size_t);
            store_word(output, load_word(input));
        }
        while (count > 0) {
            output--;
            input--;
            count--;
            *output = *input;
        }
    }

    return destination;
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *output = destination;
    unsigned char byte = (unsigned char)value;

    for (size_t index = 0; index < count; index++) {
        output[index] = byte;
    }

    return destination;
}

int memcmp(const void *left, const void *right, size_t count)
{
    const unsigned char *left_bytes = left;
    const unsigned char *right_bytes = right;
    return compare_bytes(left_bytes, right_bytes, count);
}

#endif
