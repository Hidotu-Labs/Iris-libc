#include <errno.h>
#include <fcntl.h>
#if defined(__SSE2__)
#include <emmintrin.h>
#endif
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/types.h>
#include <unistd.h>

enum stream_flags {
    STREAM_READ = 1,
    STREAM_WRITE = 2,
    STREAM_OWN_DESCRIPTOR = 4,
    STREAM_ACTIVE = 8,
    STREAM_EOF = 16,
    STREAM_ERROR = 32,
    STREAM_LAST_READ = 64,
    STREAM_LAST_WRITE = 128,
    STREAM_UNBUFFERED = 256,
    STREAM_LINE_BUFFERED = 512,
    STREAM_HAS_PUSHBACK = 1024
};

struct iris_FILE {
    int file_descriptor;
    unsigned int flags;
    size_t buffer_length;
    size_t buffer_position;
    unsigned char pushback_character;
    unsigned char buffer[BUFSIZ];
};

#define STREAM_POOL_SIZE 16

static FILE standard_input = { .file_descriptor = STDIN_FILENO,
    .flags = STREAM_READ | STREAM_ACTIVE | STREAM_OWN_DESCRIPTOR };
static FILE standard_output = { .file_descriptor = STDOUT_FILENO,
    .flags = STREAM_WRITE | STREAM_ACTIVE | STREAM_OWN_DESCRIPTOR };
static FILE standard_error = { .file_descriptor = STDERR_FILENO,
    .flags = STREAM_WRITE | STREAM_ACTIVE | STREAM_OWN_DESCRIPTOR | STREAM_UNBUFFERED };
static FILE stream_pool[STREAM_POOL_SIZE];

FILE *stdin = &standard_input;
FILE *stdout = &standard_output;
FILE *stderr = &standard_error;

static int stream_active(const FILE *stream)
{
    if (stream == NULL) {
        return 0;
    }
    if (stream == &standard_input || stream == &standard_output ||
        stream == &standard_error) {
        return (stream->flags & STREAM_ACTIVE) != 0;
    }
    /* A nonstandard FILE pointer must be one returned by fopen.  FILE is
     * opaque, so callers cannot construct a valid stream outside this unit. */
    return (stream->flags & STREAM_ACTIVE) != 0;
}

static int has_newline(const unsigned char *buffer, size_t length)
{
    for (size_t index = 0; index < length; index++) {
        if (buffer[index] == '\n') {
            return 1;
        }
    }
    return 0;
}

static int flush_stream(FILE *stream)
{
    size_t written = 0;

    while (written < stream->buffer_length) {
        ssize_t result = write(stream->file_descriptor,
            stream->buffer + written, stream->buffer_length - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            if (result == 0) {
                errno = EIO;
            }
            if (written != 0) {
                size_t remaining = stream->buffer_length - written;
                for (size_t index = 0; index < remaining; index++) {
                    stream->buffer[index] = stream->buffer[written + index];
                }
                stream->buffer_length = remaining;
            }
            stream->flags |= STREAM_ERROR;
            return -1;
        }
        written += (size_t)result;
    }
    stream->buffer_length = 0;
    return 0;
}

static int synchronize_input(FILE *stream)
{
    size_t unread = stream->buffer_length - stream->buffer_position;
    if ((stream->flags & STREAM_HAS_PUSHBACK) != 0) {
        unread++;
    }
    if (unread != 0 && lseek(stream->file_descriptor, -(off_t)unread, SEEK_CUR) < 0) {
        stream->flags |= STREAM_ERROR;
        return -1;
    }
    stream->buffer_length = 0;
    stream->buffer_position = 0;
    stream->flags &= ~STREAM_HAS_PUSHBACK;
    return 0;
}

static FILE *allocate_stream(void)
{
    for (size_t index = 0; index < STREAM_POOL_SIZE; index++) {
        if ((stream_pool[index].flags & STREAM_ACTIVE) == 0) {
            memset(&stream_pool[index], 0, sizeof(stream_pool[index]));
            stream_pool[index].flags = STREAM_ACTIVE | STREAM_OWN_DESCRIPTOR;
            return &stream_pool[index];
        }
    }
    errno = EMFILE;
    return NULL;
}

FILE *fopen(const char *path, const char *mode)
{
    if (path == NULL || mode == NULL || mode[0] == '\0') {
        errno = EINVAL;
        return NULL;
    }

    int flags;
    unsigned int stream_mode;
    if (mode[0] == 'r') {
        flags = O_RDONLY;
        stream_mode = STREAM_READ;
    } else if (mode[0] == 'w') {
        flags = O_WRONLY | O_CREAT | O_TRUNC;
        stream_mode = STREAM_WRITE;
    } else if (mode[0] == 'a') {
        flags = O_WRONLY | O_CREAT | O_APPEND;
        stream_mode = STREAM_WRITE;
    } else {
        errno = EINVAL;
        return NULL;
    }

    int update = 0;
    int binary = 0;
    for (size_t mode_index = 1; mode[mode_index] != '\0'; mode_index++) {
        if (mode[mode_index] == '+' && !update) {
            update = 1;
        } else if (mode[mode_index] == 'b' && !binary) {
            binary = 1;
        } else {
            errno = EINVAL;
            return NULL;
        }
    }
    if (update) {
        flags &= ~(O_RDONLY | O_WRONLY);
        flags |= O_RDWR;
        stream_mode = STREAM_READ | STREAM_WRITE;
    }

    FILE *stream = allocate_stream();
    if (stream == NULL) {
        return NULL;
    }
    int descriptor = (flags & O_CREAT) != 0
        ? open(path, flags, 0666) : open(path, flags);
    if (descriptor < 0) {
        stream->flags = 0;
        return NULL;
    }
    stream->file_descriptor = descriptor;
    stream->flags |= stream_mode;
    return stream;
}

FILE *tmpfile(void)
{
    char path[] = "/tmp/iris-tmp-XXXXXX";
    /* An unnamed file needs no directory entry: one open, and nothing to unlink. */
    int descriptor = open("/tmp", O_TMPFILE | O_RDWR, 0600);
    if (descriptor < 0) {
        descriptor = mkstemp(path);
        if (descriptor < 0) {
            return NULL;
        }
        if (unlink(path) != 0) {
            int saved_error = errno;
            close(descriptor);
            errno = saved_error;
            return NULL;
        }
    }
    FILE *stream = allocate_stream();
    if (stream == NULL) {
        close(descriptor);
        return NULL;
    }
    stream->file_descriptor = descriptor;
    stream->flags |= STREAM_READ | STREAM_WRITE;
    return stream;
}

