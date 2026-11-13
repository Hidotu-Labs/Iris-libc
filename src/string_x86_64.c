#include <stdint.h>
#include <string.h>

#if defined(__x86_64__)
#include <immintrin.h>
#if defined(__GNUC__) || defined(__clang__)
#include <stdatomic.h>
#define IRIS_HAVE_AVX2 1
#endif
#endif

static int compare_bytes(const unsigned char *left, const unsigned char *right, size_t count)
{
    for (size_t index = 0; index < count; index++) {
        if (left[index] != right[index]) {
            return (int)left[index] - (int)right[index];
        }
    }
    return 0;
}

#if defined(__x86_64__)
static void *copy_sse2(unsigned char *output, const unsigned char *input, size_t count)
{
    size_t index = 0;

    for (; count - index >= 64; index += 64) {
        __m128i first = _mm_loadu_si128((const __m128i *)(input + index));
        __m128i second = _mm_loadu_si128((const __m128i *)(input + index + 16));
        __m128i third = _mm_loadu_si128((const __m128i *)(input + index + 32));
        __m128i fourth = _mm_loadu_si128((const __m128i *)(input + index + 48));
        _mm_storeu_si128((__m128i *)(output + index), first);
        _mm_storeu_si128((__m128i *)(output + index + 16), second);
        _mm_storeu_si128((__m128i *)(output + index + 32), third);
        _mm_storeu_si128((__m128i *)(output + index + 48), fourth);
    }
    for (; count - index >= 16; index += 16) {
        __m128i value = _mm_loadu_si128((const __m128i *)(input + index));
        _mm_storeu_si128((__m128i *)(output + index), value);
    }
    for (; index < count; index++) {
        output[index] = input[index];
    }

    return output;
}

static void *move_sse2(unsigned char *output, const unsigned char *input, size_t count)
{
    if ((uintptr_t)output < (uintptr_t)input) {
        size_t index = 0;

        for (; count - index >= 64; index += 64) {
            __m128i first = _mm_loadu_si128((const __m128i *)(input + index));
            __m128i second = _mm_loadu_si128((const __m128i *)(input + index + 16));
            __m128i third = _mm_loadu_si128((const __m128i *)(input + index + 32));
            __m128i fourth = _mm_loadu_si128((const __m128i *)(input + index + 48));
            _mm_storeu_si128((__m128i *)(output + index), first);
            _mm_storeu_si128((__m128i *)(output + index + 16), second);
            _mm_storeu_si128((__m128i *)(output + index + 32), third);
            _mm_storeu_si128((__m128i *)(output + index + 48), fourth);
        }
        for (; count - index >= 16; index += 16) {
            __m128i value = _mm_loadu_si128((const __m128i *)(input + index));
            _mm_storeu_si128((__m128i *)(output + index), value);
        }
        for (; index < count; index++) {
            output[index] = input[index];
        }
        return output;
    }

    while (count >= 64) {
        count -= 64;
        __m128i first = _mm_loadu_si128((const __m128i *)(input + count));
        __m128i second = _mm_loadu_si128((const __m128i *)(input + count + 16));
        __m128i third = _mm_loadu_si128((const __m128i *)(input + count + 32));
        __m128i fourth = _mm_loadu_si128((const __m128i *)(input + count + 48));
        _mm_storeu_si128((__m128i *)(output + count), first);
        _mm_storeu_si128((__m128i *)(output + count + 16), second);
        _mm_storeu_si128((__m128i *)(output + count + 32), third);
        _mm_storeu_si128((__m128i *)(output + count + 48), fourth);
    }
    while (count >= 16) {
        count -= 16;
        __m128i value = _mm_loadu_si128((const __m128i *)(input + count));
        _mm_storeu_si128((__m128i *)(output + count), value);
    }
    while (count > 0) {
        count--;
        output[count] = input[count];
    }

    return output;
}

static void *set_sse2(void *destination, int value, size_t count)
{
    unsigned char *output = destination;
    __m128i pattern = _mm_set1_epi8((char)value);
    size_t index = 0;

    for (; count - index >= 64; index += 64) {
        _mm_storeu_si128((__m128i *)(output + index), pattern);
        _mm_storeu_si128((__m128i *)(output + index + 16), pattern);
        _mm_storeu_si128((__m128i *)(output + index + 32), pattern);
        _mm_storeu_si128((__m128i *)(output + index + 48), pattern);
    }
    for (; count - index >= 16; index += 16) {
        _mm_storeu_si128((__m128i *)(output + index), pattern);
    }
    for (; index < count; index++) {
        output[index] = (unsigned char)value;
    }

    return destination;
}

static inline __attribute__((always_inline))
void copy_tiny(unsigned char *output, const unsigned char *input, size_t count)
{
    if (count == 16) {
        __m128i chunk = _mm_loadu_si128((const __m128i *)input);

        _mm_storeu_si128((__m128i *)output, chunk);
    } else if (count >= 8) {
        uint64_t first;
        uint64_t last;

        __builtin_memcpy(&first, input, 8);
        __builtin_memcpy(&last, input + count - 8, 8);
        __builtin_memcpy(output, &first, 8);
        __builtin_memcpy(output + count - 8, &last, 8);
    } else if (count >= 4) {
        uint32_t first;
        uint32_t last;

        __builtin_memcpy(&first, input, 4);
        __builtin_memcpy(&last, input + count - 4, 4);
        __builtin_memcpy(output, &first, 4);
        __builtin_memcpy(output + count - 4, &last, 4);
    } else if (count > 0) {
        unsigned char first = input[0];
        unsigned char middle = input[count / 2];
        unsigned char last = input[count - 1];

        output[0] = first;
        output[count / 2] = middle;
        output[count - 1] = last;
    }
}

static inline __attribute__((always_inline))
void set_tiny(unsigned char *output, unsigned char value, size_t count)
{
    if (count == 16) {
        __m128i chunk = _mm_set1_epi8((char)value);

        _mm_storeu_si128((__m128i *)output, chunk);
    } else if (count >= 8) {
        uint64_t pattern = 0x0101010101010101ULL * (uint64_t)value;

        __builtin_memcpy(output, &pattern, 8);
        __builtin_memcpy(output + count - 8, &pattern, 8);
    } else if (count >= 4) {
        uint32_t pattern = 0x01010101U * (uint32_t)value;

        __builtin_memcpy(output, &pattern, 4);
        __builtin_memcpy(output + count - 4, &pattern, 4);
    } else if (count > 0) {
        output[0] = value;
        output[count / 2] = value;
        output[count - 1] = value;
    }
}

static inline __attribute__((always_inline))
void copy_small(unsigned char *output, const unsigned char *input, size_t count)
{
    if (count <= 32) {
        __m128i first = _mm_loadu_si128((const __m128i *)input);
        __m128i last = _mm_loadu_si128((const __m128i *)(input + count - 16));

        _mm_storeu_si128((__m128i *)output, first);
        _mm_storeu_si128((__m128i *)(output + count - 16), last);
    } else {
        __m128i first = _mm_loadu_si128((const __m128i *)input);
        __m128i second = _mm_loadu_si128((const __m128i *)(input + 16));
        __m128i penultimate = _mm_loadu_si128((const __m128i *)(input + count - 32));
        __m128i last = _mm_loadu_si128((const __m128i *)(input + count - 16));

        _mm_storeu_si128((__m128i *)output, first);
        _mm_storeu_si128((__m128i *)(output + 16), second);
        _mm_storeu_si128((__m128i *)(output + count - 32), penultimate);
        _mm_storeu_si128((__m128i *)(output + count - 16), last);
    }
}

