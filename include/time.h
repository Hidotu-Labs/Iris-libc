#ifndef IRIS_TIME_H
#define IRIS_TIME_H

#include <sys/types.h>

#define CLOCKS_PER_SEC 1000000L

typedef long clock_t;

struct tm {
	int tm_sec;
	int tm_min;
	int tm_hour;
	int tm_mday;
	int tm_mon;
	int tm_year;
	int tm_wday;
	int tm_yday;
	int tm_isdst;
};

#ifndef IRIS_TIME_IMPLEMENTATION
extern time_t (*__iris_time_dispatch)(time_t *timer);

static inline time_t time(time_t *timer)
{
	return __iris_time_dispatch(timer);
}
#else
time_t time(time_t *timer);
#endif

clock_t clock(void);
double difftime(time_t end, time_t beginning);
struct tm *gmtime(const time_t *timer);
struct tm *localtime(const time_t *timer);
struct tm *gmtime_r(const time_t *restrict timer, struct tm *restrict result);
struct tm *localtime_r(const time_t *restrict timer, struct tm *restrict result);
time_t mktime(struct tm *time_value);
size_t strftime(char *restrict buffer, size_t capacity,
	const char *restrict format, const struct tm *restrict time_value);

#endif