char *tmpnam(char *buffer)
{
    static char internal_buffer[L_tmpnam];
    static uint32_t sequence;
    static const char alphabet[] = "0123456789abcdefghijklmnopqrstuvwxyz";
    char path[] = "/tmp/iris-name-XXXXXX";
    size_t length = sizeof(path) - 1U - 6U;

    /* Like the C library's tmpnam, only check that the name is free; create nothing. */
    for (unsigned int attempt = 0; attempt < 128; attempt++) {
        uint32_t value = ++sequence ^ (uint32_t)getpid() ^ (uint32_t)(attempt * 0x9e3779b9U);
        for (size_t index = 0; index < 6; index++) {
            value ^= value << 13;
            value ^= value >> 17;
            value ^= value << 5;
            path[length + index] = alphabet[value % 36U];
        }
        int descriptor = open(path, O_RDONLY);
        if (descriptor < 0) {
            if (errno != ENOENT) {
                return NULL;
            }
            char *result = buffer == NULL ? internal_buffer : buffer;
            strcpy(result, path);
            return result;
        }
        close(descriptor);
    }
    errno = EEXIST;
    return NULL;
}

int remove(const char *path)
{
    if (unlink(path) == 0) {
        return 0;
    }
    if (errno == EISDIR) {
        return rmdir(path);
    }
    return -1;
}

int fflush(FILE *stream)
{
    if (stream == NULL) {
        int failed = 0;
        if (flush_stream(&standard_output) != 0) failed = 1;
        if (flush_stream(&standard_error) != 0) failed = 1;
        for (size_t index = 0; index < STREAM_POOL_SIZE; index++) {
            FILE *candidate = &stream_pool[index];
            if ((candidate->flags & (STREAM_ACTIVE | STREAM_LAST_WRITE)) ==
                (STREAM_ACTIVE | STREAM_LAST_WRITE) && flush_stream(candidate) != 0) {
                failed = 1;
            }
        }
        return failed ? EOF : 0;
    }
    if (!stream_active(stream)) {
        errno = EBADF;
        return EOF;
    }
    if ((stream->flags & STREAM_LAST_WRITE) != 0 && flush_stream(stream) != 0) {
        return EOF;
    }
    if ((stream->flags & STREAM_READ) != 0 &&
        (stream->flags & STREAM_LAST_READ) != 0 && synchronize_input(stream) != 0) {
        return EOF;
    }
    return 0;
}

int setvbuf(FILE *restrict stream, char *restrict buffer, int mode, size_t size)
{
    (void)size;
    if (!stream_active(stream) || buffer != NULL ||
        (mode != _IOFBF && mode != _IOLBF && mode != _IONBF)) {
        errno = EINVAL;
        return -1;
    }
    if (fflush(stream) == EOF) {
        return -1;
    }
    stream->flags &= ~(STREAM_UNBUFFERED | STREAM_LINE_BUFFERED);
    if (mode == _IONBF) {
        stream->flags |= STREAM_UNBUFFERED;
    } else if (mode == _IOLBF) {
        stream->flags |= STREAM_LINE_BUFFERED;
    }
    return 0;
}

int fclose(FILE *stream)
{
    if (!stream_active(stream)) {
        errno = EBADF;
        return EOF;
    }
    int failed = 0;
    if ((stream->flags & STREAM_LAST_WRITE) != 0 && flush_stream(stream) != 0) {
        failed = 1;
    }
    if ((stream->flags & STREAM_OWN_DESCRIPTOR) != 0 &&
        close(stream->file_descriptor) != 0) {
        failed = 1;
    }
    stream->flags = 0;
    stream->buffer_length = 0;
    stream->buffer_position = 0;
    stream->flags &= ~STREAM_HAS_PUSHBACK;
    return failed ? EOF : 0;
}

FILE *freopen(const char *restrict path, const char *restrict mode,
    FILE *restrict stream)
{
    if (path == NULL || mode == NULL || !stream_active(stream)) {
        errno = EINVAL;
        return NULL;
    }
    fclose(stream);
    return fopen(path, mode);
}

int fseek(FILE *stream, long offset, int whence)
{
    if (!stream_active(stream) ||
        (stream->flags & (STREAM_READ | STREAM_WRITE)) == 0) {
        errno = EBADF;
        return EOF;
    }
    off_t converted_offset = (off_t)offset;
    if ((long)converted_offset != offset) {
        errno = EOVERFLOW;
        stream->flags |= STREAM_ERROR;
        return EOF;
    }
    if (fflush(stream) == EOF ||
        lseek(stream->file_descriptor, converted_offset, whence) < 0) {
        stream->flags |= STREAM_ERROR;
        return EOF;
    }
    stream->buffer_length = 0;
    stream->buffer_position = 0;
    stream->flags &= ~STREAM_HAS_PUSHBACK;
    stream->flags &= ~(STREAM_EOF | STREAM_LAST_READ | STREAM_LAST_WRITE);
    return 0;
}

long ftell(FILE *stream)
{
    if (!stream_active(stream) ||
        (stream->flags & (STREAM_READ | STREAM_WRITE)) == 0) {
        errno = EBADF;
        return -1L;
    }
    off_t current = lseek(stream->file_descriptor, 0, SEEK_CUR);
    if (current < 0) {
        stream->flags |= STREAM_ERROR;
        return -1L;
    }

    intmax_t logical_position = (intmax_t)current;
    if ((stream->flags & STREAM_LAST_READ) != 0) {
        size_t unread = stream->buffer_length - stream->buffer_position;
        if ((stream->flags & STREAM_HAS_PUSHBACK) != 0) {
            unread++;
        }
        if (unread > (uintmax_t)logical_position) {
            errno = EOVERFLOW;
            stream->flags |= STREAM_ERROR;
            return -1L;
        }
        logical_position -= (intmax_t)unread;
    } else if ((stream->flags & STREAM_LAST_WRITE) != 0) {
        if (stream->buffer_length > (uintmax_t)(INTMAX_MAX - logical_position)) {
            errno = EOVERFLOW;
            stream->flags |= STREAM_ERROR;
            return -1L;
        }
        logical_position += (intmax_t)stream->buffer_length;
    }
    if (logical_position > LONG_MAX || logical_position < LONG_MIN) {
        errno = EOVERFLOW;
        stream->flags |= STREAM_ERROR;
        return -1L;
    }
    return (long)logical_position;
}

void rewind(FILE *stream)
{
    if (stream_active(stream)) {
        fseek(stream, 0, SEEK_SET);
        stream->flags &= ~(STREAM_EOF | STREAM_ERROR);
    } else {
        errno = EBADF;
    }
}

