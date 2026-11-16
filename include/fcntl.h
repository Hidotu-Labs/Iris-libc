#ifndef IRIS_FCNTL_H
#define IRIS_FCNTL_H

#define O_RDONLY 0
#define O_WRONLY 1
#define O_RDWR 2
#define O_CREAT 0100
#define O_EXCL 0200
#define O_TRUNC 01000
#define O_APPEND 02000
#define O_DIRECTORY 0200000
#define O_TMPFILE (020000000 | O_DIRECTORY)

int open(const char *path, int flags, ...);

#endif