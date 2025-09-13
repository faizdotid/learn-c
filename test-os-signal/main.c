#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>

void signal_handler(int signal)
{
    // handle signal
    switch (signal)
    {
    case SIGINT:
        printf("Caught SIGINT\n");
        break;

    default:
        break;
    }
    exit(signal);
}
int main(int argc, char const *argv[])
{
    signal(SIGINT, signal_handler);
    while (1)
    {
        printf("hello world\n");
        sleep(1);
    }
    return 0;
}