size_t fread(void *buffer, size_t size, size_t count, FILE *stream)
{
    if (size == 0 || count == 0) {
        return 0;
    }
    if (buffer == NULL || !stream_active(stream) ||
        (stream->flags & STREAM_READ) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return 0;
    }
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        stream->flags |= STREAM_ERROR;
        return 0;
    }
    if ((stream->flags & STREAM_LAST_WRITE) != 0 && fflush(stream) != 0) {
        return 0;
    }
    if ((stream->flags & STREAM_LAST_READ) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_WRITE) | STREAM_LAST_READ;
    }

    size_t requested = size * count;
    size_t received = 0;
    unsigned char *destination = buffer;
    if ((stream->flags & STREAM_HAS_PUSHBACK) != 0) {
        destination[received++] = stream->pushback_character;
        stream->flags &= ~STREAM_HAS_PUSHBACK;
    }
    size_t buffered = stream->buffer_length - stream->buffer_position;
    size_t remaining_request = requested - received;
    if (buffered >= remaining_request) {
        memcpy(destination + received, stream->buffer + stream->buffer_position,
            remaining_request);
        stream->buffer_position += remaining_request;
        return count;
    }
    while (received < requested) {
        /* Large reads can go straight to the caller when no buffered bytes
         * remain.  Keep the stream buffer for small reads so fgetc and
         * subsequent calls retain their usual read-ahead behavior. */
        size_t remaining = requested - received;
        if (stream->buffer_position == stream->buffer_length &&
            remaining >= BUFSIZ) {
            ssize_t result;
            do {
                result = read(stream->file_descriptor, destination + received,
                    remaining);
            } while (result < 0 && errno == EINTR);
            if (result < 0) {
                stream->flags |= STREAM_ERROR;
                break;
            }
            if (result == 0) {
                stream->flags |= STREAM_EOF;
                break;
            }
            received += (size_t)result;
            continue;
        }
        if (stream->buffer_position == stream->buffer_length) {
            ssize_t result;
            do {
                result = read(stream->file_descriptor, stream->buffer, BUFSIZ);
            } while (result < 0 && errno == EINTR);
            if (result < 0) {
                stream->flags |= STREAM_ERROR;
                break;
            }
            if (result == 0) {
                stream->flags |= STREAM_EOF;
                break;
            }
            stream->buffer_length = (size_t)result;
            stream->buffer_position = 0;
        }
        size_t available = stream->buffer_length - stream->buffer_position;
        size_t chunk = available < remaining ? available : remaining;
        memcpy(destination + received, stream->buffer + stream->buffer_position, chunk);
        stream->buffer_position += chunk;
        received += chunk;
    }
    return received / size;
}

size_t fwrite(const void *buffer, size_t size, size_t count, FILE *stream)
{
    if (size == 0 || count == 0) {
        return 0;
    }
    if (buffer == NULL || !stream_active(stream) ||
        (stream->flags & STREAM_WRITE) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return 0;
    }
    if (count > SIZE_MAX / size) {
        errno = EOVERFLOW;
        stream->flags |= STREAM_ERROR;
        return 0;
    }
    if ((stream->flags & STREAM_LAST_READ) != 0 && synchronize_input(stream) != 0) {
        return 0;
    }
    if ((stream->flags & STREAM_LAST_WRITE) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_READ) | STREAM_LAST_WRITE;
    }

    size_t requested = size * count;
    size_t written = 0;
    const unsigned char *source = buffer;
    size_t available = BUFSIZ - stream->buffer_length;
    if (available > requested && (stream->flags & STREAM_UNBUFFERED) == 0) {
        memcpy(stream->buffer + stream->buffer_length, source, requested);
        stream->buffer_length += requested;
        if ((stream->flags & STREAM_LINE_BUFFERED) != 0 &&
            has_newline(source, requested) && flush_stream(stream) != 0) {
            return 0;
        }
        return count;
    }
    while (written < requested) {
        size_t remaining = requested - written;
        if (stream->buffer_length == 0 && remaining >= BUFSIZ) {
            ssize_t result;
            do {
                result = write(stream->file_descriptor, source + written,
                    remaining);
            } while (result < 0 && errno == EINTR);
            if (result < 0) {
                stream->flags |= STREAM_ERROR;
                break;
            }
            if (result == 0) {
                errno = EIO;
                stream->flags |= STREAM_ERROR;
                break;
            }
            written += (size_t)result;
            continue;
        }
        size_t available = BUFSIZ - stream->buffer_length;
        size_t chunk = available < remaining ? available : remaining;
        memcpy(stream->buffer + stream->buffer_length, source + written, chunk);
        stream->buffer_length += chunk;
        written += chunk;
        if (stream->buffer_length == BUFSIZ ||
            (stream->flags & STREAM_UNBUFFERED) != 0 ||
            ((stream->flags & STREAM_LINE_BUFFERED) != 0 &&
                has_newline(source + written - chunk, chunk))) {
            if (flush_stream(stream) != 0) {
                written -= chunk;
                break;
            }
        }
    }
    return written / size;
}

int fgetc(FILE *stream)
{
    if (!stream_active(stream) || (stream->flags & STREAM_READ) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return EOF;
    }
    if ((stream->flags & STREAM_LAST_WRITE) != 0 && fflush(stream) != 0) {
        return EOF;
    }
    if ((stream->flags & STREAM_LAST_READ) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_WRITE) | STREAM_LAST_READ;
    }
    if ((stream->flags & STREAM_HAS_PUSHBACK) != 0) {
        stream->flags &= ~STREAM_HAS_PUSHBACK;
        return stream->pushback_character;
    }
    if (stream->buffer_position == stream->buffer_length) {
        ssize_t result;
        do {
            result = read(stream->file_descriptor, stream->buffer, BUFSIZ);
        } while (result < 0 && errno == EINTR);
        if (result < 0) {
            stream->flags |= STREAM_ERROR;
            return EOF;
        }
        if (result == 0) {
            stream->flags |= STREAM_EOF;
            return EOF;
        }
        stream->buffer_length = (size_t)result;
        stream->buffer_position = 0;
    }
    return stream->buffer[stream->buffer_position++];
}

int getc(FILE *stream)
{
    return fgetc(stream);
}

int ungetc(int character, FILE *stream)
{
    if (character == EOF) {
        return EOF;
    }
    if (!stream_active(stream) || (stream->flags & STREAM_READ) == 0 ||
        (stream->flags & STREAM_HAS_PUSHBACK) != 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return EOF;
    }
    if ((stream->flags & STREAM_LAST_WRITE) != 0 && fflush(stream) == EOF) {
        return EOF;
    }
    if ((stream->flags & STREAM_LAST_READ) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_WRITE) | STREAM_LAST_READ;
    }
    stream->pushback_character = (unsigned char)character;
    stream->flags = (stream->flags | STREAM_HAS_PUSHBACK) & ~STREAM_EOF;
    return stream->pushback_character;
}

static inline size_t newline_offset(const unsigned char *buffer, size_t length);

