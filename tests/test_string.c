#define _GNU_SOURCE

#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

extern int getpagesize(void);

static int check_word_paths(void)
{
    _Alignas(size_t) unsigned char original[96];
    _Alignas(size_t) unsigned char actual[96];
    _Alignas(size_t) unsigned char expected[96];
    unsigned char saved[40];

    for (size_t index = 0; index < sizeof(original); index++) {
        original[index] = (unsigned char)(index * 19U + 3U);
    }

    for (size_t source_offset = 0; source_offset < 8; source_offset++) {
        for (size_t destination_offset = 0; destination_offset < 8; destination_offset++) {
            for (size_t index = 0; index < sizeof(actual); index++) {
                actual[index] = original[index];
                expected[index] = original[index];
            }
            for (size_t index = 0; index < sizeof(saved); index++) {
                saved[index] = actual[source_offset + index];
                expected[destination_offset + index] = saved[index];
            }

            memmove(actual + destination_offset, actual + source_offset, sizeof(saved));
            for (size_t index = 0; index < sizeof(actual); index++) {
                if (actual[index] != expected[index]) {
                    return 1;
                }
            }

            memset(actual, 0, sizeof(actual));
            memcpy(actual + destination_offset, original + source_offset, sizeof(saved));
            for (size_t index = 0; index < sizeof(saved); index++) {
                if (actual[destination_offset + index] != original[source_offset + index]) {
                    return 2;
                }
            }
            memset(actual + destination_offset, 0xa7, sizeof(saved));
            for (size_t index = 0; index < sizeof(saved); index++) {
                if (actual[destination_offset + index] != 0xa7) {
                    return 3;
                }
            }
        }
    }

    _Alignas(size_t) unsigned char left_bytes[64];
    _Alignas(size_t) unsigned char right_bytes[64];
    for (size_t index = 0; index < sizeof(left_bytes); index++) {
        left_bytes[index] = (unsigned char)(index * 7U);
        right_bytes[index] = left_bytes[index];
    }
    for (size_t index = 0; index < sizeof(left_bytes); index++) {
        right_bytes[index] = (unsigned char)(left_bytes[index] ^ 0x80U);
        int result = memcmp(left_bytes, right_bytes, sizeof(left_bytes));
        if ((result < 0) != (left_bytes[index] < right_bytes[index])) {
            return 4;
        }
        right_bytes[index] = left_bytes[index];
    }

    _Alignas(size_t) char left_text[96];
    _Alignas(size_t) char right_text[96];
    for (size_t offset = 0; offset < 8; offset++) {
        for (size_t index = 0; index < sizeof(left_text); index++) {
            left_text[index] = 'q';
            right_text[index] = 'q';
        }
        left_text[offset + 64] = '\0';
        right_text[offset + 64] = '\0';
        if (strlen(left_text + offset) != 64 || strcmp(left_text + offset, right_text + offset) != 0 ||
            strncmp(left_text + offset, right_text + offset, 64) != 0) {
            return 5;
        }

        left_text[offset + 50] = (char)0xff;
        right_text[offset + 50] = (char)0x80;
        if (strcmp(left_text + offset, right_text + offset) <= 0 ||
            strncmp(left_text + offset, right_text + offset, 64) <= 0) {
            return 6;
        }
    }

    return 0;
}

