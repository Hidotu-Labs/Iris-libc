#ifndef IRIS_TERMIOS_H
#define IRIS_TERMIOS_H

typedef unsigned char cc_t;
typedef unsigned int speed_t;
typedef unsigned int tcflag_t;

#define NCCS 19

struct termios {
    tcflag_t c_iflag;
    tcflag_t c_oflag;
    tcflag_t c_cflag;
    tcflag_t c_lflag;
    cc_t c_line;
    cc_t c_cc[NCCS];
};

#define BRKINT 0x0002
#define ICRNL  0x0100
#define INPCK  0x0010
#define ISTRIP 0x0020
#define IXON   0x0400
#define OPOST  0x0001
#define CS8    0x0030
#define ISIG   0x0001
#define ICANON 0x0002
#define ECHO   0x0008
#define IEXTEN 0x8000
#define VTIME 5
#define VMIN  6

#define TCSANOW   0
#define TCSADRAIN 1
#define TCSAFLUSH 2

#define TCGETS  0x5401
#define TCSETS  0x5402
#define TCSETSW 0x5403
#define TCSETSF 0x5404

int tcgetattr(int file_descriptor, struct termios *settings);
int tcsetattr(int file_descriptor, int action,
    const struct termios *settings);

#endif