ssize_t __iris_getline_impl(char **restrict line, size_t *restrict capacity,
    FILE *restrict stream, void *(*allocate)(size_t),
    void *(*resize)(void *, size_t))
{
    if (line == NULL || capacity == NULL || !stream_active(stream) ||
        (stream->flags & STREAM_READ) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return -1;
    }

    if (*line == NULL || *capacity == 0) {
        *capacity = 128;
        *line = allocate(*capacity);
        if (*line == NULL) {
            errno = ENOMEM;
            *capacity = 0;
            return -1;
        }
    }

    if ((stream->flags & STREAM_LAST_WRITE) != 0 && fflush(stream) == EOF) {
        return -1;
    }
    if ((stream->flags & STREAM_LAST_READ) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_WRITE) | STREAM_LAST_READ;
    }

    size_t length = 0;
    if ((stream->flags & STREAM_HAS_PUSHBACK) != 0) {
        unsigned char character = stream->pushback_character;
        stream->flags &= ~STREAM_HAS_PUSHBACK;
        (*line)[length++] = (char)character;
        if (character == '\n') {
            (*line)[length] = '\0';
            return (ssize_t)length;
        }
    }
    for (;;) {
        if (stream->buffer_position == stream->buffer_length) {
            ssize_t result;
            do {
                result = read(stream->file_descriptor, stream->buffer, BUFSIZ);
            } while (result < 0 && errno == EINTR);
            if (result < 0) {
                stream->flags |= STREAM_ERROR;
                return -1;
            }
            if (result == 0) {
                stream->flags |= STREAM_EOF;
                if (length == 0) return -1;
                break;
            }
            stream->buffer_length = (size_t)result;
            stream->buffer_position = 0;
        }

        unsigned char *source = stream->buffer + stream->buffer_position;
        size_t available = stream->buffer_length - stream->buffer_position;
        size_t chunk = newline_offset(source, available);
        int found_newline = chunk < available;
        if (found_newline) chunk++;
        if (chunk > SIZE_MAX - length - 1) {
            errno = ENOMEM;
            return -1;
        }
        size_t required = length + chunk + 1;
        while (*capacity < required) {
            size_t new_capacity = *capacity > SIZE_MAX / 2
                ? required : *capacity * 2;
            char *new_line = resize(*line, new_capacity);
            if (new_line == NULL) {
                errno = ENOMEM;
                return -1;
            }
            *line = new_line;
            *capacity = new_capacity;
        }

        memcpy(*line + length, source, chunk);
        length += chunk;
        stream->buffer_position += chunk;
        if (found_newline) break;
    }

    (*line)[length] = '\0';
    return (ssize_t)length;
}

int fputc(int character, FILE *stream)
{
    if (!stream_active(stream) || (stream->flags & STREAM_WRITE) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return EOF;
    }
    if ((stream->flags & STREAM_LAST_READ) != 0 && synchronize_input(stream) != 0) {
        return EOF;
    }
    if ((stream->flags & STREAM_LAST_WRITE) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_READ) | STREAM_LAST_WRITE;
    }
    stream->buffer[stream->buffer_length++] = (unsigned char)character;
    if (stream->buffer_length == BUFSIZ ||
        (stream->flags & STREAM_UNBUFFERED) != 0 ||
        ((stream->flags & STREAM_LINE_BUFFERED) != 0 && character == '\n')) {
        if (flush_stream(stream) != 0) return EOF;
    }
    return (unsigned char)character;
}

static inline size_t newline_offset(const unsigned char *buffer, size_t length)
{
#if defined(__SSE2__)
    /* Sixteen bytes per step; the first set bit of the mask locates the newline. */
    const __m128i newlines_vector = _mm_set1_epi8('\n');
    size_t offset = 0;

    while (length - offset >= 16U) {
        unsigned int mask = (unsigned int)_mm_movemask_epi8(_mm_cmpeq_epi8(
            _mm_loadu_si128((const __m128i *)(buffer + offset)), newlines_vector));

        if (mask != 0) {
            return offset + (size_t)__builtin_ctz(mask);
        }
        offset += 16U;
    }
    while (offset < length && buffer[offset] != '\n') {
        offset++;
    }
    return offset;
#else
    const size_t ones = SIZE_MAX / UCHAR_MAX;
    const size_t high_bits = ones * (UCHAR_MAX / 2U + 1U);
    const size_t newlines = ones * (size_t)'\n';
    size_t offset = 0;

    while (length - offset >= sizeof(size_t)) {
        size_t word;
        __builtin_memcpy(&word, buffer + offset, sizeof(word));
        size_t different = word ^ newlines;
        if (((different - ones) & ~different & high_bits) != 0) {
            break;
        }
        offset += sizeof(size_t);
    }
    while (offset < length && buffer[offset] != '\n') {
        offset++;
    }
    return offset;
#endif
}

char *fgets(char *buffer, int count, FILE *stream)
{
    if (buffer == NULL || !stream_active(stream) ||
        (stream->flags & STREAM_READ) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return NULL;
    }
    if (count <= 0) {
        errno = EINVAL;
        stream->flags |= STREAM_ERROR;
        return NULL;
    }
    if (count == 1) {
        buffer[0] = '\0';
        return buffer;
    }
    if ((stream->flags & STREAM_LAST_WRITE) != 0 && fflush(stream) == EOF) {
        return NULL;
    }
    if ((stream->flags & STREAM_LAST_READ) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_WRITE) | STREAM_LAST_READ;
    }

    size_t length = 0;
    size_t limit = (size_t)(count - 1);
    while (length < limit) {
        if ((stream->flags & STREAM_HAS_PUSHBACK) != 0) {
            unsigned char character = stream->pushback_character;
            stream->flags &= ~STREAM_HAS_PUSHBACK;
            buffer[length++] = (char)character;
            if (character == '\n') {
                break;
            }
            continue;
        }
        if (stream->buffer_position == stream->buffer_length) {
            ssize_t result;
            do {
                result = read(stream->file_descriptor, stream->buffer, BUFSIZ);
            } while (result < 0 && errno == EINTR);
            if (result < 0) {
                stream->flags |= STREAM_ERROR;
                if (length == 0) {
                    return NULL;
                }
                break;
            }
            if (result == 0) {
                stream->flags |= STREAM_EOF;
                if (length == 0) {
                    return NULL;
                }
                break;
            }
            stream->buffer_length = (size_t)result;
            stream->buffer_position = 0;
        }

        size_t available = stream->buffer_length - stream->buffer_position;
        size_t chunk = limit - length < available ? limit - length : available;
        size_t scanned = newline_offset(stream->buffer + stream->buffer_position, chunk);
        if (scanned < chunk) {
            scanned++;
        }
        memcpy(buffer + length, stream->buffer + stream->buffer_position, scanned);
        stream->buffer_position += scanned;
        length += scanned;
        if (stream->buffer[stream->buffer_position - 1] == '\n') {
            break;
        }
    }
    buffer[length] = '\0';
    return buffer;
}

int fputs(const char *string, FILE *stream)
{
    if (string == NULL || !stream_active(stream) ||
        (stream->flags & STREAM_WRITE) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return EOF;
    }
    size_t length = strlen(string);
    if ((stream->flags & (STREAM_UNBUFFERED | STREAM_LAST_READ)) == 0 &&
        BUFSIZ - stream->buffer_length >= length) {
        if ((stream->flags & STREAM_LAST_WRITE) == 0) {
            stream->flags |= STREAM_LAST_WRITE;
        }
        memcpy(stream->buffer + stream->buffer_length, string, length);
        stream->buffer_length += length;
        return 0;
    }
    return fwrite(string, 1, length, stream) == length ? 0 : EOF;
}

