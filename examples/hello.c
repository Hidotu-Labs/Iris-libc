#include <unistd.h>

int main(void)
{
    static const char message[] = "Hello, world!\n";
    size_t written = 0;
    const size_t message_size = sizeof(message) - 1;

    while (written < message_size) {
        ssize_t result = write(STDOUT_FILENO, message + written, message_size - written);
        if (result <= 0) {
            return 1;
        }
        written += (size_t)result;
    }

    return 0;
}