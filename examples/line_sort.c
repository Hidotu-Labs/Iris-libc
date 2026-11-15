#include <errno.h>
#include <fcntl.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct line_slice {
    const char *text;
    size_t length;
    long numeric_key;
    int has_numeric;
};

static int g_numeric = 0;
static int g_reverse = 0;

static void print_usage(FILE *stream)
{
    fprintf(stream,
        "Usage: line_sort [-n] [-r] [-u] [-c] [-l LIMIT] [-o OUTPUT] [FILE]\n"
        "  -n         sort numerically using leading integer value\n"
        "  -r         reverse sort order\n"
        "  -u         emit unique lines only\n"
        "  -c         print summary statistics to stderr\n"
        "  -l LIMIT   maximum number of lines to output\n"
        "  -o OUTPUT  write output to file instead of stdout\n"
        "  -h         show this help message\n");
}

static int compare_slices(const void *left_pointer, const void *right_pointer)
{
    const struct line_slice *left = left_pointer;
    const struct line_slice *right = right_pointer;
    int result = 0;

    if (g_numeric) {
        if (left->has_numeric != right->has_numeric) {
            result = left->has_numeric ? -1 : 1;
        } else if (left->numeric_key != right->numeric_key) {
            result = left->numeric_key < right->numeric_key ? -1 : 1;
        }
    }
    if (result == 0) {
        size_t common = left->length < right->length
            ? left->length : right->length;
        result = memcmp(left->text, right->text, common);
        if (result == 0 && left->length != right->length) {
            result = left->length < right->length ? -1 : 1;
        }
    }
    return g_reverse ? -result : result;
}

static int same_line(const struct line_slice *left, const struct line_slice *right)
{
    return left->length == right->length &&
        memcmp(left->text, right->text, left->length) == 0;
}

static char *read_stream_fd(int fd, size_t *out_size)
{
    size_t capacity = 4096;
    size_t length = 0;
    char *buffer = malloc(capacity);
    if (buffer == NULL) {
        return NULL;
    }

    for (;;) {
        if (length == capacity) {
            if (capacity > (size_t)-1 / 2) {
                free(buffer);
                return NULL;
            }
            size_t next_capacity = capacity * 2;
            char *grown = realloc(buffer, next_capacity);
            if (grown == NULL) {
                free(buffer);
                return NULL;
            }
            buffer = grown;
            capacity = next_capacity;
        }
        ssize_t got = read(fd, buffer + length, capacity - length);
        if (got < 0) {
            if (errno == EINTR) {
                continue;
            }
            free(buffer);
            return NULL;
        }
        if (got == 0) {
            break;
        }
        length += (size_t)got;
    }
    *out_size = length;
    return buffer;
}

static size_t split_lines(const char *data, size_t data_size,
    struct line_slice **out_lines)
{
    size_t capacity = 64;
    size_t count = 0;
    struct line_slice *lines = calloc(capacity, sizeof(*lines));
    if (lines == NULL) {
        fprintf(stderr, "line_sort: out of memory\n");
        exit(EXIT_FAILURE);
    }

    size_t start = 0;
    for (size_t index = 0; index <= data_size; index++) {
        if (index == data_size || data[index] == '\n') {
            if (index == data_size && start == data_size) {
                break;
            }
            size_t line_length = index - start;
            if (line_length > 0 && data[start + line_length - 1] == '\r') {
                line_length--;
            }
            if (count == capacity) {
                capacity *= 2;
                struct line_slice *grown = realloc(lines,
                    capacity * sizeof(*lines));
                if (grown == NULL) {
                    free(lines);
                    fprintf(stderr, "line_sort: out of memory\n");
                    exit(EXIT_FAILURE);
                }
                lines = grown;
            }
            lines[count].text = data + start;
            lines[count].length = line_length;
            lines[count].numeric_key = 0;
            lines[count].has_numeric = 0;
            if (g_numeric && line_length > 0) {
                char scratch[64];
                size_t copy_len = line_length < sizeof(scratch) - 1
                    ? line_length : sizeof(scratch) - 1;
                memcpy(scratch, data + start, copy_len);
                scratch[copy_len] = '\0';
                char *end_ptr = NULL;
                long value = strtol(scratch, &end_ptr, 10);
                if (end_ptr != scratch) {
                    lines[count].numeric_key = value;
                    lines[count].has_numeric = 1;
                }
            }
            count++;
            start = index + 1;
        }
    }

    *out_lines = lines;
    return count;
}