static inline __attribute__((always_inline))
void set_small(unsigned char *output, unsigned char value, size_t count)
{
    __m128i pattern = _mm_set1_epi8((char)value);

    if (count <= 32) {
        _mm_storeu_si128((__m128i *)output, pattern);
        _mm_storeu_si128((__m128i *)(output + count - 16), pattern);
    } else {
        _mm_storeu_si128((__m128i *)output, pattern);
        _mm_storeu_si128((__m128i *)(output + 16), pattern);
        _mm_storeu_si128((__m128i *)(output + count - 32), pattern);
        _mm_storeu_si128((__m128i *)(output + count - 16), pattern);
    }
}

static int memcmp_sse2(const unsigned char *left, const unsigned char *right, size_t count)
{
    while (count >= 64) {
        __m128i left0 = _mm_loadu_si128((const __m128i *)(left));
        __m128i left1 = _mm_loadu_si128((const __m128i *)(left + 16));
        __m128i left2 = _mm_loadu_si128((const __m128i *)(left + 32));
        __m128i left3 = _mm_loadu_si128((const __m128i *)(left + 48));
        __m128i right0 = _mm_loadu_si128((const __m128i *)(right));
        __m128i right1 = _mm_loadu_si128((const __m128i *)(right + 16));
        __m128i right2 = _mm_loadu_si128((const __m128i *)(right + 32));
        __m128i right3 = _mm_loadu_si128((const __m128i *)(right + 48));
        uint64_t equal_mask = (uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(left0, right0));
        equal_mask |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(left1, right1)) << 16;
        equal_mask |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(left2, right2)) << 32;
        equal_mask |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(left3, right3)) << 48;

        if (equal_mask != UINT64_MAX) {
            size_t index = (size_t)__builtin_ctzll(~equal_mask);
            return (int)left[index] - (int)right[index];
        }
        left += 64;
        right += 64;
        count -= 64;
    }
    while (count >= 16) {
        __m128i left_chunk = _mm_loadu_si128((const __m128i *)left);
        __m128i right_chunk = _mm_loadu_si128((const __m128i *)right);
        unsigned int equal_mask = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(left_chunk, right_chunk));

        if (equal_mask != 0xffffU) {
            size_t index = (size_t)__builtin_ctz((~equal_mask) & 0xffffU);
            return (int)left[index] - (int)right[index];
        }
        left += 16;
        right += 16;
        count -= 16;
    }

    return compare_bytes(left, right, count);
}

static size_t strlen_sse2(const unsigned char *string)
{
    const unsigned char *cursor = string;
    const __m128i zero = _mm_setzero_si128();

    for (;;) {
        size_t page_offset = (uintptr_t)cursor & 4095U;
        size_t page_room = 4096U - page_offset;
        if (page_room >= 64) {
            __m128i chunk0 = _mm_loadu_si128((const __m128i *)cursor);
            __m128i chunk1 = _mm_loadu_si128((const __m128i *)(cursor + 16));
            __m128i chunk2 = _mm_loadu_si128((const __m128i *)(cursor + 32));
            __m128i chunk3 = _mm_loadu_si128((const __m128i *)(cursor + 48));
            uint64_t zero_mask = (uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk0, zero));
            zero_mask |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk1, zero)) << 16;
            zero_mask |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk2, zero)) << 32;
            zero_mask |= (uint64_t)(uint16_t)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk3, zero)) << 48;
            if (zero_mask != 0) {
                return (size_t)(cursor - string) + (size_t)__builtin_ctzll(zero_mask);
            }
            cursor += 64;
        } else if (page_room >= 16) {
            __m128i chunk = _mm_loadu_si128((const __m128i *)cursor);
            unsigned int zero_mask = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(chunk, zero));
            if (zero_mask != 0) {
                return (size_t)(cursor - string) + (size_t)__builtin_ctz(zero_mask);
            }
            cursor += 16;
        } else {
            if (*cursor == '\0') {
                return (size_t)(cursor - string);
            }
            cursor++;
        }
    }
}

static unsigned int string_chunk_stop_mask(__m128i left, __m128i right)
{
    const __m128i zero = _mm_setzero_si128();
    unsigned int equal_mask = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(left, right));
    unsigned int zero_mask = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(left, zero));

    return ((~equal_mask) & 0xffffU) | zero_mask;
}

static int compare_strings_sse2(const unsigned char *left, const unsigned char *right,
    size_t count, int bounded)
{
    while (!bounded || count > 0) {
        size_t left_room = 4096U - ((uintptr_t)left & 4095U);
        size_t right_room = 4096U - ((uintptr_t)right & 4095U);

        if ((!bounded || count >= 64) && left_room >= 64 && right_room >= 64) {
            __m128i left0 = _mm_loadu_si128((const __m128i *)left);
            __m128i left1 = _mm_loadu_si128((const __m128i *)(left + 16));
            __m128i left2 = _mm_loadu_si128((const __m128i *)(left + 32));
            __m128i left3 = _mm_loadu_si128((const __m128i *)(left + 48));
            __m128i right0 = _mm_loadu_si128((const __m128i *)right);
            __m128i right1 = _mm_loadu_si128((const __m128i *)(right + 16));
            __m128i right2 = _mm_loadu_si128((const __m128i *)(right + 32));
            __m128i right3 = _mm_loadu_si128((const __m128i *)(right + 48));
            uint64_t stop_mask = string_chunk_stop_mask(left0, right0);
            stop_mask |= (uint64_t)string_chunk_stop_mask(left1, right1) << 16;
            stop_mask |= (uint64_t)string_chunk_stop_mask(left2, right2) << 32;
            stop_mask |= (uint64_t)string_chunk_stop_mask(left3, right3) << 48;

            if (stop_mask != 0) {
                size_t index = (size_t)__builtin_ctzll(stop_mask);
                return (int)left[index] - (int)right[index];
            }
            left += 64;
            right += 64;
            if (bounded) {
                count -= 64;
            }
            continue;
        }

        if ((bounded && count < 16) || left_room < 16 || right_room < 16) {
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
            continue;
        }

        __m128i left_chunk = _mm_loadu_si128((const __m128i *)left);
        __m128i right_chunk = _mm_loadu_si128((const __m128i *)right);
        unsigned int stop_mask = string_chunk_stop_mask(left_chunk, right_chunk);

        if (stop_mask != 0) {
            size_t index = (size_t)__builtin_ctz(stop_mask);
            return (int)left[index] - (int)right[index];
        }
        left += 16;
        right += 16;
        if (bounded) {
            count -= 16;
        }
    }

    return 0;
}

