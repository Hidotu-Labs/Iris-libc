#ifndef IRIS_TIME_H
#define IRIS_TIME_H

#include <sys/types.h>

#ifndef IRIS_TIME_IMPLEMENTATION
extern time_t (*__iris_time_dispatch)(time_t *timer);

static inline time_t time(time_t *timer)
{
	return __iris_time_dispatch(timer);
}
#else
time_t time(time_t *timer);
#endif

#endif