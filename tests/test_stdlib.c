#include <errno.h>
#include <ctype.h>
#include <limits.h>
#include <locale.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include <fcntl.h>
#include <signal.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

static volatile int exit_handler_state;

static void set_exit_handler_state(void)
{
    exit_handler_state = 1;
}

static void verify_exit_handler_order(void)
{
    _Exit(exit_handler_state == 1 ? 0 : 99);
}

static void test_signal_handler(int signal_number)
{
    (void)signal_number;
}
static int compare_ints(const void *left, const void *right)
{
    int a = *(const int *)left;
    int b = *(const int *)right;
    return (a > b) - (a < b);
}

int main(void)
{
    char *small = malloc(24);
    if (small == NULL) {
        return 1;
    }
    memcpy(small, "iris-malloc", 12);
    small = realloc(small, 64);
    if (small == NULL || strcmp(small, "iris-malloc") != 0) {
        return 2;
    }
    free(small);

    int *zeroed = calloc(16, sizeof(int));
    if (zeroed == NULL) {
        return 3;
    }
    for (size_t i = 0; i < 16; i++) {
        if (zeroed[i] != 0) {
            return 4;
        }
    }
    free(zeroed);

    char *large = malloc(128 * 1024);
    if (large == NULL) {
        return 5;
    }
    memset(large, 'z', 128 * 1024);
    if (large[0] != 'z' || large[128 * 1024 - 1] != 'z') {
        return 6;
    }
    free(large);

    char *end = NULL;
    if (atoi("  -42") != -42) {
        return 7;
    }
    if (strtol("0x1f", &end, 0) != 31 || *end != '\0') {
        return 8;
    }
    if (strtoul("18446744073709551615", &end, 10) != ULONG_MAX) {
        return 9;
    }

    int values[] = { 9, -3, 7, 1, 5, 0, -8, 4, 2, 6 };
    size_t count = sizeof(values) / sizeof(values[0]);
    qsort(values, count, sizeof(values[0]), compare_ints);
    for (size_t i = 1; i < count; i++) {
        if (values[i - 1] > values[i]) {
            return 10;
        }
    }

    if (abs(-123) != 123 || labs(-123456L) != 123456L) {
        return 11;
    }

    errno = 0;
    if (errno != 0) {
        return 12;
    }
    errno = ERANGE;
    if (errno != ERANGE) {
        return 13;
    }
    errno = 0;
    if (strtol("1", &end, 1) != 0 || errno != EINVAL) {
        return 14;
    }

    if (!isalnum('7') || !isalnum('z') || isalnum('_') ||
        !isalpha('A') || !isalpha('z') || isalpha('7') ||
        !isblank(' ') || !isblank('\t') || isblank('\n') ||
        !iscntrl('\n') || !iscntrl(0x7f) || iscntrl(' ') ||
        !isdigit('0') || isdigit('x') ||
        !isgraph('!') || isgraph(' ') ||
        !islower('z') || islower('Z') ||
        !isprint(' ') || !isprint('~') || isprint(0x7f) ||
        !ispunct('!') || ispunct('A') ||
        !isspace('\n') || !isspace(' ') || isspace('0') ||
        !isupper('Z') || isupper('z') ||
        !isxdigit('f') || !isxdigit('A') || !isxdigit('9') || isxdigit('g') ||
        tolower('Q') != 'q' || tolower('q') != 'q' ||
        toupper('q') != 'Q' || toupper('Q') != 'Q' ||
        isalnum(-1) || isalpha(-1) || isblank(-1) || iscntrl(-1) ||
        isdigit(-1) || isgraph(-1) || islower(-1) || isprint(-1) ||
        ispunct(-1) || isspace(-1) || isupper(-1) || isxdigit(-1) ||
        tolower(-1) != -1 || toupper(-1) != -1 ||
        isalpha(0xe9)) {
        return 15;
    }

    if (setlocale(LC_CTYPE, NULL) == NULL ||
        strcmp(setlocale(LC_CTYPE, NULL), "C") != 0 || isalpha(0xc9)) {
        return 16;
    }
    if (setlocale(LC_CTYPE, "en_US.ISO-8859-1") == NULL ||
        !isalpha(0xc9) || !isupper(0xc9) || !islower(0xe9) ||
        !isalnum(0xdf) || !isalpha(0xdf) || isalpha(0xd7) ||
        !ispunct(0xd7) || ispunct(0xe9) ||
        !iscntrl(0x85) || iscntrl(0xa0) || !isprint(0xa0) ||
        !isblank(0xa0) || !isspace(0xa0)) {
        return 17;
    }
    if (tolower(0xc9) != 0xe9 || tolower(0xd8) != 0xf8 ||
        toupper(0xe9) != 0xc9 || toupper(0xfe) != 0xde ||
        toupper(0xdf) != 0xdf || toupper(0xff) != 0xff) {
        return 18;
    }
    if (strcmp(setlocale(LC_ALL, NULL), "en_US.ISO-8859-1") != 0 ||
        setlocale(LC_ALL, "C") == NULL || isalpha(0xc9) ||
        setlocale(LC_ALL, "en_US.ISO-8859-1") == NULL || !isalpha(0xc9) ||
        setlocale(LC_CTYPE, "unsupported_LOCALE") != NULL || !isalpha(0xc9)) {
        return 19;
    }
    if (setlocale(LC_CTYPE, "C") == NULL || isalpha(0xc9)) {
        return 20;
    }
    if (setlocale(-1, "C") != NULL) {
        return 21;
    }

    struct winsize window_size;
    struct termios terminal_settings;
    errno = 0;
    if (ioctl(-1, TIOCGWINSZ, &window_size) != -1 || errno != EBADF) {
        return 22;
    }
    errno = 0;
    if (isatty(-1) != 0 || errno != EBADF) {
        return 23;
    }
    errno = 0;
    if (tcgetattr(-1, &terminal_settings) != -1 || errno != EBADF) {
        return 24;
    }
    errno = 0;
    if (tcsetattr(-1, TCSAFLUSH, &terminal_settings) != -1 || errno != EBADF) {
        return 25;
    }
    errno = 0;
    if (ftruncate(-1, 0) != -1 || errno != EBADF) {
        return 26;
    }
    if (time(NULL) <= 0) {
        return 27;
    }
    sighandler_t old_handler = signal(SIGWINCH, test_signal_handler);
    if (old_handler == SIG_ERR || signal(SIGWINCH, old_handler) != test_signal_handler) {
        return 28;
    }

    printf("stdlib-nostdlib: ok\n");
    if (atexit(verify_exit_handler_order) != 0 ||
        atexit(set_exit_handler_state) != 0) {
        return 29;
    }
    exit(21);
}
