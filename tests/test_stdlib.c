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

int main(int argc, char **argv, char **envp)
{
    (void)argc;
    (void)argv;

    if (envp != NULL && envp[0] != NULL) {
        char *separator = strchr(envp[0], '=');
        if (separator != NULL) {
            size_t name_length = (size_t)(separator - envp[0]);
            char name[128];
            if (name_length >= sizeof(name)) {
                return 39;
            }
            memcpy(name, envp[0], name_length);
            name[name_length] = '\0';
            if (getenv(name) == NULL || strcmp(getenv(name), separator + 1) != 0) {
                return 39;
            }
        }
    }
    errno = 0;
    if (system(NULL) != 0 || system("command") != -1 || errno != ENOSYS) {
        return 41;
    }

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

    errno = 0;
    double parsed = strtod(" -12.375e2tail", &end);
    if (parsed != -1237.5 || strcmp(end, "tail") != 0 || errno != 0) {
        return 30;
    }
    parsed = strtod("0x1.8p+2", &end);
    if (parsed != 6.0 || *end != '\0') {
        return 31;
    }
    parsed = strtod(" +.5e-1!", &end);
    if (parsed != 0.05 || strcmp(end, "!") != 0) {
        return 42;
    }
    const char incomplete_exponent[] = "1e+tail";
    if (strtod(incomplete_exponent, &end) != 1.0 ||
        end != incomplete_exponent + 1) {
        return 43;
    }
    const char incomplete_hex[] = "0x";
    if (strtod(incomplete_hex, &end) != 0.0 || end != incomplete_hex + 1) {
        return 44;
    }
    errno = 0;
    parsed = strtod("1e-323", &end);
    if (parsed <= 0.0 || errno != ERANGE || *end != '\0') {
        return 45;
    }
    parsed = strtod("-INFINITY!", &end);
    if (!(parsed < -1e300) || strcmp(end, "!") != 0) {
        return 32;
    }
    parsed = strtod("nan(payload)", &end);
    if (parsed == parsed || *end != '\0') {
        return 33;
    }
    errno = 0;
    parsed = strtod("1e9999", &end);
    if (!(parsed > 1e300) || errno != ERANGE || *end != '\0') {
        return 34;
    }
    errno = 0;
    if (strtod("1e-9999", &end) != 0.0 || errno != ERANGE || *end != '\0') {
        return 35;
    }
    const char invalid_number[] = "not-a-number";
    if (strtod(invalid_number, &end) != 0.0 || end != invalid_number) {
        return 36;
    }
    if (localeconv() == NULL || strcmp(localeconv()->decimal_point, ".") != 0) {
        return 37;
    }
    sig_atomic_t signal_safe_value = 0;
    signal_safe_value++;
    if (signal_safe_value != 1) {
        return 38;
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
    time_t epoch = 0;
    struct tm utc;
    char timestamp[32];
    if (gmtime_r(&epoch, &utc) != &utc || utc.tm_year != 70 ||
        utc.tm_mon != 0 || utc.tm_mday != 1 || utc.tm_wday != 4 ||
        strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &utc) != 19 ||
        strcmp(timestamp, "1970-01-01 00:00:00") != 0 ||
        mktime(&utc) != 0 || difftime(10, 4) != 6.0 || clock() == (clock_t)-1) {
        return 40;
    }
    time_t before_epoch = -1;
    if (gmtime_r(&before_epoch, &utc) != &utc || utc.tm_year != 69 ||
        utc.tm_mon != 11 || utc.tm_mday != 31 || utc.tm_hour != 23 ||
        utc.tm_min != 59 || utc.tm_sec != 59 ||
        localtime_r(&before_epoch, &utc) != &utc ||
        strftime(timestamp, sizeof(timestamp), "%Y%m%d%H%M%S%", &utc) != 15 ||
        strcmp(timestamp, "19691231235959%") != 0) {
        return 46;
    }
    struct tm leap_day = { .tm_mday = 29, .tm_mon = 1, .tm_year = 100 };
    time_t leap_epoch = mktime(&leap_day);
    if (leap_epoch == (time_t)-1 || leap_day.tm_year != 100 ||
        leap_day.tm_mon != 1 || leap_day.tm_mday != 29 ||
        leap_day.tm_wday != 2 || leap_day.tm_yday != 59) {
        return 47;
    }
    if (strftime(timestamp, 5, "%Y-%m-%d", &utc) != 0 || timestamp[4] != '\0') {
        return 48;
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
