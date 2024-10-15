#ifndef __FD_COPY_TCC_
#define __FD_COPY_TCC_

#include "iopackage.h"

#include <algorithm>
#include <chrono>
using namespace std::chrono;
#include <cstring>
#include <poll.h>
#include <unistd.h>
#include <sys/uio.h>

template<size_t STORE_SIZE>
iopackage_stats copyfd(int readfd, int writefd)
{
    pollfd pfd[2];  // Read and write
    memset(pfd, 0, 2 * sizeof(pollfd));
    pfd[0].fd = readfd;
    pfd[1].fd = writefd;

    IOPackage<STORE_SIZE> pack(readfd, writefd);
    bool cycle_return = pack.cycle(pfd);
    while (cycle_return)
    {
        if (pfd[0].events || pfd[1].events)
        {
            int poll_return;
            NEGCHECK("poll", (poll_return = poll(pfd, 2, -1)));
            // TODO: examine pfd[*].revents ?
        }
        cycle_return = pack.cycle(pfd);
    }
    return pack.report();
}

template<size_t STORE_SIZE>
void copyfd2(
    int leftfd, int rightfd, int max_msec, iopackage_stats stats[2])
{
    pollfd pfd[4];  // Forward read and write, then backward read and write.
    memset(pfd, 0, 4 * sizeof(pollfd));

    // ugh
    int leftfd_forward, leftfd_backward, rightfd_forward, rightfd_backward;
    if (leftfd == -1)
    {
        leftfd_forward  = 0;
        leftfd_backward = 1;
    }
    else
    {
        leftfd_forward  = leftfd;
        leftfd_backward = leftfd;
    }
    if (rightfd == -1)
    {
        rightfd_forward  = 1;
        rightfd_backward = 0;
    }
    else
    {
        rightfd_forward  = rightfd;
        rightfd_backward = rightfd;
    }

    set_flags(leftfd_forward  , O_NONBLOCK);
    set_flags(leftfd_backward , O_NONBLOCK);
    set_flags(rightfd_forward , O_NONBLOCK);
    set_flags(rightfd_backward, O_NONBLOCK);
    pfd[0].fd = leftfd_forward;
    pfd[1].fd = rightfd_forward;
    pfd[2].fd = rightfd_backward;
    pfd[3].fd = leftfd_backward;
    IOPackage<STORE_SIZE> forward(leftfd_forward, rightfd_forward);
    IOPackage<STORE_SIZE> backward(rightfd_backward, leftfd_backward);

    int dur;
    time_point<system_clock> deadline;
    if (max_msec == -1)
    {
        dur = -1;
    }
    else
    {
        deadline = system_clock::now() + max_msec * 1ms;
    }

    bool cycle_return = forward.cycle(pfd) && backward.cycle(pfd+2);
    while (cycle_return)
    {
        if ((pfd[0].events || pfd[1].events) &&
            (pfd[2].events || pfd[3].events))
        {
            if (max_msec != -1)
            {
                time_point<system_clock> now = system_clock::now();
                if (now >= deadline) break;
                dur = duration_cast<milliseconds>(deadline - now).count();
            }
            int poll_return;
            NEGCHECK("poll", (poll_return = poll(pfd, 4, dur)));
            // TODO: examine pfd[*].revents ?
        }
        cycle_return = forward.cycle(pfd) && backward.cycle(pfd+2);
    }
    if (stats)
    {
        stats[0] = forward.report();
        stats[1] = backward.report();
    }
}

#endif // __FD_COPY_TCC_