int puts(const char *string)
{
    if (string == NULL || !stream_active(stdout) ||
        (stdout->flags & STREAM_WRITE) == 0) {
        errno = EINVAL;
        if (stream_active(stdout)) stdout->flags |= STREAM_ERROR;
        return EOF;
    }
    size_t length = strlen(string);
    if ((stdout->flags & STREAM_LAST_READ) != 0 && synchronize_input(stdout) != 0) {
        return EOF;
    }
    if ((stdout->flags & STREAM_LAST_WRITE) == 0) {
        stdout->flags = (stdout->flags & ~STREAM_LAST_READ) | STREAM_LAST_WRITE;
    }
    if ((stdout->flags & STREAM_UNBUFFERED) == 0 &&
        BUFSIZ - stdout->buffer_length > length) {
        memcpy(stdout->buffer + stdout->buffer_length, string, length);
        stdout->buffer[stdout->buffer_length + length] = '\n';
        stdout->buffer_length += length + 1;
        return 0;
    }
    if (fwrite(string, 1, length, stdout) != length || fputc('\n', stdout) == EOF) {
        return EOF;
    }
    return 0;
}

int feof(FILE *stream)
{
    return stream_active(stream) && (stream->flags & STREAM_EOF) != 0;
}

int ferror(FILE *stream)
{
    return stream_active(stream) && (stream->flags & STREAM_ERROR) != 0;
}

void clearerr(FILE *stream)
{
    if (stream_active(stream)) {
        stream->flags &= ~(STREAM_EOF | STREAM_ERROR);
    }
}

enum length_modifier {
    LENGTH_DEFAULT,
    LENGTH_CHAR,
    LENGTH_SHORT,
    LENGTH_LONG,
    LENGTH_LONG_LONG,
    LENGTH_SIZE,
    LENGTH_PTRDIFF,
    LENGTH_INTMAX
};

struct format_output {
    char *destination;
    size_t capacity;
    size_t length;
    char pending[64];
    size_t pending_length;
    FILE *stream;
    int file_descriptor;
    int to_buffer;
    int to_stream;
    int failed;
};

static int flush_output(struct format_output *output)
{
    if (output->to_stream) {
        if ((output->stream->flags & STREAM_UNBUFFERED) == 0) {
            size_t copied = 0;
            while (copied < output->pending_length) {
                size_t available = BUFSIZ - output->stream->buffer_length;
                if (available == 0) {
                    if (flush_stream(output->stream) != 0) {
                        output->failed = 1;
                        return -1;
                    }
                    available = BUFSIZ;
                }
                size_t remaining = output->pending_length - copied;
                size_t chunk = remaining < available ? remaining : available;
                memcpy(output->stream->buffer + output->stream->buffer_length,
                    output->pending + copied, chunk);
                output->stream->buffer_length += chunk;
                copied += chunk;
            }
            output->pending_length = 0;
            if ((output->stream->flags & STREAM_ERROR) != 0) {
                output->failed = 1;
                return -1;
            }
            return 0;
        }
        if (fwrite(output->pending, 1, output->pending_length, output->stream) !=
            output->pending_length || ferror(output->stream)) {
            output->failed = 1;
            return -1;
        }
        output->pending_length = 0;
        return 0;
    }

    size_t written = 0;

    while (written < output->pending_length) {
        ssize_t result = write(output->file_descriptor, output->pending + written,
            output->pending_length - written);
        if (result < 0 && errno == EINTR) {
            continue;
        }
        if (result <= 0) {
            if (result == 0) {
                errno = EIO;
            }
            output->failed = 1;
            return -1;
        }
        written += (size_t)result;
    }
    output->pending_length = 0;
    return 0;
}

static inline void emit_character(struct format_output *output, char character)
{
    if (output->failed) {
        return;
    }
    if (output->length >= INT_MAX) {
        errno = EOVERFLOW;
        output->failed = 1;
        return;
    }

    if (output->to_buffer) {
        if (output->capacity != 0 && output->length < output->capacity - 1) {
            output->destination[output->length] = character;
        }
    } else {
        output->pending[output->pending_length++] = character;
        if (output->pending_length == sizeof(output->pending)) {
            flush_output(output);
        }
    }
    output->length++;
}

static inline void emit_repeat(struct format_output *output, char character, size_t count)
{
    if (!output->failed && count > (size_t)INT_MAX - output->length) {
        errno = EOVERFLOW;
        output->failed = 1;
        return;
    }
    if (!output->failed && output->to_buffer) {
        if (output->capacity != 0 && output->length < output->capacity - 1) {
            size_t available = output->capacity - 1 - output->length;
            size_t stored = count < available ? count : available;
            for (size_t index = 0; index < stored; index++) {
                output->destination[output->length + index] = character;
            }
        }
        output->length += count;
        return;
    }
    while (count-- != 0 && !output->failed) {
        emit_character(output, character);
    }
}

static inline void emit_buffer_string(struct format_output *output, const char *string,
    size_t length)
{
    if (output->failed || length == 0) {
        return;
    }
    if (length > (size_t)INT_MAX - output->length) {
        errno = EOVERFLOW;
        output->failed = 1;
        return;
    }
    if (output->to_buffer) {
        if (output->capacity != 0 && output->length < output->capacity - 1) {
            size_t available = output->capacity - 1 - output->length;
            size_t stored = length < available ? length : available;
            if (stored == 1) {
                output->destination[output->length] = string[0];
            } else {
                memcpy(output->destination + output->length, string, stored);
            }
        }
    } else {
        if (length == 1 && output->pending_length < sizeof(output->pending)) {
            output->pending[output->pending_length++] = string[0];
        } else if (output->pending_length + length <= sizeof(output->pending)) {
            memcpy(output->pending + output->pending_length, string, length);
            output->pending_length += length;
        } else {
            if (output->pending_length != 0 && flush_output(output) != 0) {
                return;
            }
            if (length <= sizeof(output->pending)) {
                memcpy(output->pending, string, length);
                output->pending_length = length;
            } else if (fwrite(string, 1, length, output->stream) != length ||
                ferror(output->stream)) {
                output->failed = 1;
                return;
            }
        }
    }
    output->length += length;
}

static inline size_t string_length(const char *string, int precision, int precision_set)
{
    if (!precision_set) {
        return strlen(string);
    }
    size_t length = 0;

    while (length < (size_t)precision && string[length] != '\0') {
        length++;
    }
    return length;
}

static inline void emit_string_field(struct format_output *output, const char *string,
    int width, int left, int precision, int precision_set)
{
    size_t length = string_length(string, precision, precision_set);
    size_t padding = width > 0 && (size_t)width > length ? (size_t)width - length : 0;

    if (padding != 0 && !left) {
        emit_repeat(output, ' ', padding);
    }
    emit_buffer_string(output, string, length);
    if (padding != 0 && left) {
        emit_repeat(output, ' ', padding);
    }
}

