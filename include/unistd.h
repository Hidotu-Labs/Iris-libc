#ifndef IRIS_UNISTD_H
#define IRIS_UNISTD_H

#include <stddef.h>
#include <sys/types.h>

#define STDIN_FILENO 0
#define STDOUT_FILENO 1
#define STDERR_FILENO 2

ssize_t read(int file_descriptor, void *buffer, size_t count);
ssize_t write(int file_descriptor, const void *buffer, size_t count);
int close(int file_descriptor);
off_t lseek(int file_descriptor, off_t offset, int whence);
_Noreturn void _exit(int status);
pid_t getpid(void);

#endif