static int check_vector_paths(void)
{
    _Alignas(32) unsigned char original[1024];
    _Alignas(32) unsigned char actual[1024];
    _Alignas(32) unsigned char expected[1024];
    unsigned char saved[512];

    for (size_t index = 0; index < sizeof(original); index++) {
        original[index] = (unsigned char)(index * 23U + 11U);
    }

    for (size_t source_offset = 0; source_offset < 32; source_offset++) {
        for (size_t destination_offset = 0; destination_offset < 32; destination_offset++) {
            for (size_t index = 0; index < sizeof(actual); index++) {
                actual[index] = original[index];
                expected[index] = original[index];
            }
            for (size_t index = 0; index < sizeof(saved); index++) {
                saved[index] = actual[source_offset + index];
                expected[destination_offset + index] = saved[index];
            }
            memmove(actual + destination_offset, actual + source_offset, sizeof(saved));
            for (size_t index = 0; index < sizeof(actual); index++) {
                if (actual[index] != expected[index]) {
                    return 1;
                }
            }

            memset(actual, 0, sizeof(actual));
            memcpy(actual + destination_offset, original + source_offset, sizeof(saved));
            for (size_t index = 0; index < sizeof(saved); index++) {
                if (actual[destination_offset + index] != original[source_offset + index]) {
                    return 2;
                }
            }
            memset(actual + destination_offset, 0xa7, sizeof(saved));
            for (size_t index = 0; index < sizeof(saved); index++) {
                if (actual[destination_offset + index] != 0xa7) {
                    return 3;
                }
            }
        }
    }

    _Alignas(32) unsigned char left_bytes[512];
    _Alignas(32) unsigned char right_bytes[512];
    for (size_t index = 0; index < sizeof(left_bytes); index++) {
        left_bytes[index] = (unsigned char)(index * 13U);
        right_bytes[index] = left_bytes[index];
    }
    for (size_t index = 0; index < sizeof(left_bytes); index++) {
        right_bytes[index] = (unsigned char)(left_bytes[index] ^ 0x80U);
        int result = memcmp(left_bytes, right_bytes, sizeof(left_bytes));
        if ((result < 0) != (left_bytes[index] < right_bytes[index])) {
            return 4;
        }
        right_bytes[index] = left_bytes[index];
    }
    for (size_t index = 0; index < 256; index++) {
        right_bytes[index] = (unsigned char)(left_bytes[index] ^ 0x80U);
        int result = memcmp(left_bytes, right_bytes, 256);
        if ((result < 0) != (left_bytes[index] < right_bytes[index])) {
            return 7;
        }
        right_bytes[index] = left_bytes[index];
    }

    _Alignas(32) char left_text[1024];
    _Alignas(32) char right_text[1024];
    for (size_t offset = 0; offset < 32; offset++) {
        for (size_t index = 0; index < sizeof(left_text); index++) {
            left_text[index] = 'a';
            right_text[index] = 'a';
        }
        left_text[offset + 512] = '\0';
        right_text[offset + 512] = '\0';
        if (strlen(left_text + offset) != 512 || strcmp(left_text + offset, right_text + offset) != 0 ||
            strncmp(left_text + offset, right_text + offset, 513) != 0) {
            return 5;
        }
        right_text[offset + 401] = 'b';
        if (strcmp(left_text + offset, right_text + offset) >= 0 ||
            strncmp(left_text + offset, right_text + offset, 513) >= 0) {
            return 6;
        }
    }

    return 0;
}

