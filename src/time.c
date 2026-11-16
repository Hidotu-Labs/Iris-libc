#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <time.h>

struct iris_timespec {
    long seconds;
    long nanoseconds;
};

static int64_t floor_divide(int64_t value, int64_t divisor)
{
    int64_t quotient = value / divisor;
    int64_t remainder = value % divisor;
    return remainder < 0 ? quotient - 1 : quotient;
}

static int64_t days_from_civil(int64_t year, int month, int day)
{
    year -= month <= 2;
    int64_t era = floor_divide(year, 400);
    unsigned int year_of_era = (unsigned int)(year - era * 400);
    unsigned int adjusted_month = (unsigned int)(month + (month > 2 ? -3 : 9));
    unsigned int day_of_year = (153U * adjusted_month + 2U) / 5U +
        (unsigned int)day - 1U;
    unsigned int day_of_era = year_of_era * 365U + year_of_era / 4U -
        year_of_era / 100U + day_of_year;
    return era * 146097 + (int64_t)day_of_era - 719468;
}

static void civil_from_days(int64_t days, int64_t *year, unsigned int *month,
    unsigned int *day)
{
    int64_t shifted = days + 719468;
    int64_t era = floor_divide(shifted, 146097);
    unsigned int day_of_era = (unsigned int)(shifted - era * 146097);
    unsigned int year_of_era = (day_of_era - day_of_era / 1460U +
        day_of_era / 36524U - day_of_era / 146096U) / 365U;
    int64_t result_year = (int64_t)year_of_era + era * 400;
    unsigned int day_of_year = day_of_era -
        (365U * year_of_era + year_of_era / 4U - year_of_era / 100U);
    unsigned int month_prime = (5U * day_of_year + 2U) / 153U;
    *day = day_of_year - (153U * month_prime + 2U) / 5U + 1U;
    *month = month_prime + (month_prime < 10U ? 3U : (unsigned int)-9);
    *year = result_year + (*month <= 2U);
}

struct tm *gmtime_r(const time_t *restrict timer, struct tm *restrict result)
{
    if (timer == NULL || result == NULL) {
        errno = EINVAL;
        return NULL;
    }

    int64_t total_seconds = (int64_t)*timer;
    int64_t days = floor_divide(total_seconds, 86400);
    int64_t seconds_in_day = total_seconds - days * 86400;
    int64_t year;
    unsigned int month;
    unsigned int day;
    civil_from_days(days, &year, &month, &day);
    if (year - 1900 < INT_MIN || year - 1900 > INT_MAX) {
        errno = EOVERFLOW;
        return NULL;
    }

    result->tm_year = (int)(year - 1900);
    result->tm_mon = (int)month - 1;
    result->tm_mday = (int)day;
    result->tm_hour = (int)(seconds_in_day / 3600);
    result->tm_min = (int)((seconds_in_day / 60) % 60);
    result->tm_sec = (int)(seconds_in_day % 60);
    int64_t weekday = (days + 4) % 7;
    result->tm_wday = (int)(weekday < 0 ? weekday + 7 : weekday);
    result->tm_yday = (int)(days - days_from_civil(year, 1, 1));
    result->tm_isdst = 0;
    return result;
}

struct tm *localtime_r(const time_t *restrict timer, struct tm *restrict result)
{
    return gmtime_r(timer, result);
}

struct tm *gmtime(const time_t *timer)
{
    static struct tm result;
    return gmtime_r(timer, &result);
}

struct tm *localtime(const time_t *timer)
{
    static struct tm result;
    return localtime_r(timer, &result);
}

time_t mktime(struct tm *time_value)
{
    if (time_value == NULL) {
        errno = EINVAL;
        return (time_t)-1;
    }
    int64_t year = (int64_t)time_value->tm_year + 1900;
    int64_t month = time_value->tm_mon;
    year += floor_divide(month, 12);
    month -= floor_divide(month, 12) * 12;

    int64_t days = days_from_civil(year, (int)month + 1, 1) +
        (int64_t)time_value->tm_mday - 1;
    int64_t seconds = days * 86400 + (int64_t)time_value->tm_hour * 3600 +
        (int64_t)time_value->tm_min * 60 + time_value->tm_sec;
    if (seconds < LONG_MIN || seconds > LONG_MAX) {
        errno = EOVERFLOW;
        return (time_t)-1;
    }
    time_t result = (time_t)seconds;
    if (gmtime_r(&result, time_value) == NULL) {
        return (time_t)-1;
    }
    return result;
}

