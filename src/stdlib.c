#include <errno.h>
#include <limits.h>
#include <float.h>
#include <stdint.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#define IRIS_PAGE_SIZE 4096U
#define IRIS_SLAB_SIZE (64U * 1024U)
#define IRIS_MIN_CLASS 4U /* 16 bytes */
#define IRIS_MAX_CLASS 15U /* 32768 bytes */
#define IRIS_CLASS_COUNT (IRIS_MAX_CLASS - IRIS_MIN_CLASS + 1U)
#define IRIS_LARGE_THRESHOLD (1U << IRIS_MAX_CLASS)

struct alloc_header {
    size_t capacity;
    size_t tag; /* class index (< IRIS_CLASS_COUNT) or mapped_size (>= IRIS_CLASS_COUNT) */
};

struct free_node {
    struct free_node *next;
};

static struct free_node *free_lists[IRIS_CLASS_COUNT];
static unsigned char *slab_cursor = NULL;
static size_t slab_remaining = 0;
static void (*exit_handlers[32])(void);
static size_t exit_handler_count;
static int exit_handlers_running;

extern char **environ __attribute__((weak));

static inline size_t size_to_class(size_t size)
{
    if (size <= 16U) {
        return 0;
    }
    unsigned int bits = (unsigned int)(sizeof(unsigned long long) * 8U - 1U -
        (unsigned int)__builtin_clzll((unsigned long long)(size - 1U)));
    return (size_t)bits - (IRIS_MIN_CLASS - 1U);
}

static size_t align_page(size_t size)
{
    return (size + IRIS_PAGE_SIZE - 1U) & ~(size_t)(IRIS_PAGE_SIZE - 1U);
}