static int check_page_edge_strings(void)
{
    long page_size = getpagesize();
    if (page_size < 4096) {
        return 1;
    }

    size_t mapping_size = (size_t)page_size * 4;
    char *mapping = mmap(NULL, mapping_size, PROT_READ | PROT_WRITE,
        MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (mapping == MAP_FAILED) {
        return 2;
    }
    if (mprotect(mapping + page_size, (size_t)page_size, PROT_NONE) != 0) {
        munmap(mapping, mapping_size);
        return 3;
    }
    if (mprotect(mapping + page_size * 3, (size_t)page_size, PROT_NONE) != 0) {
        munmap(mapping, mapping_size);
        return 3;
    }

    char *last_byte = mapping + page_size - 1;
    *last_byte = '\0';
    int failed = strlen(last_byte) != 0 || strcmp(last_byte, "") != 0 ||
        strncmp(last_byte, "", 1) != 0;

    /* After strcmp's 32-byte prefix, its AVX-512 path compares this exact
     * 256-byte range, ending at the readable page boundary. */
    char *left = last_byte - 287;
    char *right = mapping + page_size * 3 - 288;
    for (size_t index = 0; index < 287; index++) {
        left[index] = 'q';
        right[index] = 'q';
    }
    left[287] = '\0';
    right[287] = '\0';
    if (strcmp(left, right) != 0 || strncmp(left, right, 288) != 0) {
        failed = 1;
    }

    const size_t mismatch_offsets[] = { 32, 95, 96, 159, 160, 223, 224, 286 };
    for (size_t index = 0;
        index < sizeof(mismatch_offsets) / sizeof(mismatch_offsets[0]); index++) {
        size_t offset = mismatch_offsets[index];
        right[offset] = 'z';
        if (strcmp(left, right) >= 0 || strncmp(left, right, 288) >= 0) {
            failed = 1;
        }
        right[offset] = 'q';
    }

    /* A shared NUL at a vector boundary must end comparison before any
     * differing bytes later in the mapped page. */
    left[160] = '\0';
    right[160] = '\0';
    right[200] = 'z';
    if (strcmp(left, right) != 0 || strncmp(left, right, 288) != 0) {
        failed = 1;
    }
    munmap(mapping, mapping_size);
    return failed ? 4 : 0;
}

static int check_unistd(void)
{
    FILE *stream = tmpfile();
    if (stream == NULL) {
        return 1;
    }

    const char expected[] = "iris";
    char actual[sizeof(expected)];
    int file_descriptor = fileno(stream);
    if (write(file_descriptor, expected, sizeof(expected)) != (ssize_t)sizeof(expected)) {
        fclose(stream);
        return 2;
    }
    if (fseek(stream, 0, SEEK_SET) != 0 ||
        read(file_descriptor, actual, sizeof(actual)) != (ssize_t)sizeof(actual) ||
        memcmp(actual, expected, sizeof(expected)) != 0) {
        fclose(stream);
        return 3;
    }
    if (fclose(stream) != 0) {
        return 4;
    }

    file_descriptor = open("/dev/null", O_RDONLY);
    if (file_descriptor < 0 || close(file_descriptor) != 0) {
        return 5;
    }

    errno = 0;
    if (read(-1, actual, sizeof(actual)) != -1 || errno != EBADF) {
        return 6;
    }

    return 0;
}

int main(void)
{
    char buffer[8] = "iris";
    const char expected[] = "iris";

    if (strlen(buffer) != 4 || strcmp(buffer, expected) != 0) {
        return 1;
    }
    if (strncmp("iris", "iridium", 3) != 0 || strncmp("iris", "iridium", 4) == 0) {
        return 2;
    }
    if (memcmp(buffer, expected, sizeof(expected)) != 0) {
        return 3;
    }

    memset(buffer, 'x', 2);
    if (buffer[0] != 'x' || buffer[1] != 'x' || buffer[2] != 'i') {
        return 4;
    }

    memcpy(buffer, expected, sizeof(expected));
    if (strcmp(buffer, expected) != 0) {
        return 5;
    }

    memmove(buffer + 1, buffer, 5);
    if (buffer[0] != 'i' || buffer[1] != 'i' || buffer[4] != 's' || buffer[5] != '\0') {
        return 6;
    }
    memmove(buffer, buffer + 1, 5);
    if (strcmp(buffer, expected) != 0) {
        return 7;
    }
    int word_result = check_word_paths();
    if (word_result != 0) {
        return 7 + word_result;
    }
    int vector_result = check_vector_paths();
    if (vector_result != 0) {
        return 20 + vector_result;
    }
    int page_result = check_page_edge_strings();
    if (page_result != 0) {
        return 30 + page_result;
    }
    int unistd_result = check_unistd();
    if (unistd_result != 0) {
        return 40 + unistd_result;
    }

    return 0;
}