double difftime(time_t end, time_t beginning)
{
    return (double)end - (double)beginning;
}

clock_t clock(void)
{
    struct iris_timespec value;
    long result;
#if defined(__linux__) && defined(__x86_64__)
    __asm__ volatile("syscall"
        : "=a"(result)
        : "a"(228L), "D"(2L), "S"(&value)
        : "rcx", "r11", "memory");
#elif defined(__linux__) && defined(__aarch64__)
    register long first __asm__("x0") = 2;
    register long second __asm__("x1") = (long)&value;
    register long number __asm__("x8") = 113;
    __asm__ volatile("svc 0"
        : "+r"(first)
        : "r"(second), "r"(number)
        : "memory", "cc");
    result = first;
#else
#error "Iris clock currently requires Linux on x86-64 or AArch64"
#endif
    if (result < 0) {
        errno = (int)-result;
        return (clock_t)-1;
    }
    if (value.seconds > LONG_MAX / CLOCKS_PER_SEC) {
        errno = EOVERFLOW;
        return (clock_t)-1;
    }
    return (clock_t)value.seconds * CLOCKS_PER_SEC +
        value.nanoseconds / 1000;
}

struct strftime_output {
    char *buffer;
    size_t capacity;
    size_t length;
    int failed;
};

static void append_character(struct strftime_output *output, char character)
{
    if (output->capacity == 0 || output->length >= output->capacity - 1) {
        output->failed = 1;
        return;
    }
    output->buffer[output->length++] = character;
}

static void append_string(struct strftime_output *output, const char *string)
{
    while (*string != '\0') {
        append_character(output, *string++);
    }
}

static void append_number(struct strftime_output *output, int64_t number,
    unsigned int width, char padding)
{
    char digits[24];
    unsigned int length = 0;
    int negative = number < 0;
    uint64_t magnitude = negative ? (uint64_t)(-(number + 1)) + 1U :
        (uint64_t)number;
    do {
        digits[length++] = (char)('0' + magnitude % 10U);
        magnitude /= 10U;
    } while (magnitude != 0);
    unsigned int total = length + (unsigned int)negative;
    if (negative) {
        append_character(output, '-');
    }
    while (total < width) {
        append_character(output, padding);
        total++;
    }
    while (length != 0) {
        append_character(output, digits[--length]);
    }
}

