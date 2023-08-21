#ifndef __FD_COPY_TCC_
#define __FD_COPY_TCC_

#include "ringbufr.h"

#include <algorithm>
#include <chrono>
using namespace std::chrono;
#include <cstring>
#include <unistd.h>
#include <sys/uio.h>

#ifdef VERBOSE
#include <iostream>
#include <iomanip>
#endif

namespace
{
template<size_t STORE_SIZE>
class IOPackage
{
public:
    IOPackage(int rdfd, int wrfd)
        : readfd(rdfd), writefd(wrfd), bufr(STORE_SIZE, store) { }
    bool cycle(pollfd pfd[2]); // Read and write
    copyfd_stats report();

private:
    int readfd;
    int writefd;

    unsigned char store[STORE_SIZE];
    RingbufRbase<unsigned char> bufr;
    size_t bytes_copied {0};
    struct iovec readvec[2];
    struct iovec writevec[2];
    size_t read_nseg, write_nseg;
    unsigned char* read_start0;
    unsigned char* read_start1;
    unsigned char* write_start0;
    unsigned char* write_start1;
    ssize_t bytes_read{0};
    ssize_t bytes_write{0};
    bool inquire_needed{true};
};

template<size_t STORE_SIZE>
bool IOPackage<STORE_SIZE>::cycle(pollfd pfd[2])
{
    pfd[0].events = 0;
    pfd[1].events = 0;
    pfd[0].revents = 0;
    pfd[1].revents = 0;

    if (inquire_needed)
    {
        read_nseg = bufr.pushInquire(
            readvec[0].iov_len, read_start0,
            readvec[1].iov_len, read_start1);
    }
    bytes_read = 0;
    if (read_nseg)
    {
        readvec[0].iov_base = read_start0;
        readvec[1].iov_base = read_start1;
#if (VERBOSE >= 4)
        auto before = system_clock::now();
#endif
        bytes_read = readv(readfd, readvec, read_nseg);
#if (VERBOSE >= 4)
        auto after = system_clock::now();
        auto dur = duration_cast<milliseconds>(after - before).count();
        std::cerr << "read time " << dur << std::endl;
#endif
        if (bytes_read < 0)
        {
            if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
            {
                // poll() may be needed
                pfd[0].events = POLLIN;
            }
            else
            {
                // Some other error on input
                CopyFDReadException r(errno, bytes_copied);
                throw(r);
            }
        }
        else if (bytes_read == 0)
        {
            // End of input
            ;
        }
        else
        {
            // Some data was input, no need to poll.
            bufr.push(bytes_read);
        }
    }

    bytes_write = 0;
    if (inquire_needed)
    {
        write_nseg = bufr.popInquire(
            writevec[0].iov_len, write_start0,
            writevec[1].iov_len, write_start1);
    }
    if (write_nseg)
    {
        writevec[0].iov_base = write_start0;
        writevec[1].iov_base = write_start1;
#if (VERBOSE >= 4)
        auto before = system_clock::now();
#endif
        bytes_write = writev(writefd, writevec, write_nseg);
#if (VERBOSE >= 4)
        auto after = system_clock::now();
        auto dur = duration_cast<milliseconds>(after - before).count();
        std::cerr << "write time " << dur << std::endl;
#endif
        if (bytes_write < 0)
        {
            if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
            {
                // poll() may be needed
                pfd[1].events = POLLOUT;
            }
            else
            {
                // Some other error on write
                CopyFDWriteException w(errno, bytes_copied);
                throw(w);
            }
        }
        else if (bytes_write == 0)
        {
            // EOF on write.
            CopyFDWriteException w(0, bytes_copied);
            throw(w);
        }
        else
        {
            // Some data was output, no need to poll.
            bufr.pop(bytes_write);
            bytes_copied += bytes_write;
        }
    }

    // Only block if really necessary
    if (bytes_read  > 0) pfd[1].events = 0;
    if (bytes_write > 0) pfd[0].events = 0;

    // Only inquire if really necessary
    inquire_needed = ((bytes_read > 0) || (bytes_write > 0));

#if (VERBOSE >= 4)
        if (read_nseg)
        {
            std::cerr << std::setw(7) << std::left << "read" <<
                std::setw(6) << std::right << bytes_read;
        }
        else
            std::cerr << std::setw(13) << " ";
        std::cerr << "    ";
        if (write_nseg)
        {
            std::cerr << std::setw(7) << std::left << "write" <<
                std::setw(6) << std::right << bytes_write;
        }
        else
            std::cerr << std::setw(13) << "  ";
        std::cerr << "    ";
        std::cerr << (pfd[0].events ? "x" : "|");
        std::cerr << (pfd[1].events ? "x" : "|");
        std::cerr << std::endl;
#endif
        return (bytes_read || bytes_write);
}

template<size_t STORE_SIZE>
copyfd_stats IOPackage<STORE_SIZE>::report()
{
    copyfd_stats stats;
    stats.bytes_copied = bytes_copied;
    auto result = bufr.getState();
    stats.reads = result.pushes;
    stats.writes = result.pops;
    return stats;
}

} // anonymous namespace

template<size_t STORE_SIZE>
copyfd_stats copyfd(int readfd, int writefd)
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
    int readfd, int writefd, int max_msec, copyfd_stats stats[2])
{
    pollfd pfd[4];  // Forward read and write, then backward read and write.
    memset(pfd, 0, 4 * sizeof(pollfd));
    pfd[0].fd = readfd;
    pfd[1].fd = writefd;
    pfd[2].fd = writefd;
    pfd[3].fd = readfd;
    IOPackage<STORE_SIZE> forward(readfd, writefd);
    IOPackage<STORE_SIZE> backward(writefd, readfd);

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

#include "ringbufr.tcc"

#endif // __FD_COPY_TCC_
