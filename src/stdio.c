#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
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
    STREAM_UNBUFFERED = 256
};

struct iris_FILE {
    int file_descriptor;
    unsigned int flags;
    size_t buffer_length;
    size_t buffer_position;
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
    if (unread != 0 && lseek(stream->file_descriptor, -(off_t)unread, SEEK_CUR) < 0) {
        stream->flags |= STREAM_ERROR;
        return -1;
    }
    stream->buffer_length = 0;
    stream->buffer_position = 0;
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
    return failed ? EOF : 0;
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
    size_t buffered = stream->buffer_length - stream->buffer_position;
    if (buffered >= requested) {
        memcpy(destination, stream->buffer + stream->buffer_position, requested);
        stream->buffer_position += requested;
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
            (stream->flags & STREAM_UNBUFFERED) != 0) {
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
        (stream->flags & STREAM_UNBUFFERED) != 0) {
        if (flush_stream(stream) != 0) return EOF;
    }
    return (unsigned char)character;
}

static inline size_t newline_offset(const unsigned char *buffer, size_t length)
{
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
