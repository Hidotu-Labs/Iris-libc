#include <errno.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

int tcgetattr(int file_descriptor, struct termios *settings)
{
    if (settings == NULL) {
        errno = EINVAL;
        return -1;
    }
    return ioctl(file_descriptor, TCGETS, settings);
}

int tcsetattr(int file_descriptor, int action, const struct termios *settings)
{
    unsigned long request;
    if (settings == NULL) {
        errno = EINVAL;
        return -1;
    }
    switch (action) {
    case TCSANOW: request = TCSETS; break;
    case TCSADRAIN: request = TCSETSW; break;
    case TCSAFLUSH: request = TCSETSF; break;
    default:
        errno = EINVAL;
        return -1;
    }
    return ioctl(file_descriptor, request, (void *)settings);
}

int isatty(int file_descriptor)
{
    struct termios settings;
    return tcgetattr(file_descriptor, &settings) == 0;
}