static intmax_t signed_argument(va_list *arguments, enum length_modifier length)
{
    switch (length) {
    case LENGTH_CHAR:
        return (signed char)va_arg(*arguments, int);
    case LENGTH_SHORT:
        return (short)va_arg(*arguments, int);
    case LENGTH_LONG:
        return va_arg(*arguments, long);
    case LENGTH_LONG_LONG:
        return va_arg(*arguments, long long);
    case LENGTH_SIZE:
        return va_arg(*arguments, ssize_t);
    case LENGTH_PTRDIFF:
        return va_arg(*arguments, ptrdiff_t);
    case LENGTH_INTMAX:
        return va_arg(*arguments, intmax_t);
    default:
        return va_arg(*arguments, int);
    }
}

static uintmax_t unsigned_argument(va_list *arguments, enum length_modifier length)
{
    switch (length) {
    case LENGTH_CHAR:
        return (unsigned char)va_arg(*arguments, int);
    case LENGTH_SHORT:
        return (unsigned short)va_arg(*arguments, int);
    case LENGTH_LONG:
        return va_arg(*arguments, unsigned long);
    case LENGTH_LONG_LONG:
        return va_arg(*arguments, unsigned long long);
    case LENGTH_SIZE:
        return va_arg(*arguments, size_t);
    case LENGTH_PTRDIFF:
        return va_arg(*arguments, uintptr_t);
    case LENGTH_INTMAX:
        return va_arg(*arguments, uintmax_t);
    default:
        return va_arg(*arguments, unsigned int);
    }
}

static void emit_integer(struct format_output *output, uintmax_t value,
    int negative, unsigned int base, int uppercase, int width, int precision,
    int precision_set, int left, int zero, int alternate, int plus, int space)
{
    char buf[80];
    char *end = buf + sizeof(buf);
    char *cursor = end;
    const char *alphabet = uppercase ? "0123456789ABCDEF" : "0123456789abcdef";
    char sign = '\0';
    char prefix[2];
    size_t prefix_length = 0;

    if (base == 10) {
        static const char two_digits[201] =
            "0001020304050607080910111213141516171819"
            "2021222324252627282930313233343536373839"
            "4041424344454647484950515253545556575859"
            "6061626364656667686970717273747576777879"
            "8081828384858687888990919293949596979899";
        while (value >= 100U) {
            unsigned int rem = (unsigned int)(value % 100U);
            value /= 100U;
            *--cursor = two_digits[rem * 2U + 1U];
            *--cursor = two_digits[rem * 2U];
        }
        if (value >= 10U) {
            unsigned int rem = (unsigned int)value;
            *--cursor = two_digits[rem * 2U + 1U];
            *--cursor = two_digits[rem * 2U];
        } else {
            *--cursor = (char)('0' + value);
        }
    } else if (base == 16) {
        do {
            *--cursor = alphabet[value & 15U];
            value >>= 4;
        } while (value != 0);
    } else {
        do {
            *--cursor = alphabet[value & 7U];
            value >>= 3;
        } while (value != 0);
    }

    size_t digit_count = (size_t)(end - cursor);
    if (precision_set && precision == 0 && digit_count == 1 && *cursor == '0') {
        cursor = end;
        digit_count = 0;
    }
    if (negative) {
        sign = '-';
    } else if (plus) {
        sign = '+';
    } else if (space) {
        sign = ' ';
    }
    if (alternate && base == 16 && digit_count != 0) {
        prefix[0] = '0';
        prefix[1] = uppercase ? 'X' : 'x';
        prefix_length = 2;
    } else if (alternate && base == 8 &&
        (digit_count == 0 || *cursor != '0') &&
        (!precision_set || (size_t)precision <= digit_count)) {
        prefix[0] = '0';
        prefix_length = 1;
    }

    size_t zero_count = precision_set && (size_t)precision > digit_count
        ? (size_t)precision - digit_count : 0;
    size_t content_length = digit_count + zero_count + prefix_length + (sign != '\0');
    size_t padding = width > 0 && (size_t)width > content_length
        ? (size_t)width - content_length : 0;
    int width_zeroes = zero && !left && !precision_set;
    size_t total_zeroes = zero_count + (width_zeroes ? padding : 0);

    if (!left && !width_zeroes && padding != 0) {
        emit_repeat(output, ' ', padding);
    }
    if (total_zeroes <= (size_t)(cursor - buf) - 3U) {
        while (total_zeroes-- != 0) {
            *--cursor = '0';
        }
        while (prefix_length != 0) {
            *--cursor = prefix[--prefix_length];
        }
        if (sign != '\0') {
            *--cursor = sign;
        }
        emit_buffer_string(output, cursor, (size_t)(end - cursor));
    } else {
        if (sign != '\0') {
            emit_character(output, sign);
        }
        for (size_t index = 0; index < prefix_length; index++) {
            emit_character(output, prefix[index]);
        }
        if (total_zeroes != 0) {
            emit_repeat(output, '0', total_zeroes);
        }
        emit_buffer_string(output, cursor, digit_count);
    }
    if (left && padding != 0) {
        emit_repeat(output, ' ', padding);
    }
}

static void append_local_character(char *buffer, size_t *length, char character)
{
    buffer[(*length)++] = character;
}

static void append_local_number(char *buffer, size_t *length,
    unsigned int number, unsigned int width)
{
    char digits[12];
    unsigned int digit_count = 0;
    do {
        digits[digit_count++] = (char)('0' + number % 10U);
        number /= 10U;
    } while (number != 0);
    while (digit_count < width) {
        append_local_character(buffer, length, '0');
        width--;
    }
    while (digit_count != 0) {
        append_local_character(buffer, length, digits[--digit_count]);
    }
}

static void normalize_decimal(long double *value, int *exponent)
{
    *exponent = 0;
    if (*value == 0.0L) {
        return;
    }
    while (*value >= 10.0L && *exponent < 400) {
        *value /= 10.0L;
        (*exponent)++;
    }
    while (*value < 1.0L && *exponent > -400) {
        *value *= 10.0L;
        (*exponent)--;
    }
}

static void generate_significant_digits(long double value, char *digits,
    size_t count, int *exponent)
{
    normalize_decimal(&value, exponent);
    for (size_t index = 0; index <= count; index++) {
        int digit = (int)value;
        if (digit < 0) digit = 0;
        if (digit > 9) digit = 9;
        if (index < count) {
            digits[index] = (char)('0' + digit);
        } else {
            digits[count] = (char)('0' + digit);
        }
        value = (value - digit) * 10.0L;
    }
    if (digits[count] >= '5') {
        size_t index = count;
        while (index != 0 && digits[index - 1] == '9') {
            digits[--index] = '0';
        }
        if (index == 0) {
            digits[0] = '1';
            for (size_t digit_index = 1; digit_index < count; digit_index++) {
                digits[digit_index] = '0';
            }
            (*exponent)++;
        } else {
            digits[index - 1]++;
        }
    }
}

