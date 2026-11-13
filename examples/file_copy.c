#include <stdio.h>

static void report_error(const char *operation, const char *path)
{
    printf("file_copy: %s: %s\n", operation, path);
    fflush(stdout);
}

int main(int argument_count, char **arguments)
{
    if (argument_count != 3) {
        printf("Usage: file_copy SOURCE DESTINATION\n");
        fflush(stdout);
        return 2;
    }

    FILE *source = fopen(arguments[1], "rb");
    if (source == NULL) {
        report_error("cannot open", arguments[1]);
        return 1;
    }

    FILE *destination = fopen(arguments[2], "wb");
    if (destination == NULL) {
        report_error("cannot create", arguments[2]);
        fclose(source);
        return 1;
    }

    unsigned char buffer[BUFSIZ];
    int failed = 0;
    size_t bytes_read;
    while ((bytes_read = fread(buffer, 1, sizeof(buffer), source)) != 0) {
        if (fwrite(buffer, 1, bytes_read, destination) != bytes_read) {
            report_error("write failed for", arguments[2]);
            failed = 1;
            break;
        }
    }
    if (ferror(source)) {
        report_error("read failed for", arguments[1]);
        failed = 1;
    }

    if (fclose(source) != 0) {
        report_error("close failed for", arguments[1]);
        failed = 1;
    }
    if (fclose(destination) != 0) {
        report_error("close failed for", arguments[2]);
        failed = 1;
    }
    return failed ? 1 : 0;
}