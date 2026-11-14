#define _GNU_SOURCE

#include <string.h>
#include <sys/mman.h>
#include <fcntl.h>
#include <stdio.h>
#include <errno.h>
#include <unistd.h>

extern int getpagesize(void);
extern int unlink(const char *path);

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
    unsigned char actual[4];
    int file_descriptor = open("/dev/zero", O_RDONLY);
    if (file_descriptor < 0 || read(file_descriptor, actual, sizeof(actual)) != (ssize_t)sizeof(actual)) {
        if (file_descriptor >= 0) close(file_descriptor);
        return 1;
    }
    for (size_t index = 0; index < sizeof(actual); index++) {
        if (actual[index] != 0) {
            close(file_descriptor);
            return 2;
        }
    }
    if (close(file_descriptor) != 0) {
        return 3;
    }

    file_descriptor = open("/dev/null", O_WRONLY);
    const char expected[] = "iris";
    if (file_descriptor < 0 || write(file_descriptor, expected, sizeof(expected)) != (ssize_t)sizeof(expected)) {
        if (file_descriptor >= 0) close(file_descriptor);
        return 4;
    }
    if (close(file_descriptor) != 0) {
        return 5;
    }

    file_descriptor = open("/dev/null", O_RDONLY);
    if (file_descriptor < 0 || close(file_descriptor) != 0) {
        return 6;
    }

    errno = 0;
    if (read(-1, actual, sizeof(actual)) != -1 || errno != EBADF) {
        return 7;
    }

    return 0;
}

