#ifndef IRIS_SIGNAL_H
#define IRIS_SIGNAL_H

typedef int sig_atomic_t;
typedef void (*sighandler_t)(int);

#define SIGINT 2
#define SIGTERM 15
#define SIGWINCH 28
#define SIG_DFL ((sighandler_t)0)
#define SIG_IGN ((sighandler_t)1)
#define SIG_ERR ((sighandler_t)-1)

sighandler_t signal(int signal_number, sighandler_t handler);

#endif