#if defined(IRIS_HAVE_AVX2)
static void cpuid(unsigned int leaf, unsigned int subleaf, unsigned int *eax_out,
    unsigned int *ebx_out, unsigned int *ecx_out, unsigned int *edx_out)
{
    unsigned int eax = leaf;
    unsigned int ebx;
    unsigned int ecx = subleaf;
    unsigned int edx;

    __asm__ volatile("cpuid" : "+a"(eax), "=b"(ebx), "+c"(ecx), "=d"(edx));
    *eax_out = eax;
    *ebx_out = ebx;
    *ecx_out = ecx;
    *edx_out = edx;
}

static int detect_avx2(void)
{
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;
    unsigned int xcr0_low;
    unsigned int xcr0_high;

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 7) {
        return 0;
    }
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if ((ecx & ((1U << 27) | (1U << 28))) != ((1U << 27) | (1U << 28))) {
        return 0;
    }
    __asm__ volatile("xgetbv" : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0));
    if ((xcr0_low & 0x6U) != 0x6U) {
        return 0;
    }
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    return (ebx & (1U << 5)) != 0;
}

static inline int cpu_has_avx2(void)
{
    static atomic_int cached_support = ATOMIC_VAR_INIT(-1);
    int supported = atomic_load_explicit(&cached_support, memory_order_relaxed);

    if (__builtin_expect(supported < 0, 0)) {
        supported = detect_avx2();
        atomic_store_explicit(&cached_support, supported, memory_order_relaxed);
    }

    return supported;
}

static int detect_avx512(void)
{
    unsigned int eax;
    unsigned int ebx;
    unsigned int ecx;
    unsigned int edx;
    unsigned int xcr0_low;
    unsigned int xcr0_high;

    cpuid(0, 0, &eax, &ebx, &ecx, &edx);
    if (eax < 7) {
        return 0;
    }
    cpuid(1, 0, &eax, &ebx, &ecx, &edx);
    if ((ecx & ((1U << 27) | (1U << 28))) != ((1U << 27) | (1U << 28))) {
        return 0;
    }
    __asm__ volatile("xgetbv" : "=a"(xcr0_low), "=d"(xcr0_high) : "c"(0));
    if ((xcr0_low & 0xe6U) != 0xe6U) {
        return 0;
    }
    cpuid(7, 0, &eax, &ebx, &ecx, &edx);
    return (ebx & ((1U << 16) | (1U << 30) | (1U << 31))) ==
        ((1U << 16) | (1U << 30) | (1U << 31));
}

static inline int cpu_has_avx512(void)
{
    static atomic_int cached_support = ATOMIC_VAR_INIT(-1);
    int supported = atomic_load_explicit(&cached_support, memory_order_relaxed);

    if (__builtin_expect(supported < 0, 0)) {
        supported = detect_avx512();
        atomic_store_explicit(&cached_support, supported, memory_order_relaxed);
    }

    return supported;
}