static int format_variadic(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

static int print_variadic(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vprintf(format, arguments);
    va_end(arguments);
    return result;
}

static int print_stream_variadic(FILE *stream, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

static int check_stdio(void)
{
    char buffer[64];
    if (stdin == NULL || stdout == NULL || stderr == NULL) {
        return 12;
    }
    int length = snprintf(buffer, sizeof(buffer), "[%#08x] %s %d", 42U, "iris", -17);
    if (length != 19 || strcmp(buffer, "[0x00002a] iris -17") != 0) {
        return 1;
    }
    length = snprintf(buffer, 8, "%.3s %05d", "iridium", 42);
    if (length != 9 || strcmp(buffer, "iri 000") != 0) {
        return 2;
    }
    if (snprintf(NULL, 0, "%s:%u", "ok", 7U) != 4) {
        return 3;
    }
    if (snprintf(buffer, sizeof(buffer), "%+06d %#o %#.0o", -42, 8U, 0U) != 12 ||
        strcmp(buffer, "-00042 010 0") != 0) {
        return 4;
    }
    if (snprintf(buffer, sizeof(buffer), "%s", "abcdef") != 6 || buffer[6] != '\0') {
        return 5;
    }
    if (snprintf(buffer, 4, "%s", "abcdef") != 6 || strcmp(buffer, "abc") != 0) {
        return 8;
    }
    if (snprintf(buffer, sizeof(buffer), "%.0s", "abcdef") != 0 || buffer[0] != '\0') {
        return 9;
    }
    errno = 0;
    if (snprintf(buffer, sizeof(buffer), "1%2147483647s", "x") != -1 ||
        errno != EOVERFLOW || buffer[sizeof(buffer) - 1] != '\0') {
        return 10;
    }
    if (format_variadic(buffer, sizeof(buffer), "%s-%d", "ok", 7) != 4 ||
        strcmp(buffer, "ok-7") != 0) {
        return 6;
    }
    if (printf("%s", "") != 0 || printf("printf: %d\n", 7) != 10 ||
        print_variadic("vprintf: %d\n", 7) != 11 || fprintf(stderr, "") != 0) {
        return 7;
    }
    if (fflush(NULL) != 0) {
        return 11;
    }
    return 0;
}

static int check_file_streams(void)
{
    const char *path = "/tmp/iris-libc-stdio-test";
    const char original[] = "abcde";
    const char expected[] = "abcXe-7:ok";
    char actual[sizeof(expected)];
    unlink(path);

    FILE *stream = fopen(path, "w");
    if (stream == NULL) {
        unlink(path);
        return 1;
    }
    if (fwrite(original, 1, sizeof(original) - 1, stream) !=
        sizeof(original) - 1 || fprintf(stream, "-%d", 7) != 2 ||
        print_stream_variadic(stream, ":%s", "ok") != 3 || ftell(stream) != 10 ||
        fseek(stream, 0, SEEK_CUR) != 0 || ftell(stream) != 10 || fflush(stream) != 0) {
        fclose(stream);
        unlink(path);
        return 1;
    }
    if (fclose(stream) != 0) {
        unlink(path);
        return 1;
    }

    stream = fopen(path, "r+");
    if (stream == NULL) {
        unlink(path);
        return 2;
    }
    if (fread(actual, 1, 3, stream) != 3 || actual[0] != 'a' || actual[1] != 'b' ||
        actual[2] != 'c' || ftell(stream) != 3) {
        fclose(stream);
        unlink(path);
        return 3;
    }
    if (fseek(stream, 1, SEEK_SET) != 0 || ftell(stream) != 1) {
        fclose(stream);
        unlink(path);
        return 21;
    }
    if (fgetc(stream) != 'b' || ftell(stream) != 2) {
        fclose(stream);
        unlink(path);
        return 22;
    }
    if (fseek(stream, -1, SEEK_CUR) != 0) {
        fclose(stream);
        unlink(path);
        return 23;
    }
    long relative_position = ftell(stream);
    if (relative_position != 1) {
        fclose(stream);
        unlink(path);
        return 28;
    }
    if (fgetc(stream) != 'b') {
        fclose(stream);
        unlink(path);
        return 27;
    }
    if (fseek(stream, -2, SEEK_END) != 0 || ftell(stream) != 8) {
        fclose(stream);
        unlink(path);
        return 24;
    }
    if (fgetc(stream) != 'o' || ftell(stream) != 9) {
        fclose(stream);
        unlink(path);
        return 25;
    }
    rewind(stream);
    if (ftell(stream) != 0 || fgetc(stream) != 'a') {
        fclose(stream);
        unlink(path);
        return 18;
    }
    if (fseek(stream, 3, SEEK_SET) != 0) {
        fclose(stream);
        unlink(path);
        return 30;
    }
    if (fputc('X', stream) != 'X' || fclose(stream) != 0) {
        unlink(path);
        return 4;
    }

    stream = fopen(path, "rb+");
    if (stream == NULL || fread(actual, 1, 3, stream) != 3 ||
        memcmp(actual, "abc", 3) != 0 ||
        fgetc(stream) != 'X' || fgetc(stream) != 'e' ||
        fgetc(stream) != '-' || fgetc(stream) != '7' ||
        fgetc(stream) != ':' || fgetc(stream) != 'o' || fgetc(stream) != 'k' ||
        fgetc(stream) != EOF || !feof(stream) || ferror(stream)) {
        if (stream != NULL) fclose(stream);
        unlink(path);
        return 5;
    }
    clearerr(stream);
    if (feof(stream) || ferror(stream)) {
        fclose(stream);
        unlink(path);
        return 12;
    }
    if (fgetc(stream) != EOF || !feof(stream)) {
        fclose(stream);
        unlink(path);
        return 13;
    }
    if (fseek(stream, 0, SEEK_SET) != 0 || feof(stream) || ftell(stream) != 0 ||
        fgetc(stream) != 'a') {
        fclose(stream);
        unlink(path);
        return 19;
    }
    if (fseek(stream, 0, 12345) != EOF || !ferror(stream)) {
        fclose(stream);
        unlink(path);
        return 20;
    }
    clearerr(stream);
    if (fseek(stream, 0, SEEK_END) != 0 || fgetc(stream) != EOF || !feof(stream)) {
        fclose(stream);
        unlink(path);
        return 31;
    }
    rewind(stream);
    if (feof(stream) || ferror(stream) || ftell(stream) != 0 || fgetc(stream) != 'a') {
        fclose(stream);
        unlink(path);
        return 32;
    }
    if (fclose(stream) != 0 || unlink(path) != 0) {
        return 6;
    }

    stream = fopen("/dev/full", "w");
    if (stream == NULL || fwrite("x", 1, 1, stream) != 1 ||
        fflush(stream) != EOF || !ferror(stream)) {
        if (stream != NULL) fclose(stream);
        return 14;
    }
    if (fprintf(stream, "%s", "y") != -1 || !ferror(stream)) {
        fclose(stream);
        return 16;
    }
    clearerr(stream);
    if (ferror(stream) || fclose(stream) != EOF) {
        return 15;
    }

    errno = 0;
    if (fopen(path, "invalid") != NULL || errno != EINVAL) {
        unlink(path);
        return 7;
    }
    return 0;
}

static int check_bulk_file_streams(void)
{
    const char *path = "/tmp/iris-libc-stdio-bulk-test";
    const size_t length = 2 * BUFSIZ + 37;
    unsigned char source[2 * BUFSIZ + 37];
    unsigned char actual[2 * BUFSIZ + 42];
    char formatted[BUFSIZ + 1];
    unlink(path);

    for (size_t index = 0; index < length; index++) {
        source[index] = (unsigned char)(index * 29U + 11U);
    }
    memset(formatted, 'f', BUFSIZ);
    formatted[BUFSIZ] = '\0';
    FILE *formatted_stream = fopen(path, "w");
    if (formatted_stream == NULL ||
        fprintf(formatted_stream, "%s", formatted) != BUFSIZ ||
        fflush(formatted_stream) != 0 || fclose(formatted_stream) != 0) {
        if (formatted_stream != NULL) fclose(formatted_stream);
        unlink(path);
        return 8;
    }
    formatted_stream = fopen(path, "r");
    if (formatted_stream == NULL ||
        fread(actual, 1, BUFSIZ, formatted_stream) != BUFSIZ ||
        memcmp(actual, formatted, BUFSIZ) != 0 ||
        fgetc(formatted_stream) != EOF || fclose(formatted_stream) != 0) {
        if (formatted_stream != NULL) fclose(formatted_stream);
        unlink(path);
        return 9;
    }

    FILE *stream = fopen(path, "w");
    if (stream == NULL) return 1;
    if (fputc(source[0], stream) != source[0] ||
        fwrite(source + 1, 1, length - 1, stream) != length - 1) {
        fclose(stream);
        unlink(path);
        return 2;
    }
    if (fclose(stream) != 0) {
        unlink(path);
        return 3;
    }

    stream = fopen(path, "r");
    if (stream == NULL) {
        unlink(path);
        return 4;
    }
    if (fgetc(stream) != source[0] ||
        fread(actual + 1, 3, (length + 4) / 3, stream) != (length - 1) / 3 ||
        !feof(stream) || ferror(stream) ||
        memcmp(actual + 1, source + 1, length - 1) != 0) {
        fclose(stream);
        unlink(path);
        return 5;
    }
    if (fclose(stream) != 0) {
        unlink(path);
        return 6;
    }
    if (unlink(path) != 0) return 7;
    return 0;
}

static int check_line_streams(void)
{
    const char *path = "/tmp/iris-libc-stdio-lines-test";
    char line[32];
    unlink(path);

    FILE *stream = fopen(path, "w");
    if (stream == NULL) {
        unlink(path);
        return 1;
    }
    if (fputs("1234567\nfirst\nsecond line is long\n", stream) == EOF) {
        fclose(stream);
        unlink(path);
        return 1;
    }
    if (fclose(stream) != 0) {
        unlink(path);
        return 1;
    }

    stream = fopen(path, "r");
    if (stream == NULL) {
        unlink(path);
        return 2;
    }
    line[0] = 'x';
    if (fgets(line, 1, stream) != line || line[0] != '\0' ||
        fgets(line, 9, stream) != line || strcmp(line, "1234567\n") != 0 ||
        fgets(line, 7, stream) != line || strcmp(line, "first\n") != 0 ||
        fgets(line, 8, stream) != line || strcmp(line, "second ") != 0 ||
        fgets(line, sizeof(line), stream) != line ||
        strcmp(line, "line is long\n") != 0 ||
        fgets(line, sizeof(line), stream) != NULL || !feof(stream)) {
        fclose(stream);
        unlink(path);
        return 3;
    }
    if (fclose(stream) != 0 || unlink(path) != 0) {
        return 4;
    }

    stream = fopen(path, "w");
    if (stream == NULL) {
        unlink(path);
        return 5;
    }
    if (fclose(stream) != 0) {
        unlink(path);
        return 5;
    }
    stream = fopen(path, "r");
    if (stream == NULL || fgets(line, sizeof(line), stream) != NULL || !feof(stream)) {
        if (stream != NULL) fclose(stream);
        unlink(path);
        return 6;
    }
    if (fclose(stream) != 0 || unlink(path) != 0) {
        return 7;
    }

    if (puts("") < 0 || fflush(stdout) != 0) {
        return 8;
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
    int stdio_result = check_stdio();
    if (stdio_result != 0) {
        return 50 + stdio_result;
    }
    int file_stream_result = check_file_streams();
    if (file_stream_result != 0) {
        return 60 + file_stream_result;
    }
    int bulk_stream_result = check_bulk_file_streams();
    if (bulk_stream_result != 0) {
        return 80 + bulk_stream_result;
    }
    int line_stream_result = check_line_streams();
    if (line_stream_result != 0) {
        return 100 + line_stream_result;
    }

    return 0;
}
