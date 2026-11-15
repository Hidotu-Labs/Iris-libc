#ifndef IRIS_SYS_IOCTL_H
#define IRIS_SYS_IOCTL_H

#include <sys/types.h>

#define TIOCGWINSZ 0x5413

struct winsize {
    unsigned short ws_row;
    unsigned short ws_col;
    unsigned short ws_xpixel;
    unsigned short ws_ypixel;
};

int ioctl(int file_descriptor, unsigned long request, ...);

#endif