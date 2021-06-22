#include <stdio.h>
#include <stdbool.h>
#include <sys/inotify.h>
#include <sys/epoll.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <unistd.h>
#include <string.h>
#include <stdlib.h>

#include "ae.h"

int main(int argc, const char* argv[])
{
    aeEventLoop *loop = aeCreateEventLoop(64);

    aeMain(loop);


    aeDeleteEventLoop(loop);
    return 0;
}