__attribute__((target("avx2")))
static void *copy_avx2(unsigned char *output, const unsigned char *input, size_t count)
{
    size_t index = 0;

    for (; count - index >= 64; index += 64) {
        __m256i first = _mm256_loadu_si256((const __m256i *)(input + index));
        __m256i second = _mm256_loadu_si256((const __m256i *)(input + index + 32));
        _mm256_storeu_si256((__m256i *)(output + index), first);
        _mm256_storeu_si256((__m256i *)(output + index + 32), second);
    }
    for (; count - index >= 32; index += 32) {
        __m256i value = _mm256_loadu_si256((const __m256i *)(input + index));
        _mm256_storeu_si256((__m256i *)(output + index), value);
    }
    _mm256_zeroupper();
    if (index < count) {
        copy_sse2(output + index, input + index, count - index);
    }

    return output;
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static void *copy_avx512(unsigned char *output, const unsigned char *input, size_t count)
{
    if (count <= 128) {
        if (count <= 64) {
            if (count <= 32) {
                __m128i first = _mm_loadu_si128((const __m128i *)input);
                __m128i last = _mm_loadu_si128((const __m128i *)(input + count - 16));

                _mm_storeu_si128((__m128i *)output, first);
                _mm_storeu_si128((__m128i *)(output + count - 16), last);
            } else {
                __m256i first = _mm256_loadu_si256((const __m256i *)input);
                __m256i last = _mm256_loadu_si256((const __m256i *)(input + count - 32));

                _mm256_storeu_si256((__m256i *)output, first);
                _mm256_storeu_si256((__m256i *)(output + count - 32), last);
            }
            return output;
        }
        {
            __m512i first = _mm512_loadu_si512((const void *)input);
            __m512i last = _mm512_loadu_si512((const void *)(input + count - 64));

            _mm512_storeu_si512((void *)output, first);
            _mm512_storeu_si512((void *)(output + count - 64), last);
            return output;
        }
    }
    if (count <= 256) {
        __m512i first = _mm512_loadu_si512((const void *)input);
        __m512i second = _mm512_loadu_si512((const void *)(input + 64));
        __m512i penultimate = _mm512_loadu_si512((const void *)(input + count - 128));
        __m512i last = _mm512_loadu_si512((const void *)(input + count - 64));

        _mm512_storeu_si512((void *)output, first);
        _mm512_storeu_si512((void *)(output + 64), second);
        _mm512_storeu_si512((void *)(output + count - 128), penultimate);
        _mm512_storeu_si512((void *)(output + count - 64), last);
        return output;
    }

    size_t index = 0;
    for (; count - index >= 256; index += 256) {
        __m512i first = _mm512_loadu_si512((const void *)(input + index));
        __m512i second = _mm512_loadu_si512((const void *)(input + index + 64));
        __m512i third = _mm512_loadu_si512((const void *)(input + index + 128));
        __m512i fourth = _mm512_loadu_si512((const void *)(input + index + 192));
        _mm512_storeu_si512((void *)(output + index), first);
        _mm512_storeu_si512((void *)(output + index + 64), second);
        _mm512_storeu_si512((void *)(output + index + 128), third);
        _mm512_storeu_si512((void *)(output + index + 192), fourth);
    }
    if (index < count) {
        copy_avx512(output + index, input + index, count - index);
    }

    return output;
}

__attribute__((target("avx2")))
static void *move_avx2(unsigned char *output, const unsigned char *input, size_t count)
{
    if ((uintptr_t)output < (uintptr_t)input) {
        size_t index = 0;

        for (; count - index >= 64; index += 64) {
            __m256i first = _mm256_loadu_si256((const __m256i *)(input + index));
            __m256i second = _mm256_loadu_si256((const __m256i *)(input + index + 32));
            _mm256_storeu_si256((__m256i *)(output + index), first);
            _mm256_storeu_si256((__m256i *)(output + index + 32), second);
        }
        for (; count - index >= 32; index += 32) {
            __m256i value = _mm256_loadu_si256((const __m256i *)(input + index));
            _mm256_storeu_si256((__m256i *)(output + index), value);
        }
        _mm256_zeroupper();
        if (index < count) {
            move_sse2(output + index, input + index, count - index);
        }
        return output;
    }

    while (count >= 64) {
        count -= 64;
        __m256i first = _mm256_loadu_si256((const __m256i *)(input + count));
        __m256i second = _mm256_loadu_si256((const __m256i *)(input + count + 32));
        _mm256_storeu_si256((__m256i *)(output + count), first);
        _mm256_storeu_si256((__m256i *)(output + count + 32), second);
    }
    while (count >= 32) {
        count -= 32;
        __m256i value = _mm256_loadu_si256((const __m256i *)(input + count));
        _mm256_storeu_si256((__m256i *)(output + count), value);
    }
    _mm256_zeroupper();
    if (count > 0) {
        move_sse2(output, input, count);
    }

    return output;
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static void *move_avx512(unsigned char *output, const unsigned char *input, size_t count)
{
    if ((uintptr_t)output < (uintptr_t)input) {
        return copy_avx512(output, input, count);
    }
    if (count <= 32) {
        __m128i first = _mm_loadu_si128((const __m128i *)input);
        __m128i last = _mm_loadu_si128((const __m128i *)(input + count - 16));

        _mm_storeu_si128((__m128i *)(output + count - 16), last);
        _mm_storeu_si128((__m128i *)output, first);
        return output;
    }
    if (count <= 64) {
        __m256i first = _mm256_loadu_si256((const __m256i *)input);
        __m256i last = _mm256_loadu_si256((const __m256i *)(input + count - 32));

        _mm256_storeu_si256((__m256i *)(output + count - 32), last);
        _mm256_storeu_si256((__m256i *)output, first);
        return output;
    }
    if (count <= 128) {
        __m512i first = _mm512_loadu_si512((const void *)input);
        __m512i last = _mm512_loadu_si512((const void *)(input + count - 64));

        _mm512_storeu_si512((void *)(output + count - 64), last);
        _mm512_storeu_si512((void *)output, first);
        return output;
    }
    if (count <= 256) {
        __m512i first = _mm512_loadu_si512((const void *)input);
        __m512i second = _mm512_loadu_si512((const void *)(input + 64));
        __m512i penultimate = _mm512_loadu_si512((const void *)(input + count - 128));
        __m512i last = _mm512_loadu_si512((const void *)(input + count - 64));

        _mm512_storeu_si512((void *)(output + count - 128), penultimate);
        _mm512_storeu_si512((void *)(output + count - 64), last);
        _mm512_storeu_si512((void *)output, first);
        _mm512_storeu_si512((void *)(output + 64), second);
        return output;
    }

    size_t tail = (uintptr_t)(output + count) & 63U;
    while (tail > 0) {
        count--;
        output[count] = input[count];
        tail--;
    }
    while (count >= 256) {
        count -= 256;
        __m512i first = _mm512_loadu_si512((const void *)(input + count));
        __m512i second = _mm512_loadu_si512((const void *)(input + count + 64));
        __m512i third = _mm512_loadu_si512((const void *)(input + count + 128));
        __m512i fourth = _mm512_loadu_si512((const void *)(input + count + 192));
        _mm512_store_si512((void *)(output + count), first);
        _mm512_store_si512((void *)(output + count + 64), second);
        _mm512_store_si512((void *)(output + count + 128), third);
        _mm512_store_si512((void *)(output + count + 192), fourth);
    }
    while (count >= 64) {
        count -= 64;
        __m512i value = _mm512_loadu_si512((const void *)(input + count));
        _mm512_store_si512((void *)(output + count), value);
    }
    if (count >= 32) {
        count -= 32;
        __m256i value = _mm256_loadu_si256((const __m256i *)(input + count));
        _mm256_storeu_si256((__m256i *)(output + count), value);
    }
    if (count >= 16) {
        count -= 16;
        __m128i value = _mm_loadu_si128((const __m128i *)(input + count));
        _mm_storeu_si128((__m128i *)(output + count), value);
    }
    if (count >= 8) {
        count -= 8;
        uint64_t value;

        __builtin_memcpy(&value, input + count, 8);
        __builtin_memcpy(output + count, &value, 8);
    }
    while (count > 0) {
        count--;
        output[count] = input[count];
    }

    return output;
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static void *set_avx512(void *destination, int value, size_t count)
{
    unsigned char *output = destination;
    __m512i pattern = _mm512_set1_epi8((char)value);

    if (count <= 128) {
        if (count <= 64) {
            if (count <= 32) {
                __m128i small = _mm_set1_epi8((char)value);

                _mm_storeu_si128((__m128i *)output, small);
                _mm_storeu_si128((__m128i *)(output + count - 16), small);
            } else {
                __m256i small = _mm256_set1_epi8((char)value);

                _mm256_storeu_si256((__m256i *)output, small);
                _mm256_storeu_si256((__m256i *)(output + count - 32), small);
            }
            return destination;
        }
        _mm512_storeu_si512((void *)output, pattern);
        _mm512_storeu_si512((void *)(output + count - 64), pattern);
        return destination;
    }
    if (count <= 256) {
        _mm512_storeu_si512((void *)output, pattern);
        _mm512_storeu_si512((void *)(output + 64), pattern);
        _mm512_storeu_si512((void *)(output + count - 128), pattern);
        _mm512_storeu_si512((void *)(output + count - 64), pattern);
        return destination;
    }

    size_t index = 0;
    for (; count - index >= 256; index += 256) {
        _mm512_storeu_si512((void *)(output + index), pattern);
        _mm512_storeu_si512((void *)(output + index + 64), pattern);
        _mm512_storeu_si512((void *)(output + index + 128), pattern);
        _mm512_storeu_si512((void *)(output + index + 192), pattern);
    }
    if (index < count) {
        set_avx512(output + index, value, count - index);
    }

    return destination;
}

__attribute__((target("avx2")))
static void *set_avx2(void *destination, int value, size_t count)
{
    unsigned char *output = destination;
    __m256i pattern = _mm256_set1_epi8((char)value);
    size_t index = 0;

    for (; count - index >= 64; index += 64) {
        _mm256_storeu_si256((__m256i *)(output + index), pattern);
        _mm256_storeu_si256((__m256i *)(output + index + 32), pattern);
    }
    for (; count - index >= 32; index += 32) {
        _mm256_storeu_si256((__m256i *)(output + index), pattern);
    }
    _mm256_zeroupper();
    if (index < count) {
        set_sse2(output + index, value, count - index);
    }

    return destination;
}

__attribute__((target("avx2")))
static int memcmp_avx2(const unsigned char *left, const unsigned char *right, size_t count)
{
    while (count >= 64) {
        __m256i left0 = _mm256_loadu_si256((const __m256i *)left);
        __m256i left1 = _mm256_loadu_si256((const __m256i *)(left + 32));
        __m256i right0 = _mm256_loadu_si256((const __m256i *)right);
        __m256i right1 = _mm256_loadu_si256((const __m256i *)(right + 32));
        uint64_t equal_mask = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(left0, right0));
        equal_mask |= (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(left1, right1)) << 32;

        if (equal_mask != UINT64_MAX) {
            size_t index = (size_t)__builtin_ctzll(~equal_mask);
            return (int)left[index] - (int)right[index];
        }
        left += 64;
        right += 64;
        count -= 64;
    }
    while (count >= 32) {
        __m256i left_chunk = _mm256_loadu_si256((const __m256i *)left);
        __m256i right_chunk = _mm256_loadu_si256((const __m256i *)right);
        uint32_t equal_mask = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(left_chunk, right_chunk));

        if (equal_mask != UINT32_MAX) {
            size_t index = (size_t)__builtin_ctz(~equal_mask);
            return (int)left[index] - (int)right[index];
        }
        left += 32;
        right += 32;
        count -= 32;
    }
    _mm256_zeroupper();
    return memcmp_sse2(left, right, count);
}

__attribute__((target("avx2")))
static size_t strlen_avx2(const unsigned char *string)
{
    const unsigned char *cursor = string;
    const __m256i zero = _mm256_setzero_si256();

    for (;;) {
        size_t page_room = 4096U - ((uintptr_t)cursor & 4095U);
        while (page_room >= 64) {
            __m256i first = _mm256_loadu_si256((const __m256i *)cursor);
            __m256i second = _mm256_loadu_si256((const __m256i *)(cursor + 32));
            uint64_t zero_mask = (uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(first, zero));
            zero_mask |= (uint64_t)(uint32_t)_mm256_movemask_epi8(_mm256_cmpeq_epi8(second, zero)) << 32;
            if (zero_mask != 0) {
                size_t length = (size_t)(cursor - string) + (size_t)__builtin_ctzll(zero_mask);
                _mm256_zeroupper();
                return length;
            }
            cursor += 64;
            page_room -= 64;
        }
        if (page_room >= 32) {
            __m256i chunk = _mm256_loadu_si256((const __m256i *)cursor);
            unsigned int zero_mask = (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(chunk, zero));
            if (zero_mask != 0) {
                size_t length = (size_t)(cursor - string) + (size_t)__builtin_ctz(zero_mask);
                _mm256_zeroupper();
                return length;
            }
            cursor += 32;
        } else {
            if (*cursor == '\0') {
                size_t length = (size_t)(cursor - string);
                _mm256_zeroupper();
                return length;
            }
            cursor++;
        }
    }
}

__attribute__((target("avx2")))
static unsigned int string_chunk_stop_mask_avx2(__m256i left, __m256i right, __m256i zero)
{
    unsigned int equal_mask = (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(left, right));
    unsigned int zero_mask = (unsigned int)_mm256_movemask_epi8(_mm256_cmpeq_epi8(left, zero));

    return ~equal_mask | zero_mask;
}

__attribute__((target("avx2")))
static int compare_strings_avx2(const unsigned char *left, const unsigned char *right,
    size_t count, int bounded)
{
    const __m256i zero = _mm256_setzero_si256();

    for (;;) {
        if (bounded && count == 0) {
            _mm256_zeroupper();
            return 0;
        }
        size_t left_room = 4096U - ((uintptr_t)left & 4095U);
        size_t right_room = 4096U - ((uintptr_t)right & 4095U);
        size_t safe_room = left_room < right_room ? left_room : right_room;

        while ((!bounded || count >= 64) && safe_room >= 64) {
            __m256i left0 = _mm256_loadu_si256((const __m256i *)left);
            __m256i left1 = _mm256_loadu_si256((const __m256i *)(left + 32));
            __m256i right0 = _mm256_loadu_si256((const __m256i *)right);
            __m256i right1 = _mm256_loadu_si256((const __m256i *)(right + 32));
            uint64_t stop_mask = string_chunk_stop_mask_avx2(left0, right0, zero);
            stop_mask |= (uint64_t)string_chunk_stop_mask_avx2(left1, right1, zero) << 32;

            if (stop_mask != 0) {
                size_t index = (size_t)__builtin_ctzll(stop_mask);
                int result = (int)left[index] - (int)right[index];
                _mm256_zeroupper();
                return result;
            }
            left += 64;
            right += 64;
            if (bounded) {
                count -= 64;
            }
            safe_room -= 64;
        }
        if (bounded && count == 0) {
            _mm256_zeroupper();
            return 0;
        }
        if ((!bounded || count >= 32) && safe_room >= 32) {
            __m256i left_chunk = _mm256_loadu_si256((const __m256i *)left);
            __m256i right_chunk = _mm256_loadu_si256((const __m256i *)right);
            unsigned int stop_mask = string_chunk_stop_mask_avx2(left_chunk, right_chunk, zero);

            if (stop_mask != 0) {
                size_t index = (size_t)__builtin_ctz(stop_mask);
                int result = (int)left[index] - (int)right[index];
                _mm256_zeroupper();
                return result;
            }
            left += 32;
            right += 32;
            if (bounded) {
                count -= 32;
            }
            continue;
        }

        _mm256_zeroupper();
        return compare_strings_sse2(left, right, count, bounded);
    }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static int memcmp_short_avx512(const unsigned char *left,
    const unsigned char *right, size_t count)
{
    if (count <= 128) {
        __m512i difference0 = _mm512_xor_si512(
            _mm512_loadu_si512((const void *)left),
            _mm512_loadu_si512((const void *)right));
        __m512i difference1 = _mm512_xor_si512(
            _mm512_loadu_si512((const void *)(left + count - 64)),
            _mm512_loadu_si512((const void *)(right + count - 64)));
        __m512i difference = _mm512_ternarylogic_epi32(difference0, difference1,
            difference1, 0xfe);

        if ((uint64_t)_mm512_test_epi8_mask(difference, difference) == 0) {
            return 0;
        }
        uint64_t mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference0,
            difference0);
        size_t index;

        if (mismatch_mask != 0) {
            index = (size_t)__builtin_ctzll(mismatch_mask);
        } else {
            mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference1,
                difference1);
            index = count - 64 + (size_t)__builtin_ctzll(mismatch_mask);
        }
        return (int)left[index] - (int)right[index];
    }

    __m512i difference0 = _mm512_xor_si512(
        _mm512_loadu_si512((const void *)left),
        _mm512_loadu_si512((const void *)right));
    __m512i difference1 = _mm512_xor_si512(
        _mm512_loadu_si512((const void *)(left + 64)),
        _mm512_loadu_si512((const void *)(right + 64)));
    __m512i difference2 = _mm512_xor_si512(
        _mm512_loadu_si512((const void *)(left + count - 128)),
        _mm512_loadu_si512((const void *)(right + count - 128)));
    __m512i difference3 = _mm512_xor_si512(
        _mm512_loadu_si512((const void *)(left + count - 64)),
        _mm512_loadu_si512((const void *)(right + count - 64)));
    __m512i combined = _mm512_ternarylogic_epi32(difference0, difference1,
        difference2, 0xfe);
    __m512i difference = _mm512_ternarylogic_epi32(combined, difference3,
        difference3, 0xfe);

    if ((uint64_t)_mm512_test_epi8_mask(difference, difference) == 0) {
        return 0;
    }
    uint64_t mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference0,
        difference0);

    if (mismatch_mask != 0) {
        size_t index = (size_t)__builtin_ctzll(mismatch_mask);
        return (int)left[index] - (int)right[index];
    }
    mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference1, difference1);
    if (mismatch_mask != 0) {
        size_t index = 64 + (size_t)__builtin_ctzll(mismatch_mask);
        return (int)left[index] - (int)right[index];
    }
    mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference2, difference2);
    if (mismatch_mask != 0) {
        size_t index = count - 128 + (size_t)__builtin_ctzll(mismatch_mask);
        return (int)left[index] - (int)right[index];
    }
    mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference3, difference3);
    {
        size_t index = count - 64 + (size_t)__builtin_ctzll(mismatch_mask);
        return (int)left[index] - (int)right[index];
    }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static int memcmp_avx512(const unsigned char *left, const unsigned char *right, size_t count)
{
    if (count >= 64 && count <= 256) {
        return memcmp_short_avx512(left, right, count);
    }
    while (count >= 128) {
        __m512i left0 = _mm512_loadu_si512((const void *)left);
        __m512i left1 = _mm512_loadu_si512((const void *)(left + 64));
        __m512i right0 = _mm512_loadu_si512((const void *)right);
        __m512i right1 = _mm512_loadu_si512((const void *)(right + 64));
        __m512i difference0 = _mm512_xor_si512(left0, right0);
        __m512i difference1 = _mm512_xor_si512(left1, right1);
        __m512i difference = _mm512_ternarylogic_epi32(difference0, difference1, difference1, 0xfe);
        uint64_t mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference, difference);

        if (mismatch_mask != 0) {
            uint64_t first_mask = (uint64_t)_mm512_test_epi8_mask(difference0, difference0);
            size_t index;

            if (first_mask != 0) {
                index = (size_t)__builtin_ctzll(first_mask);
            } else {
                uint64_t second_mask = (uint64_t)_mm512_test_epi8_mask(difference1, difference1);
                index = 64 + (size_t)__builtin_ctzll(second_mask);
            }
            return (int)left[index] - (int)right[index];
        }
        left += 128;
        right += 128;
        count -= 128;
    }
    if (count >= 64) {
        __m512i difference = _mm512_xor_si512(
            _mm512_loadu_si512((const void *)left),
            _mm512_loadu_si512((const void *)right));
        uint64_t mismatch_mask = (uint64_t)_mm512_test_epi8_mask(difference, difference);

        if (mismatch_mask != 0) {
            size_t index = (size_t)__builtin_ctzll(mismatch_mask);
            return (int)left[index] - (int)right[index];
        }
        left += 64;
        right += 64;
        count -= 64;
    }
    if (count >= 32) {
        __m256i difference = _mm256_xor_si256(
            _mm256_loadu_si256((const __m256i *)left),
            _mm256_loadu_si256((const __m256i *)right));
        unsigned int mismatch_mask = (unsigned int)_mm256_test_epi8_mask(difference, difference);

        if (mismatch_mask != 0) {
            size_t index = (size_t)__builtin_ctz(mismatch_mask);
            return (int)left[index] - (int)right[index];
        }
        left += 32;
        right += 32;
        count -= 32;
    }
    return compare_bytes(left, right, count);
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static size_t strlen_avx512(const unsigned char *string)
{
    const unsigned char *cursor = string;
    size_t room = 4096U - ((uintptr_t)cursor & 4095U);

    if (room >= 64) {
        __m512i chunk = _mm512_loadu_si512((const void *)cursor);
        uint64_t zero_mask = (uint64_t)_mm512_testn_epi8_mask(chunk, chunk);

        if (zero_mask != 0) {
            return (size_t)__builtin_ctzll(zero_mask);
        }
        cursor += 64;
    }

    for (;;) {
        size_t page_room = 4096U - ((uintptr_t)cursor & 4095U);
        while (page_room >= 256) {
            __m512i first = _mm512_loadu_si512((const void *)cursor);
            __m512i second = _mm512_loadu_si512((const void *)(cursor + 64));
            __m512i third = _mm512_loadu_si512((const void *)(cursor + 128));
            __m512i fourth = _mm512_loadu_si512((const void *)(cursor + 192));
            __m512i minimum = _mm512_min_epu8(_mm512_min_epu8(first, second),
                _mm512_min_epu8(third, fourth));

            if ((uint64_t)_mm512_testn_epi8_mask(minimum, minimum) != 0) {
                uint64_t zero_mask = (uint64_t)_mm512_testn_epi8_mask(first, first);
                size_t offset = (size_t)(cursor - string);

                if (zero_mask != 0) {
                    return offset + (size_t)__builtin_ctzll(zero_mask);
                }
                zero_mask = (uint64_t)_mm512_testn_epi8_mask(second, second);
                if (zero_mask != 0) {
                    return offset + 64 + (size_t)__builtin_ctzll(zero_mask);
                }
                zero_mask = (uint64_t)_mm512_testn_epi8_mask(third, third);
                if (zero_mask != 0) {
                    return offset + 128 + (size_t)__builtin_ctzll(zero_mask);
                }
                zero_mask = (uint64_t)_mm512_testn_epi8_mask(fourth, fourth);
                return offset + 192 + (size_t)__builtin_ctzll(zero_mask);
            }
            cursor += 256;
            page_room -= 256;
        }
        if (page_room >= 64) {
            __m512i chunk = _mm512_loadu_si512((const void *)cursor);
            uint64_t zero_mask = (uint64_t)_mm512_testn_epi8_mask(chunk, chunk);

            if (zero_mask != 0) {
                return (size_t)(cursor - string) + (size_t)__builtin_ctzll(zero_mask);
            }
            cursor += 64;
            continue;
        }
        if (page_room >= 32) {
            __m256i chunk = _mm256_loadu_si256((const __m256i *)cursor);
            unsigned int zero_mask = (unsigned int)_mm256_testn_epi8_mask(chunk, chunk);

            if (zero_mask != 0) {
                return (size_t)(cursor - string) + (size_t)__builtin_ctz(zero_mask);
            }
            cursor += 32;
            continue;
        }
        if (page_room == 0) {
            continue;
        }
        while (page_room > 0) {
            if (*cursor == '\0') {
                return (size_t)(cursor - string);
            }
            cursor++;
            page_room--;
        }
    }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static unsigned int string_chunk_stop_mask_avx512(__m256i left, __m256i right)
{
    return (unsigned int)(~_mm256_cmpeq_epi8_mask(left, right)) |
        (unsigned int)_mm256_testn_epi8_mask(left, left);
}

__attribute__((target("avx512f,avx512bw,avx512vl"), always_inline))
static inline uint64_t string_chunk_stop_mask_avx512_64(__m512i left, __m512i right)
{
    return (uint64_t)(~_mm512_cmpeq_epi8_mask(left, right)) |
        (uint64_t)_mm512_testn_epi8_mask(left, left);
}

__attribute__((target("avx512f,avx512bw,avx512vl"), always_inline))
static inline int compare_block_avx512(const unsigned char *left,
    const unsigned char *right, size_t *stop_index)
{
    __m512i left0 = _mm512_loadu_si512((const void *)left);
    __m512i left1 = _mm512_loadu_si512((const void *)(left + 64));
    __m512i left2 = _mm512_loadu_si512((const void *)(left + 128));
    __m512i left3 = _mm512_loadu_si512((const void *)(left + 192));
    __m512i right0 = _mm512_loadu_si512((const void *)right);
    __m512i right1 = _mm512_loadu_si512((const void *)(right + 64));
    __m512i right2 = _mm512_loadu_si512((const void *)(right + 128));
    __m512i right3 = _mm512_loadu_si512((const void *)(right + 192));
    __m512i minimum = _mm512_min_epu8(_mm512_min_epu8(left0, left1),
        _mm512_min_epu8(left2, left3));
    __m512i difference0 = _mm512_xor_si512(left0, right0);
    __m512i difference1 = _mm512_xor_si512(left1, right1);
    __m512i difference2 = _mm512_xor_si512(left2, right2);
    __m512i difference3 = _mm512_xor_si512(left3, right3);
    __m512i difference01 = _mm512_ternarylogic_epi32(difference0, difference1,
        difference1, 0xfe);
    __m512i difference23 = _mm512_ternarylogic_epi32(difference2, difference3,
        difference3, 0xfe);
    __m512i difference = _mm512_ternarylogic_epi32(difference01, difference23,
        difference23, 0xfe);
    __mmask64 no_zero = _mm512_test_epi8_mask(minimum, minimum);
    __mmask64 equal = _mm512_mask_testn_epi8_mask(no_zero, difference, difference);

    if ((uint64_t)(equal + 1U) == 0) {
        return 0;
    }
    uint64_t stop_mask = string_chunk_stop_mask_avx512_64(left0, right0);

    if (stop_mask != 0) {
        *stop_index = (size_t)__builtin_ctzll(stop_mask);
        return 1;
    }
    stop_mask = string_chunk_stop_mask_avx512_64(left1, right1);
    if (stop_mask != 0) {
        *stop_index = 64 + (size_t)__builtin_ctzll(stop_mask);
        return 1;
    }
    stop_mask = string_chunk_stop_mask_avx512_64(left2, right2);
    if (stop_mask != 0) {
        *stop_index = 128 + (size_t)__builtin_ctzll(stop_mask);
        return 1;
    }
    stop_mask = string_chunk_stop_mask_avx512_64(left3, right3);
    *stop_index = 192 + (size_t)__builtin_ctzll(stop_mask);
    return 1;
}

__attribute__((target("avx512f,avx512bw,avx512vl"), always_inline))
static inline int compare_block_avx512_128(const unsigned char *left,
    const unsigned char *right, size_t *stop_index)
{
    __m512i left0 = _mm512_loadu_si512((const void *)left);
    __m512i left1 = _mm512_loadu_si512((const void *)(left + 64));
    __m512i right0 = _mm512_loadu_si512((const void *)right);
    __m512i right1 = _mm512_loadu_si512((const void *)(right + 64));
    __m512i minimum = _mm512_min_epu8(left0, left1);
    __m512i difference0 = _mm512_xor_si512(left0, right0);
    __m512i difference1 = _mm512_xor_si512(left1, right1);
    __m512i difference = _mm512_ternarylogic_epi32(difference0, difference1,
        difference1, 0xfe);
    __mmask64 no_zero = _mm512_test_epi8_mask(minimum, minimum);
    __mmask64 equal = _mm512_mask_testn_epi8_mask(no_zero, difference, difference);

    if ((uint64_t)(equal + 1U) == 0) {
        return 0;
    }
    uint64_t stop_mask = string_chunk_stop_mask_avx512_64(left0, right0);
    if (stop_mask != 0) {
        *stop_index = (size_t)__builtin_ctzll(stop_mask);
        return 1;
    }
    stop_mask = string_chunk_stop_mask_avx512_64(left1, right1);
    *stop_index = 64 + (size_t)__builtin_ctzll(stop_mask);
    return 1;
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static int compare_strings_avx512(const unsigned char *left, const unsigned char *right)
{
    for (;;) {
        size_t left_room = 4096U - ((uintptr_t)left & 4095U);
        size_t right_room = 4096U - ((uintptr_t)right & 4095U);
        size_t safe_room = left_room < right_room ? left_room : right_room;

        while (safe_room >= 256) {
            size_t stop_index;

            if (compare_block_avx512(left, right, &stop_index)) {
                return (int)left[stop_index] - (int)right[stop_index];
            }
            left += 256;
            right += 256;
            safe_room -= 256;
        }
        if (safe_room >= 32) {
            __m256i left_chunk = _mm256_loadu_si256((const __m256i *)left);
            __m256i right_chunk = _mm256_loadu_si256((const __m256i *)right);
            unsigned int stop_mask = string_chunk_stop_mask_avx512(left_chunk, right_chunk);

            if (stop_mask != 0) {
                size_t index = (size_t)__builtin_ctz(stop_mask);
                return (int)left[index] - (int)right[index];
            }
            left += 32;
            right += 32;
            safe_room -= 32;
            continue;
        }
        if (safe_room == 0) {
            continue;
        }
        while (safe_room > 0) {
            if (*left != *right) {
                return (int)*left - (int)*right;
            }
            if (*left == '\0') {
                return 0;
            }
            left++;
            right++;
            safe_room--;
        }
    }
}

__attribute__((target("avx512f,avx512bw,avx512vl")))
static int compare_n_strings_avx512(const unsigned char *left, const unsigned char *right,
    size_t count)
{
    for (;;) {
        if (count == 0) {
            return 0;
        }
        size_t left_room = 4096U - ((uintptr_t)left & 4095U);
        size_t right_room = 4096U - ((uintptr_t)right & 4095U);
        size_t safe_room = left_room < right_room ? left_room : right_room;

        while (count >= 128 && safe_room >= 128) {
            size_t stop_index;

            if (compare_block_avx512_128(left, right, &stop_index)) {
                return (int)left[stop_index] - (int)right[stop_index];
            }
            left += 128;
            right += 128;
            count -= 128;
            safe_room -= 128;
        }
        if (count == 0) {
            return 0;
        }
        if (count >= 32 && safe_room >= 32) {
            __m256i left_chunk = _mm256_loadu_si256((const __m256i *)left);
            __m256i right_chunk = _mm256_loadu_si256((const __m256i *)right);
            unsigned int stop_mask = string_chunk_stop_mask_avx512(left_chunk, right_chunk);

            if (stop_mask != 0) {
                size_t index = (size_t)__builtin_ctz(stop_mask);
                return (int)left[index] - (int)right[index];
            }
            left += 32;
            right += 32;
            count -= 32;
            safe_room -= 32;
            continue;
        }
        if (safe_room == 0) {
            continue;
        }
        size_t chunk = count < safe_room ? count : safe_room;
        for (size_t index = 0; index < chunk; index++) {
            if (left[index] != right[index]) {
                return (int)left[index] - (int)right[index];
            }
            if (left[index] == '\0') {
                return 0;
            }
        }
        left += chunk;
        right += chunk;
        count -= chunk;
    }
}

#endif
#endif

#if defined(__x86_64__)
void *memcpy(void *restrict destination, const void *restrict source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;


    if (count == 16) {
        __m128i value = _mm_loadu_si128((const __m128i *)input);
        _mm_storeu_si128((__m128i *)output, value);
        return destination;
    }
    if (count < 16) {
        copy_tiny(output, input, count);
        return destination;
    }
    if (count <= 64) {
        copy_small(output, input, count);
        return destination;
    }
#if defined(IRIS_HAVE_AVX2)
    if (cpu_has_avx512()) {
        return copy_avx512(output, input, count);
    }
    if (cpu_has_avx2()) {
        return copy_avx2(output, input, count);
    }
#endif
    return copy_sse2(output, input, count);
}

void *memmove(void *destination, const void *source, size_t count)
{
    unsigned char *output = destination;
    const unsigned char *input = source;

    if (count == 0 || output == input) {
        return destination;
    }
    if (count == 16) {
        __m128i value = _mm_loadu_si128((const __m128i *)input);
        _mm_storeu_si128((__m128i *)output, value);
        return destination;
    }
    if (count < 16) {
        copy_tiny(output, input, count);
        return destination;
    }
    if (count <= 64) {
        copy_small(output, input, count);
        return destination;
    }
#if defined(IRIS_HAVE_AVX2)
    if (cpu_has_avx512()) {
        return move_avx512(output, input, count);
    }
    if (cpu_has_avx2()) {
        return move_avx2(output, input, count);
    }
#endif
    return move_sse2(output, input, count);
}

void *memset(void *destination, int value, size_t count)
{
    unsigned char *output = destination;
    unsigned char byte = (unsigned char)value;

    if (count <= 16) {
        set_tiny(output, byte, count);
        return destination;
    }
    if (count <= 64) {
        set_small(output, byte, count);
        return destination;
    }
#if defined(IRIS_HAVE_AVX2)
    if (cpu_has_avx512()) {
        return set_avx512(destination, value, count);
    }
    if (cpu_has_avx2()) {
        return set_avx2(destination, value, count);
    }
#endif
    return set_sse2(destination, value, count);
}

int memcmp(const void *left, const void *right, size_t count)
{
#if defined(IRIS_HAVE_AVX2)
    if (count >= 64) {
        if (cpu_has_avx512()) {
            if (count <= 256) {
                return memcmp_short_avx512((const unsigned char *)left,
                    (const unsigned char *)right, count);
            }
            return memcmp_avx512((const unsigned char *)left,
                (const unsigned char *)right, count);
        }
        if (cpu_has_avx2()) {
            return memcmp_avx2((const unsigned char *)left,
                (const unsigned char *)right, count);
        }
    }
#endif
    if (count == 16) {
        __m128i left_chunk = _mm_loadu_si128((const __m128i *)left);
        __m128i right_chunk = _mm_loadu_si128((const __m128i *)right);
        unsigned int equal_mask = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(left_chunk, right_chunk));

        if (equal_mask != 0xffffU) {
            size_t index = (size_t)__builtin_ctz((~equal_mask) & 0xffffU);
            return (int)((const unsigned char *)left)[index] -
                (int)((const unsigned char *)right)[index];
        }
        return 0;
    }
    if (count >= 16) {
        return memcmp_sse2(left, right, count);
    }
    return compare_bytes(left, right, count);
}

size_t strlen(const char *string)
{
    const unsigned char *bytes = (const unsigned char *)string;
    size_t page_room = 4096U - ((uintptr_t)bytes & 4095U);

    if (page_room >= 32) {
        const __m128i zero = _mm_setzero_si128();
        __m128i first = _mm_loadu_si128((const __m128i *)bytes);
        unsigned int zero_mask = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(first, zero));

        if (zero_mask != 0) {
            return (size_t)__builtin_ctz(zero_mask);
        }
        __m128i second = _mm_loadu_si128((const __m128i *)(bytes + 16));
        unsigned int second_zero_mask = (unsigned int)_mm_movemask_epi8(
            _mm_cmpeq_epi8(second, zero));
        if (second_zero_mask != 0) {
            return 16 + (size_t)__builtin_ctz(second_zero_mask);
        }
        /* Longer strings use the scanner's original alignment and wide
         * chunks, which benchmark better than starting it at byte 32. */
    }
#if defined(IRIS_HAVE_AVX2)
    if (cpu_has_avx512()) {
        return strlen_avx512((const unsigned char *)string);
    }
    if (cpu_has_avx2()) {
        return strlen_avx2((const unsigned char *)string);
    }
#endif
    return strlen_sse2((const unsigned char *)string);
}