int main(int argc, char **argv)
{
    int unique = 0;
    int show_stats = 0;
    long limit = -1;
    const char *input_path = NULL;
    const char *output_path = NULL;

    for (int index = 1; index < argc; index++) {
        const char *arg = argv[index];
        if (strcmp(arg, "-n") == 0) {
            g_numeric = 1;
        } else if (strcmp(arg, "-r") == 0) {
            g_reverse = 1;
        } else if (strcmp(arg, "-u") == 0) {
            unique = 1;
        } else if (strcmp(arg, "-c") == 0) {
            show_stats = 1;
        } else if (strcmp(arg, "-h") == 0) {
            print_usage(stdout);
            return EXIT_SUCCESS;
        } else if (strcmp(arg, "-l") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "line_sort: -l requires an argument\n");
                return 2;
            }
            char *end_ptr = NULL;
            limit = strtol(argv[++index], &end_ptr, 10);
            if (*end_ptr != '\0' || limit < 0) {
                fprintf(stderr, "line_sort: invalid limit '%s'\n", argv[index]);
                return 2;
            }
        } else if (strcmp(arg, "-o") == 0) {
            if (index + 1 >= argc) {
                fprintf(stderr, "line_sort: -o requires an argument\n");
                return 2;
            }
            output_path = argv[++index];
        } else if (arg[0] == '-' && arg[1] != '\0') {
            fprintf(stderr, "line_sort: unknown option '%s'\n", arg);
            print_usage(stderr);
            return 2;
        } else if (input_path == NULL) {
            input_path = arg;
        } else {
            fprintf(stderr, "line_sort: unexpected argument '%s'\n", arg);
            return 2;
        }
    }

    int input_fd = STDIN_FILENO;
    int close_input = 0;
    if (input_path != NULL && strcmp(input_path, "-") != 0) {
        input_fd = open(input_path, O_RDONLY);
        if (input_fd < 0) {
            fprintf(stderr, "line_sort: cannot open '%s'\n", input_path);
            return EXIT_FAILURE;
        }
        close_input = 1;
    }

    const char *data = NULL;
    char *heap_data = NULL;
    void *mapped_data = MAP_FAILED;
    size_t data_size = 0;

    if (close_input) {
        off_t file_size = lseek(input_fd, 0, SEEK_END);
        if (file_size > 0 && lseek(input_fd, 0, SEEK_SET) == 0) {
            mapped_data = mmap(NULL, (size_t)file_size, PROT_READ, MAP_PRIVATE,
                input_fd, 0);
            if (mapped_data != MAP_FAILED) {
                data = (const char *)mapped_data;
                data_size = (size_t)file_size;
            }
        }
    }
    if (data == NULL) {
        heap_data = read_stream_fd(input_fd, &data_size);
        if (heap_data == NULL) {
            if (close_input) {
                close(input_fd);
            }
            fprintf(stderr, "line_sort: read failed\n");
            return EXIT_FAILURE;
        }
        data = heap_data;
    }
    if (close_input) {
        close(input_fd);
    }

    struct line_slice *lines = NULL;
    size_t line_count = split_lines(data, data_size, &lines);
    if (line_count > 1) {
        qsort(lines, line_count, sizeof(*lines), compare_slices);
    }

    FILE *out = stdout;
    if (output_path != NULL) {
        out = fopen(output_path, "wb");
        if (out == NULL) {
            fprintf(stderr, "line_sort: cannot create '%s'\n", output_path);
            free(lines);
            if (mapped_data != MAP_FAILED) {
                munmap(mapped_data, data_size);
            }
            free(heap_data);
            return EXIT_FAILURE;
        }
    }

    size_t emitted = 0;
    for (size_t index = 0; index < line_count; index++) {
        if (unique && index > 0 && same_line(&lines[index - 1], &lines[index])) {
            continue;
        }
        if (limit >= 0 && emitted >= (size_t)limit) {
            break;
        }
        if (lines[index].length > 0) {
            fwrite(lines[index].text, 1, lines[index].length, out);
        }
        fputc('\n', out);
        emitted++;
    }
    fflush(out);

    if (show_stats) {
        char summary[128];
        snprintf(summary, sizeof(summary),
            "[line_sort pid=%d] input_bytes=%zu lines=%zu emitted=%zu\n",
            (int)getpid(), data_size, line_count, emitted);
        fprintf(stderr, "%s", summary);
    }

    if (output_path != NULL && fclose(out) != 0) {
        fprintf(stderr, "line_sort: failed closing '%s'\n", output_path);
        free(lines);
        if (mapped_data != MAP_FAILED) {
            munmap(mapped_data, data_size);
        }
        free(heap_data);
        return EXIT_FAILURE;
    }

    free(lines);
    if (mapped_data != MAP_FAILED) {
        munmap(mapped_data, data_size);
    }
    free(heap_data);
    return EXIT_SUCCESS;
}