static void emit_float_field(struct format_output *output, double input,
    char conversion, int width, int precision, int precision_set,
    int left, int zero, int alternate, int plus, int space)
{
    char body[800];
    char digits[700];
    size_t body_length = 0;
    int uppercase = conversion == 'F' || conversion == 'E' || conversion == 'G';
    char kind = (char)(conversion | 0x20);
    int negative = __builtin_signbit(input);
    long double magnitude = negative ? -(long double)input : (long double)input;
    char sign = negative ? '-' : (plus ? '+' : (space ? ' ' : '\0'));

    if (!precision_set) {
        precision = 6;
    }
    if (kind == 'g' && precision == 0) {
        precision = 1;
    }
    if (precision < 0 || precision > 300) {
        errno = EOVERFLOW;
        output->failed = 1;
        return;
    }

    if (__builtin_isnan(input) || __builtin_isinf(input)) {
        const char *special = __builtin_isnan(input) ? "nan" : "inf";
        for (size_t index = 0; special[index] != '\0'; index++) {
            body[body_length++] = uppercase
                ? (char)(special[index] - ('a' - 'A')) : special[index];
        }
    } else if (kind == 'e') {
        int exponent;
        size_t significant_count = (size_t)precision + 1;
        generate_significant_digits(magnitude, digits, significant_count, &exponent);
        body[body_length++] = digits[0];
        if (precision != 0 || alternate) {
            body[body_length++] = '.';
            for (size_t index = 0; index < (size_t)precision; index++) {
                body[body_length++] = digits[index + 1];
            }
        }
        body[body_length++] = uppercase ? 'E' : 'e';
        body[body_length++] = exponent < 0 ? '-' : '+';
        unsigned int exponent_magnitude = (unsigned int)(exponent < 0
            ? -exponent : exponent);
        append_local_number(body, &body_length, exponent_magnitude, 2);
    } else if (kind == 'f') {
        int exponent;
        normalize_decimal(&magnitude, &exponent);
        int integer_digits = exponent + 1;
        int places = integer_digits + precision;
        int small_round_up = places == 0 && magnitude >= 5.0L;
        if (places <= 0) {
            if (small_round_up && precision == 0) {
                body[body_length++] = '1';
            } else {
                body[body_length++] = '0';
                if (precision != 0 || alternate) {
                    body[body_length++] = '.';
                    size_t zero_count = (size_t)precision;
                    for (size_t index = 0; index < zero_count; index++) {
                        body[body_length++] = small_round_up && index + 1 == zero_count
                            ? '1' : '0';
                    }
                }
            }
        } else {
            size_t significant_count = (size_t)places;
            generate_significant_digits(magnitude, digits, significant_count,
                &exponent);
            integer_digits = exponent + 1;
            if (integer_digits <= 0) {
                body[body_length++] = '0';
            } else {
                for (int index = 0; index < integer_digits; index++) {
                    body[body_length++] = (size_t)index < significant_count
                        ? digits[index] : '0';
                }
            }
            if (precision != 0 || alternate) {
                body[body_length++] = '.';
                int leading_zeroes = integer_digits < 0 ? -integer_digits : 0;
                for (int index = 0; index < precision; index++) {
                    int digit_index = index - leading_zeroes + (integer_digits > 0
                        ? integer_digits : 0);
                    body[body_length++] = digit_index < 0 ||
                        (size_t)digit_index >= significant_count
                        ? '0' : digits[digit_index];
                }
            }
        }
    } else {
        size_t significant_count = (size_t)precision;
        int exponent;
        generate_significant_digits(magnitude, digits, significant_count, &exponent);
        int scientific = exponent < -4 || exponent >= precision;
        if (scientific) {
            body[body_length++] = digits[0];
            size_t fraction_count = significant_count - 1;
            while (!alternate && fraction_count != 0 && digits[fraction_count] == '0') {
                fraction_count--;
            }
            if (fraction_count != 0 || alternate) {
                body[body_length++] = '.';
                for (size_t index = 0; index < fraction_count; index++) {
                    body[body_length++] = digits[index + 1];
                }
            }
            body[body_length++] = uppercase ? 'E' : 'e';
            body[body_length++] = exponent < 0 ? '-' : '+';
            unsigned int exponent_magnitude = (unsigned int)(exponent < 0
                ? -exponent : exponent);
            append_local_number(body, &body_length, exponent_magnitude, 2);
        } else {
            int integer_digits = exponent + 1;
            int fraction_count = precision - integer_digits;
            if (fraction_count < 0) fraction_count = 0;
            while (!alternate && fraction_count > 0) {
                int digit_index = integer_digits + fraction_count - 1;
                if (digit_index >= 0 && (size_t)digit_index < significant_count &&
                    digits[digit_index] == '0') {
                    fraction_count--;
                } else {
                    break;
                }
            }
            if (integer_digits <= 0) {
                body[body_length++] = '0';
            } else {
                for (int index = 0; index < integer_digits; index++) {
                    body[body_length++] = (size_t)index < significant_count
                        ? digits[index] : '0';
                }
            }
            if (fraction_count != 0 || alternate) {
                body[body_length++] = '.';
                int leading_zeroes = integer_digits < 0 ? -integer_digits : 0;
                for (int index = 0; index < fraction_count; index++) {
                    int digit_index = index - leading_zeroes +
                        (integer_digits > 0 ? integer_digits : 0);
                    body[body_length++] = digit_index < 0 ||
                        (size_t)digit_index >= significant_count
                        ? '0' : digits[digit_index];
                }
            }
        }
    }

    size_t content_length = body_length + (sign != '\0');
    size_t padding = width > 0 && (size_t)width > content_length
        ? (size_t)width - content_length : 0;
    if (!left && !zero) emit_repeat(output, ' ', padding);
    if (sign != '\0') emit_character(output, sign);
    if (!left && zero) emit_repeat(output, '0', padding);
    emit_buffer_string(output, body, body_length);
    if (left) emit_repeat(output, ' ', padding);
}