int strcmp(const char *left, const char *right)
{
    const unsigned char *left_bytes = (const unsigned char *)left;
    const unsigned char *right_bytes = (const unsigned char *)right;
    size_t left_room = 4096U - ((uintptr_t)left_bytes & 4095U);
    size_t right_room = 4096U - ((uintptr_t)right_bytes & 4095U);

    if (left_room >= 16 && right_room >= 16) {
        __m128i left0 = _mm_loadu_si128((const __m128i *)left_bytes);
        __m128i right0 = _mm_loadu_si128((const __m128i *)right_bytes);
        unsigned int stop_mask = string_chunk_stop_mask(left0, right0);
        if (stop_mask != 0) {
            size_t index = (size_t)__builtin_ctz(stop_mask);
            return (int)left_bytes[index] - (int)right_bytes[index];
        }
        left_room -= 16;
        right_room -= 16;
        if (left_room >= 16 && right_room >= 16) {
            __m128i left1 = _mm_loadu_si128((const __m128i *)(left_bytes + 16));
            __m128i right1 = _mm_loadu_si128((const __m128i *)(right_bytes + 16));
            stop_mask = string_chunk_stop_mask(left1, right1);
            if (stop_mask != 0) {
                size_t index = 16 + (size_t)__builtin_ctz(stop_mask);
                return (int)left_bytes[index] - (int)right_bytes[index];
            }
            left_bytes += 32;
            right_bytes += 32;
        }
    }
#if defined(IRIS_HAVE_AVX2)
    if (cpu_has_avx512()) {
        return compare_strings_avx512(left_bytes, right_bytes);
    }
    if (cpu_has_avx2()) {
        return compare_strings_avx2(left_bytes, right_bytes, SIZE_MAX, 0);
    }
#endif
    return compare_strings_sse2(left_bytes, right_bytes, SIZE_MAX, 0);
}