static void *slab_alloc(size_t total_size)
{
    if (total_size > slab_remaining) {
        void *mapping = mmap(NULL, IRIS_SLAB_SIZE, PROT_READ | PROT_WRITE,
            MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
        if (mapping == MAP_FAILED) {
            return NULL;
        }
        slab_cursor = (unsigned char *)mapping;
        slab_remaining = IRIS_SLAB_SIZE;
    }
    void *block = slab_cursor;
    slab_cursor += total_size;
    slab_remaining -= total_size;
    return block;
}

__attribute__((noinline))
static void *malloc_slow(size_t size)
{
    if (size <= IRIS_LARGE_THRESHOLD) {
        size_t class_index = size_to_class(size);
        size_t capacity = (size_t)1U << (class_index + IRIS_MIN_CLASS);
        struct alloc_header *header = slab_alloc(sizeof(*header) + capacity);
        if (header == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        header->capacity = capacity;
        header->tag = class_index;
        return (void *)(header + 1);
    }

    if (size > SIZE_MAX - sizeof(struct alloc_header) - IRIS_PAGE_SIZE) {
        errno = ENOMEM;
        return NULL;
    }

    size_t mapped_size = align_page(sizeof(struct alloc_header) + size);
    struct alloc_header *header = mmap(NULL, mapped_size,
        PROT_READ | PROT_WRITE, MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (header == MAP_FAILED) {
        errno = ENOMEM;
        return NULL;
    }
    header->capacity = mapped_size - sizeof(*header);
    header->tag = mapped_size;
    return (void *)(header + 1);
}

void *malloc(size_t size)
{
    if (__builtin_expect(size <= IRIS_LARGE_THRESHOLD, 1)) {
        size_t class_index = size_to_class(size);
        struct free_node *node = free_lists[class_index];
        if (__builtin_expect(node != NULL, 1)) {
            free_lists[class_index] = node->next;
            return (void *)node;
        }
    }
    return malloc_slow(size);
}

void free(void *pointer)
{
    if (__builtin_expect(pointer == NULL, 0)) {
        return;
    }
    struct alloc_header *header = ((struct alloc_header *)pointer) - 1;
    if (__builtin_expect(header->tag < IRIS_CLASS_COUNT, 1)) {
        struct free_node *node = (struct free_node *)pointer;
        node->next = free_lists[header->tag];
        free_lists[header->tag] = node;
    } else {
        munmap(header, header->tag);
    }
}

void *calloc(size_t count, size_t size)
{
    if (count != 0 && size > SIZE_MAX / count) {
        errno = ENOMEM;
        return NULL;
    }
    size_t total = count * size;
    if (total <= IRIS_LARGE_THRESHOLD) {
        size_t alloc_size = total == 0 ? 1U : total;
        size_t class_index = size_to_class(alloc_size);
        size_t capacity = (size_t)1U << (class_index + IRIS_MIN_CLASS);
        void *pointer;

        if (free_lists[class_index] != NULL) {
            struct free_node *node = free_lists[class_index];
            free_lists[class_index] = node->next;
            pointer = (void *)node;
            if (total <= 64U) {
                uint64_t *words = (uint64_t *)pointer;
                words[0] = 0;
                words[1] = 0;
                if (total > 16U) {
                    words[2] = 0;
                    words[3] = 0;
                }
                if (total > 32U) {
                    words[4] = 0;
                    words[5] = 0;
                    words[6] = 0;
                    words[7] = 0;
                }
            } else {
                memset(pointer, 0, total);
            }
            return pointer;
        }

        struct alloc_header *header = slab_alloc(sizeof(*header) + capacity);
        if (header == NULL) {
            errno = ENOMEM;
            return NULL;
        }
        header->capacity = capacity;
        header->tag = class_index;
        /* Fresh mmap slab is already zeroed by the kernel. */
        return (void *)(header + 1);
    }

    void *pointer = malloc(total);
    return pointer;
}

void *realloc(void *pointer, size_t size)
{
    if (pointer == NULL) {
        return malloc(size);
    }
    if (size == 0) {
        free(pointer);
        return NULL;
    }
    struct alloc_header *header = ((struct alloc_header *)pointer) - 1;
    if (size <= header->capacity) {
        return pointer;
    }
    void *updated = malloc(size);
    if (updated == NULL) {
        return NULL;
    }
    memcpy(updated, pointer, header->capacity);
    free(pointer);
    return updated;
}

int atexit(void (*function)(void))
{
    if (function == NULL || exit_handler_count ==
        sizeof(exit_handlers) / sizeof(exit_handlers[0])) {
        errno = ENOMEM;
        return -1;
    }
    exit_handlers[exit_handler_count++] = function;
    return 0;
}

_Noreturn void exit(int status)
{
    if (!exit_handlers_running) {
        exit_handlers_running = 1;
        while (exit_handler_count != 0) {
            void (*function)(void) = exit_handlers[--exit_handler_count];
            function();
        }
    }
    fflush(NULL);
    _exit(status);
}

_Noreturn void _Exit(int status)
{
    _exit(status);
}

_Noreturn void abort(void)
{
    _exit(134);
}

char *getenv(const char *name)
{
    if (name == NULL || *name == '\0' || &environ == NULL || environ == NULL) {
        return NULL;
    }
    for (char **entry = environ; *entry != NULL; entry++) {
        /* Most entries stop at the first byte; only matching prefixes are walked. */
        const char *candidate = *entry;
        const char *key = name;

        while (*key != '\0' && *candidate == *key) {
            candidate++;
            key++;
        }
        if (*key == '\0' && *candidate == '=') {
            return (char *)candidate + 1;
        }
    }
    return NULL;
}

int system(const char *command)
{
    if (command == NULL) {
        return 0;
    }
    errno = ENOSYS;
    return -1;
}

int abs(int value)
{
    return value < 0 ? -value : value;
}

long labs(long value)
{
    return value < 0 ? -value : value;
}

static int is_space_char(unsigned char ch)
{
    return ch == ' ' || (ch >= '\t' && ch <= '\r');
}

static int digit_value(unsigned char ch)
{
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'a' && ch <= 'z') {
        return ch - 'a' + 10;
    }
    if (ch >= 'A' && ch <= 'Z') {
        return ch - 'A' + 10;
    }
    return -1;
}

unsigned long strtoul(const char *restrict string, char **restrict end_pointer,
    int base)
{
    const char *cursor = string;
    while (is_space_char((unsigned char)*cursor)) {
        cursor++;
    }

    int negative = 0;
    if (*cursor == '-') {
        negative = 1;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }

    if (base == 0) {
        if (cursor[0] == '0') {
            if ((cursor[1] == 'x' || cursor[1] == 'X') &&
                digit_value((unsigned char)cursor[2]) >= 0 &&
                digit_value((unsigned char)cursor[2]) < 16) {
                base = 16;
                cursor += 2;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X') &&
        digit_value((unsigned char)cursor[2]) >= 0 &&
        digit_value((unsigned char)cursor[2]) < 16) {
        cursor += 2;
    }

    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (end_pointer != NULL) {
            *end_pointer = (char *)string;
        }
        return 0;
    }

    unsigned long cutoff = ULONG_MAX / (unsigned long)base;
    unsigned long cutlim = ULONG_MAX % (unsigned long)base;
    unsigned long value = 0;
    int any = 0;
    int overflow = 0;

    for (;;) {
        int digit = digit_value((unsigned char)*cursor);
        if (digit < 0 || digit >= base) {
            break;
        }
        any = 1;
        if (value > cutoff || (value == cutoff && (unsigned long)digit > cutlim)) {
            overflow = 1;
        } else {
            value = value * (unsigned long)base + (unsigned long)digit;
        }
        cursor++;
    }

    if (end_pointer != NULL) {
        *end_pointer = (char *)(any ? cursor : string);
    }
    if (!any) {
        return 0;
    }
    if (overflow) {
        errno = ERANGE;
        return ULONG_MAX;
    }
    return negative ? (unsigned long)(-(long)value) : value;
}

long strtol(const char *restrict string, char **restrict end_pointer, int base)
{
    const char *cursor = string;
    while (is_space_char((unsigned char)*cursor)) {
        cursor++;
    }

    int negative = 0;
    if (*cursor == '-') {
        negative = 1;
        cursor++;
    } else if (*cursor == '+') {
        cursor++;
    }

    if (base == 0) {
        if (cursor[0] == '0') {
            if ((cursor[1] == 'x' || cursor[1] == 'X') &&
                digit_value((unsigned char)cursor[2]) >= 0 &&
                digit_value((unsigned char)cursor[2]) < 16) {
                base = 16;
                cursor += 2;
            } else {
                base = 8;
            }
        } else {
            base = 10;
        }
    } else if (base == 16 && cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X') &&
        digit_value((unsigned char)cursor[2]) >= 0 &&
        digit_value((unsigned char)cursor[2]) < 16) {
        cursor += 2;
    }

    if (base < 2 || base > 36) {
        errno = EINVAL;
        if (end_pointer != NULL) {
            *end_pointer = (char *)string;
        }
        return 0;
    }

    unsigned long limit = negative
        ? (unsigned long)LONG_MAX + 1UL : (unsigned long)LONG_MAX;
    unsigned long value = 0;
    int any = 0;
    int overflow = 0;

    if (base == 10) {
        unsigned long cutoff = limit / 10UL;
        unsigned long cutlim = limit % 10UL;
        for (;;) {
            unsigned int digit = (unsigned int)(unsigned char)*cursor - '0';
            if (digit >= 10U) {
                break;
            }
            any = 1;
            if (__builtin_expect(value > cutoff ||
                (value == cutoff && digit > cutlim), 0)) {
                overflow = 1;
            } else {
                value = value * 10UL + digit;
            }
            cursor++;
        }
    } else if (base == 16) {
        unsigned long cutoff = limit >> 4;
        unsigned long cutlim = limit & 15UL;
        for (;;) {
            unsigned char ch = (unsigned char)*cursor;
            unsigned int digit;
            if ((unsigned int)(ch - '0') < 10U) {
                digit = ch - '0';
            } else if ((unsigned int)((ch | 0x20U) - 'a') < 6U) {
                digit = (ch | 0x20U) - 'a' + 10U;
            } else {
                break;
            }
            any = 1;
            if (__builtin_expect(value > cutoff ||
                (value == cutoff && digit > cutlim), 0)) {
                overflow = 1;
            } else {
                value = (value << 4) + digit;
            }
            cursor++;
        }
    } else {
        unsigned long cutoff = limit / (unsigned long)base;
        unsigned long cutlim = limit % (unsigned long)base;
        for (;;) {
            int digit = digit_value((unsigned char)*cursor);
            if (digit < 0 || digit >= base) {
                break;
            }
            any = 1;
            if (value > cutoff || (value == cutoff && (unsigned long)digit > cutlim)) {
                overflow = 1;
            } else {
                value = value * (unsigned long)base + (unsigned long)digit;
            }
            cursor++;
        }
    }

    if (end_pointer != NULL) {
        *end_pointer = (char *)(any ? cursor : string);
    }
    if (!any) {
        return 0;
    }
    if (overflow) {
        errno = ERANGE;
        return negative ? LONG_MIN : LONG_MAX;
    }
    return negative ? (long)(0UL - value) : (long)value;
}

int atoi(const char *string)
{
    return (int)strtol(string, NULL, 10);
}

static int ascii_lower(int character)
{
    if (character >= 'A' && character <= 'Z') {
        return character + ('a' - 'A');
    }
    return character;
}

static int match_ascii_word(const char *string, const char *word)
{
    while (*word != '\0') {
        if (ascii_lower((unsigned char)*string) != *word) {
            return 0;
        }
        string++;
        word++;
    }
    return 1;
}

static int bounded_exponent_add(int left, int right)
{
    if (right > 0 && left > 10000 - right) {
        return 10000;
    }
    if (right < 0 && left < -10000 - right) {
        return -10000;
    }
    return left + right;
}

static int parse_float_exponent(const char **cursor, char marker_lower,
    char marker_upper)
{
    const char *start = *cursor;
    if (*start != marker_lower && *start != marker_upper) {
        return 0;
    }

    const char *position = start + 1;
    int negative = 0;
    if (*position == '-' || *position == '+') {
        negative = *position == '-';
        position++;
    }
    if (*position < '0' || *position > '9') {
        return 0;
    }

    int exponent = 0;
    do {
        int digit = *position++ - '0';
        if (exponent < 10000) {
            exponent = exponent > (10000 - digit) / 10
                ? 10000 : exponent * 10 + digit;
        }
    } while (*position >= '0' && *position <= '9');

    *cursor = position;
    return negative ? -exponent : exponent;
}

double strtod(const char *restrict string, char **restrict end_pointer)
{
    const char *cursor = string;
    while (is_space_char((unsigned char)*cursor)) {
        cursor++;
    }

    int negative = 0;
    if (*cursor == '-' || *cursor == '+') {
        negative = *cursor == '-';
        cursor++;
    }

    if (match_ascii_word(cursor, "infinity")) {
        cursor += 8;
        if (end_pointer != NULL) *end_pointer = (char *)cursor;
        double result = __builtin_huge_val();
        return negative ? -result : result;
    }
    if (match_ascii_word(cursor, "inf")) {
        cursor += 3;
        if (end_pointer != NULL) *end_pointer = (char *)cursor;
        double result = __builtin_huge_val();
        return negative ? -result : result;
    }
    if (match_ascii_word(cursor, "nan")) {
        cursor += 3;
        if (*cursor == '(') {
            const char *payload = cursor + 1;
            while ((*payload >= 'a' && *payload <= 'z') ||
                (*payload >= 'A' && *payload <= 'Z') ||
                (*payload >= '0' && *payload <= '9') || *payload == '_') {
                payload++;
            }
            if (*payload == ')') {
                cursor = payload + 1;
            }
        }
        if (end_pointer != NULL) *end_pointer = (char *)cursor;
        double result = __builtin_nan("");
        return negative ? -result : result;
    }

    int hexadecimal = cursor[0] == '0' &&
        (cursor[1] == 'x' || cursor[1] == 'X');
    if (hexadecimal) {
        const char *probe = cursor + 2;
        int has_hex_digit = 0;
        while (digit_value((unsigned char)*probe) >= 0 &&
            digit_value((unsigned char)*probe) < 16) {
            has_hex_digit = 1;
            probe++;
        }
        if (*probe == '.') {
            probe++;
            while (digit_value((unsigned char)*probe) >= 0 &&
                digit_value((unsigned char)*probe) < 16) {
                has_hex_digit = 1;
                probe++;
            }
        }
        hexadecimal = has_hex_digit;
        if (hexadecimal) {
            cursor += 2;
        }
    }

    struct lconv *conventions = localeconv();
    char decimal_point = conventions != NULL && conventions->decimal_point != NULL
        ? conventions->decimal_point[0] : '.';
    long double significand = 0.0L;
    int exponent_adjustment = 0;
    int significant_digits = 0;
    int saw_digit = 0;
    int saw_significant = 0;
    int after_decimal = 0;
    int radix = hexadecimal ? 16 : 10;
    int exponent_step = hexadecimal ? 4 : 1;

    for (;;) {
        if (*cursor == decimal_point && !after_decimal) {
            after_decimal = 1;
            cursor++;
            continue;
        }

        int digit = hexadecimal
            ? digit_value((unsigned char)*cursor)
            : ((*cursor >= '0' && *cursor <= '9') ? *cursor - '0' : -1);
        if (digit < 0 || digit >= radix) {
            break;
        }
        saw_digit = 1;
        if (saw_significant || digit != 0) {
            saw_significant = 1;
            if (significant_digits < 19) {
                significand = significand * (long double)radix + digit;
                significant_digits++;
                if (after_decimal) {
                    exponent_adjustment = bounded_exponent_add(
                        exponent_adjustment, -exponent_step);
                }
            } else if (!after_decimal) {
                exponent_adjustment = bounded_exponent_add(
                    exponent_adjustment, exponent_step);
            }
        } else if (after_decimal) {
            exponent_adjustment = bounded_exponent_add(
                exponent_adjustment, -exponent_step);
        }
        cursor++;
    }

    if (!saw_digit) {
        if (end_pointer != NULL) *end_pointer = (char *)string;
        return 0.0;
    }

    const char *exponent_cursor = cursor;
    int explicit_exponent = parse_float_exponent(&exponent_cursor,
        hexadecimal ? 'p' : 'e', hexadecimal ? 'P' : 'E');
    cursor = exponent_cursor;
    int exponent = bounded_exponent_add(exponent_adjustment, explicit_exponent);
    if (end_pointer != NULL) *end_pointer = (char *)cursor;

    if (significand == 0.0L) {
        return negative ? -0.0 : 0.0;
    }

    int exponent_limit = hexadecimal ? 1200 : 400;
    if (exponent > exponent_limit) {
        errno = ERANGE;
        double result = __builtin_huge_val();
        return negative ? -result : result;
    }
    if (exponent < -exponent_limit) {
        errno = ERANGE;
        return negative ? -0.0 : 0.0;
    }

    long double scaled = significand;
    long double scale = hexadecimal ? 2.0L : 10.0L;
    while (exponent > 0) {
        scaled *= scale;
        exponent--;
    }
    while (exponent < 0) {
        scaled /= scale;
        exponent++;
    }

    if (scaled > (long double)DBL_MAX) {
        errno = ERANGE;
        double result = __builtin_huge_val();
        return negative ? -result : result;
    }
    double result = (double)scaled;
    if (result == 0.0 || result < DBL_MIN) {
        errno = ERANGE;
    }
    return negative ? -result : result;
}

static void swap_bytes(unsigned char *left, unsigned char *right, size_t size)
{
    if (left == right) {
        return;
    }
    while (size >= 8) {
        uint64_t a;
        uint64_t b;
        memcpy(&a, left, 8);
        memcpy(&b, right, 8);
        memcpy(left, &b, 8);
        memcpy(right, &a, 8);
        left += 8;
        right += 8;
        size -= 8;
    }
    while (size-- > 0) {
        unsigned char tmp = *left;
        *left++ = *right;
        *right++ = tmp;
    }
}

static void insertion_sort_4(uint32_t *base, size_t count,
    int (*compare)(const void *, const void *))
{
    for (size_t i = 1; i < count; i++) {
        uint32_t tmp = base[i];
        size_t j = i;
        while (j > 0 && compare(&base[j - 1], &tmp) > 0) {
            base[j] = base[j - 1];
            j--;
        }
        base[j] = tmp;
    }
}

static void merge_sort_4(uint32_t *base, uint32_t *tmp, size_t count,
    int (*compare)(const void *, const void *))
{
    if (count <= 16) {
        insertion_sort_4(base, count, compare);
        return;
    }
    size_t mid = count / 2;
    merge_sort_4(base, tmp, mid, compare);
    merge_sort_4(base + mid, tmp, count - mid, compare);
    if (compare(&base[mid - 1], &base[mid]) <= 0) {
        return;
    }
    memcpy(tmp, base, mid * sizeof(uint32_t));
    size_t i = 0;
    size_t j = mid;
    size_t k = 0;
    while (i < mid && j < count) {
        if (compare(&tmp[i], &base[j]) <= 0) {
            base[k++] = tmp[i++];
        } else {
            base[k++] = base[j++];
        }
    }
    while (i < mid) {
        base[k++] = tmp[i++];
    }
}

static void qsort_range(unsigned char *base, size_t low, size_t high, size_t size,
    int (*compare)(const void *, const void *))
{
    while (low < high) {
        if (high - low < 16) {
            for (size_t i = low + 1; i <= high; i++) {
                size_t j = i;
                while (j > low &&
                    compare(base + (j - 1) * size, base + j * size) > 0) {
                    swap_bytes(base + (j - 1) * size, base + j * size, size);
                    j--;
                }
            }
            return;
        }

        size_t mid = low + (high - low) / 2;
        if (compare(base + mid * size, base + low * size) < 0) {
            swap_bytes(base + mid * size, base + low * size, size);
        }
        if (compare(base + high * size, base + low * size) < 0) {
            swap_bytes(base + high * size, base + low * size, size);
        }
        if (compare(base + mid * size, base + high * size) < 0) {
            swap_bytes(base + mid * size, base + high * size, size);
        }

        unsigned char *pivot = base + high * size;
        size_t i = low;
        size_t j = high;
        for (;;) {
            do {
                i++;
            } while (i < high && compare(base + i * size, pivot) < 0);
            do {
                j--;
            } while (j > low && compare(base + j * size, pivot) > 0);
            if (i >= j) {
                break;
            }
            swap_bytes(base + i * size, base + j * size, size);
        }
        swap_bytes(base + i * size, pivot, size);

        if (i - low < high - i) {
            if (i > low) {
                qsort_range(base, low, i - 1, size, compare);
            }
            low = i + 1;
        } else {
            if (i + 1 < high) {
                qsort_range(base, i + 1, high, size, compare);
            }
            if (i == 0) {
                return;
            }
            high = i - 1;
        }
    }
}

void qsort(void *base, size_t count, size_t size,
    int (*compare)(const void *, const void *))
{
    if (count < 2 || size == 0) {
        return;
    }
    if (size == 4 && ((uintptr_t)base & 3U) == 0 && count <= 1024) {
        uint32_t tmp[512];
        merge_sort_4((uint32_t *)base, tmp, count, compare);
        return;
    }
    qsort_range((unsigned char *)base, 0, count - 1, size, compare);
}