static int render_format(struct format_output *output, const char *format,
    va_list *arguments)
{
    while (*format != '\0' && !output->failed) {
        if (*format != '%') {
            if (format[1] == '%' || format[1] == '\0') {
                emit_character(output, *format++);
                continue;
            }
            const char *literal_start = format;
            do {
                format++;
            } while (*format != '\0' && *format != '%');
            emit_buffer_string(output, literal_start, (size_t)(format - literal_start));
            continue;
        }
        format++;

        int left = 0;
        int plus = 0;
        int space = 0;
        int alternate = 0;
        int zero = 0;
        for (;;) {
            if (*format == '-') left = 1;
            else if (*format == '+') plus = 1;
            else if (*format == ' ') space = 1;
            else if (*format == '#') alternate = 1;
            else if (*format == '0') zero = 1;
            else break;
            format++;
        }

        int width = 0;
        if (*format == '*') {
            width = va_arg(*arguments, int);
            format++;
            if (width < 0) {
                left = 1;
                width = width == INT_MIN ? INT_MAX : -width;
            }
        } else {
            while (*format >= '0' && *format <= '9') {
                int digit = *format++ - '0';
                width = width > (INT_MAX - digit) / 10
                    ? INT_MAX : width * 10 + digit;
            }
        }

        int precision = 0;
        int precision_set = 0;
        if (*format == '.') {
            precision_set = 1;
            format++;
            if (*format == '*') {
                precision = va_arg(*arguments, int);
                format++;
                if (precision < 0) {
                    precision_set = 0;
                }
            } else {
                while (*format >= '0' && *format <= '9') {
                    int digit = *format++ - '0';
                    precision = precision > (INT_MAX - digit) / 10
                        ? INT_MAX : precision * 10 + digit;
                }
            }
        }

        enum length_modifier length = LENGTH_DEFAULT;
        if (*format == 'h') {
            format++;
            if (*format == 'h') {
                length = LENGTH_CHAR;
                format++;
            } else {
                length = LENGTH_SHORT;
            }
        } else if (*format == 'l') {
            format++;
            if (*format == 'l') {
                length = LENGTH_LONG_LONG;
                format++;
            } else {
                length = LENGTH_LONG;
            }
        } else if (*format == 'z') {
            length = LENGTH_SIZE;
            format++;
        } else if (*format == 't') {
            length = LENGTH_PTRDIFF;
            format++;
        } else if (*format == 'j') {
            length = LENGTH_INTMAX;
            format++;
        } else if (*format == 'L') {
            errno = EINVAL;
            output->failed = 1;
            break;
        }

        if (*format == '\0') {
            errno = EINVAL;
            output->failed = 1;
            break;
        }
        char conversion = *format++;
        if (conversion == 'd' || conversion == 'i') {
            intmax_t signed_value = signed_argument(arguments, length);
            int negative = signed_value < 0;
            uintmax_t magnitude = negative
                ? (uintmax_t)(-(signed_value + 1)) + 1 : (uintmax_t)signed_value;
            emit_integer(output, magnitude, negative, 10, 0, width, precision,
                precision_set, left, zero, 0, plus, space);
        } else if (conversion == 'u' || conversion == 'o' ||
            conversion == 'x' || conversion == 'X') {
            unsigned int base = conversion == 'o' ? 8U
                : (conversion == 'u' ? 10U : 16U);
            emit_integer(output, unsigned_argument(arguments, length), 0, base,
                conversion == 'X', width, precision, precision_set,
                left, zero, alternate, 0, 0);
        } else if (conversion == 'p') {
            uintptr_t pointer = (uintptr_t)va_arg(*arguments, void *);
            emit_integer(output, pointer, 0, 16, 0, width, precision,
                precision_set, left, zero, 1, 0, 0);
        } else if (conversion == 'c') {
            char character = (char)va_arg(*arguments, int);
            size_t padding = width > 1 ? (size_t)width - 1 : 0;
            if (!left) emit_repeat(output, ' ', padding);
            emit_character(output, character);
            if (left) emit_repeat(output, ' ', padding);
        } else if (conversion == 's') {
            const char *string = va_arg(*arguments, const char *);
            if (string == NULL) string = "(null)";
            emit_string_field(output, string, width, left, precision, precision_set);
        } else if (conversion == 'f' || conversion == 'F' || conversion == 'e' ||
            conversion == 'E' || conversion == 'g' || conversion == 'G') {
            double value = va_arg(*arguments, double);
            emit_float_field(output, value, conversion, width, precision,
                precision_set, left, zero, alternate, plus, space);
        } else if (conversion == '%') {
            size_t padding = width > 1 ? (size_t)width - 1 : 0;
            if (!left) emit_repeat(output, ' ', padding);
            emit_character(output, '%');
            if (left) emit_repeat(output, ' ', padding);
        } else {
            errno = EINVAL;
            output->failed = 1;
        }
    }
    return output->failed ? -1 : (int)output->length;
}

static int finish_output(struct format_output *output)
{
    if (output->to_buffer) {
        if (output->capacity != 0) {
            size_t terminator = output->length < output->capacity
                ? output->length : output->capacity - 1;
            output->destination[terminator] = '\0';
        }
    } else if (!output->failed) {
        flush_output(output);
    }
    return output->failed ? -1 : (int)output->length;
}

int vsnprintf(char *buffer, size_t size, const char *format, va_list arguments)
{
    if (size != 0 && buffer == NULL) {
        errno = EINVAL;
        return -1;
    }
    struct format_output output;
    output.destination = buffer;
    output.capacity = size;
    output.length = 0;
    output.pending_length = 0;
    output.stream = NULL;
    output.file_descriptor = -1;
    output.to_buffer = 1;
    output.to_stream = 0;
    output.failed = 0;
    va_list copied_arguments;
    va_copy(copied_arguments, arguments);
    render_format(&output, format, &copied_arguments);
    va_end(copied_arguments);
    return finish_output(&output);
}

int snprintf(char *buffer, size_t size, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsnprintf(buffer, size, format, arguments);
    va_end(arguments);
    return result;
}

int vsprintf(char *restrict buffer, const char *restrict format,
    va_list arguments)
{
    return vsnprintf(buffer, SIZE_MAX, format, arguments);
}

int sprintf(char *restrict buffer, const char *restrict format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vsprintf(buffer, format, arguments);
    va_end(arguments);
    return result;
}

int vfprintf(FILE *stream, const char *format, va_list arguments)
{
    if (!stream_active(stream) || (stream->flags & STREAM_WRITE) == 0) {
        errno = EINVAL;
        if (stream_active(stream)) stream->flags |= STREAM_ERROR;
        return -1;
    }
    if ((stream->flags & STREAM_LAST_READ) != 0 && synchronize_input(stream) != 0) {
        return -1;
    }
    if ((stream->flags & STREAM_LAST_WRITE) == 0) {
        stream->flags = (stream->flags & ~STREAM_LAST_READ) | STREAM_LAST_WRITE;
    }
    struct format_output output;
    output.destination = NULL;
    output.capacity = 0;
    output.length = 0;
    output.pending_length = 0;
    output.stream = stream;
    output.file_descriptor = -1;
    output.to_buffer = 0;
    output.to_stream = 1;
    output.failed = 0;
    va_list copied_arguments;
    va_copy(copied_arguments, arguments);
    render_format(&output, format, &copied_arguments);
    va_end(copied_arguments);
    return finish_output(&output);
}

int fprintf(FILE *stream, const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stream, format, arguments);
    va_end(arguments);
    return result;
}

int vprintf(const char *format, va_list arguments)
{
    return vfprintf(stdout, format, arguments);
}

int printf(const char *format, ...)
{
    va_list arguments;
    va_start(arguments, format);
    int result = vfprintf(stdout, format, arguments);
    va_end(arguments);
    return result;
}