int strncmp(const char *left, const char *right, size_t count)
{
    if (count < 16) {
        for (size_t index = 0; index < count; index++) {
            unsigned char left_char = (unsigned char)left[index];
            unsigned char right_char = (unsigned char)right[index];
            if (left_char != right_char) {
                return (int)left_char - (int)right_char;
            }
            if (left_char == '\0') {
                return 0;
            }
        }
        return 0;
    }
    if (count == 16) {
        __m128i left_chunk = _mm_loadu_si128((const __m128i *)left);
        __m128i right_chunk = _mm_loadu_si128((const __m128i *)right);
        unsigned int stop_mask = string_chunk_stop_mask(left_chunk, right_chunk);
        if (stop_mask != 0) {
            size_t index = (size_t)__builtin_ctz(stop_mask);
            return (int)(unsigned char)left[index] - (int)(unsigned char)right[index];
        }
        return 0;
    }
#if defined(IRIS_HAVE_AVX2)
    if (count >= 64) {
        if (cpu_has_avx512()) {
            return compare_n_strings_avx512((const unsigned char *)left,
                (const unsigned char *)right, count);
        }
        if (cpu_has_avx2()) {
            return compare_strings_avx2((const unsigned char *)left,
                (const unsigned char *)right, count, 1);
        }
    } else if (count >= 32 && cpu_has_avx2()) {
        return compare_strings_avx2((const unsigned char *)left, (const unsigned char *)right,
            count, 1);
    }
#endif
    return compare_strings_sse2((const unsigned char *)left, (const unsigned char *)right,
        count, 1);
}
#endif