size_t strftime(char *restrict buffer, size_t capacity,
    const char *restrict format, const struct tm *restrict time_value)
{
    static const char *const weekdays_short[] = {
        "Sun", "Mon", "Tue", "Wed", "Thu", "Fri", "Sat"
    };
    static const char *const weekdays_long[] = {
        "Sunday", "Monday", "Tuesday", "Wednesday", "Thursday", "Friday", "Saturday"
    };
    static const char *const months_short[] = {
        "Jan", "Feb", "Mar", "Apr", "May", "Jun",
        "Jul", "Aug", "Sep", "Oct", "Nov", "Dec"
    };
    static const char *const months_long[] = {
        "January", "February", "March", "April", "May", "June",
        "July", "August", "September", "October", "November", "December"
    };
    if (buffer == NULL || format == NULL || time_value == NULL) {
        return 0;
    }

    struct strftime_output output = { buffer, capacity, 0, 0 };
    int64_t year = (int64_t)time_value->tm_year + 1900;
    while (*format != '\0' && !output.failed) {
        if (*format != '%') {
            append_character(&output, *format++);
            continue;
        }
        format++;
        if (*format == 'E' || *format == 'O') {
            format++;
        }
        char conversion = *format++;
        if (conversion == '\0') {
            append_character(&output, '%');
            break;
        }
        switch (conversion) {
        case '%': append_character(&output, '%'); break;
        case 'a':
            if (time_value->tm_wday >= 0 && time_value->tm_wday <= 6)
                append_string(&output, weekdays_short[time_value->tm_wday]);
            break;
        case 'A':
            if (time_value->tm_wday >= 0 && time_value->tm_wday <= 6)
                append_string(&output, weekdays_long[time_value->tm_wday]);
            break;
        case 'b': case 'h':
            if (time_value->tm_mon >= 0 && time_value->tm_mon <= 11)
                append_string(&output, months_short[time_value->tm_mon]);
            break;
        case 'B':
            if (time_value->tm_mon >= 0 && time_value->tm_mon <= 11)
                append_string(&output, months_long[time_value->tm_mon]);
            break;
        case 'c':
            if (time_value->tm_wday >= 0 && time_value->tm_wday <= 6)
                append_string(&output, weekdays_short[time_value->tm_wday]);
            append_character(&output, ' ');
            if (time_value->tm_mon >= 0 && time_value->tm_mon <= 11)
                append_string(&output, months_short[time_value->tm_mon]);
            append_character(&output, ' ');
            append_number(&output, time_value->tm_mday, 2, ' ');
            append_character(&output, ' ');
            append_number(&output, time_value->tm_hour, 2, '0');
            append_character(&output, ':');
            append_number(&output, time_value->tm_min, 2, '0');
            append_character(&output, ':');
            append_number(&output, time_value->tm_sec, 2, '0');
            append_character(&output, ' ');
            append_number(&output, year, 4, '0');
            break;
        case 'C': append_number(&output, year / 100, 2, '0'); break;
        case 'd': append_number(&output, time_value->tm_mday, 2, '0'); break;
        case 'D':
            append_number(&output, time_value->tm_mon + 1, 2, '0');
            append_character(&output, '/');
            append_number(&output, time_value->tm_mday, 2, '0');
            append_character(&output, '/');
            append_number(&output, (year % 100 + 100) % 100, 2, '0');
            break;
        case 'e': append_number(&output, time_value->tm_mday, 2, ' '); break;
        case 'F':
            append_number(&output, year, 4, '0');
            append_character(&output, '-');
            append_number(&output, time_value->tm_mon + 1, 2, '0');
            append_character(&output, '-');
            append_number(&output, time_value->tm_mday, 2, '0');
            break;
        case 'H': append_number(&output, time_value->tm_hour, 2, '0'); break;
        case 'I': {
            int hour = time_value->tm_hour % 12;
            append_number(&output, hour == 0 ? 12 : hour, 2, '0');
            break;
        }
        case 'j': append_number(&output, time_value->tm_yday + 1, 3, '0'); break;
        case 'm': append_number(&output, time_value->tm_mon + 1, 2, '0'); break;
        case 'M': append_number(&output, time_value->tm_min, 2, '0'); break;
        case 'n': append_character(&output, '\n'); break;
        case 'p': append_string(&output, time_value->tm_hour < 12 ? "AM" : "PM"); break;
        case 'r':
            append_number(&output, time_value->tm_hour % 12 == 0 ? 12 :
                time_value->tm_hour % 12, 2, '0');
            append_character(&output, ':');
            append_number(&output, time_value->tm_min, 2, '0');
            append_character(&output, ':');
            append_number(&output, time_value->tm_sec, 2, '0');
            append_character(&output, ' ');
            append_string(&output, time_value->tm_hour < 12 ? "AM" : "PM");
            break;
        case 'R':
            append_number(&output, time_value->tm_hour, 2, '0');
            append_character(&output, ':');
            append_number(&output, time_value->tm_min, 2, '0');
            break;
        case 'S': append_number(&output, time_value->tm_sec, 2, '0'); break;
        case 't': append_character(&output, '\t'); break;
        case 'T': case 'X':
            append_number(&output, time_value->tm_hour, 2, '0');
            append_character(&output, ':');
            append_number(&output, time_value->tm_min, 2, '0');
            append_character(&output, ':');
            append_number(&output, time_value->tm_sec, 2, '0');
            break;
        case 'u': append_number(&output, time_value->tm_wday == 0 ? 7 :
            time_value->tm_wday, 1, '0'); break;
        case 'U': append_number(&output,
            (time_value->tm_yday + 7 - time_value->tm_wday) / 7, 2, '0'); break;
        case 'w': append_number(&output, time_value->tm_wday, 1, '0'); break;
        case 'W': append_number(&output, (time_value->tm_yday + 7 -
            ((time_value->tm_wday + 6) % 7)) / 7, 2, '0'); break;
        case 'x':
            append_number(&output, time_value->tm_mon + 1, 2, '0');
            append_character(&output, '/');
            append_number(&output, time_value->tm_mday, 2, '0');
            append_character(&output, '/');
            append_number(&output, (year % 100 + 100) % 100, 2, '0');
            break;
        case 'y': append_number(&output, (year % 100 + 100) % 100, 2, '0'); break;
        case 'Y': append_number(&output, year, 4, '0'); break;
        case 'z': append_string(&output, "+0000"); break;
        case 'Z': append_string(&output, "UTC"); break;
        default:
            append_character(&output, '%');
            if (conversion != '\0') append_character(&output, conversion);
            break;
        }
    }
    if (capacity != 0) {
        output.buffer[output.length < capacity ? output.length : capacity - 1] = '\0';
    }
    return output.failed ? 0 : output.length;
}