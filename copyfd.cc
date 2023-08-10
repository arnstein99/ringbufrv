#include "copyfd.h"
#include "miscutils.h"

#include "ringbufr.h"
#include <sys/poll.h>
#include <sys/uio.h>
#include <algorithm>
#include <cstring>
#include <unistd.h>

#ifdef VERBOSE
#include <iostream>
#include <iomanip>
#include <chrono>
using namespace std::chrono;
#endif

copyfd_stats copyfd(
    int readfd, int writefd, size_t buffer_size)
{
    std::atomic<bool> cflag(true);
    return copyfd_while(readfd, writefd, cflag, -1, buffer_size);
}

copyfd_stats copyfd_while(
    int readfd, int writefd,
    const std::atomic<bool>& continue_flag, int check_msec, size_t buffer_size)
{
    pollfd pfd[2];  // Read and write
    memset(pfd, 0, 2 * sizeof(pollfd));
    pfd[0].fd = readfd;
    pfd[0].events = POLLIN;
    pfd[1].fd = writefd;
    pfd[1].events = POLLOUT;

    RingbufR<unsigned char> bufr(buffer_size);

    size_t bytes_copied = 0;
    struct iovec readvec[2];
    struct iovec writevec[2];
    size_t read_nseg, write_nseg;
    unsigned char* read_start0;
    unsigned char* read_start1;
    unsigned char* write_start0;
    unsigned char* write_start1;

    ssize_t bytes_read = 0;
    ssize_t bytes_write = 0;
    bool inquire_needed = true;
    bool poll_needed = false;
    bool read_possible = true;
    bool write_possible = true;
    do
    {
        if (inquire_needed) {
            read_nseg = bufr.pushInquire(
                readvec[0].iov_len, read_start0,
                readvec[1].iov_len, read_start1);
        }
        bytes_read = 0;
        if (read_nseg && read_possible)
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
            if (bytes_read > 0)
                std::cerr << "read time " << dur << std::endl;
#endif
            if (bytes_read < 0)
            {
                if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
                {
                    // poll() may be needed
                    ;
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
        if (write_nseg && write_possible)
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
            if (bytes_write > 0)
                std::cerr << "write time " << dur << std::endl;
#endif
            if (bytes_write < 0)
            {
                if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
                {
                    // poll() may be needed
                    ;
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

        if (poll_needed)
        {
            int poll_return;
            NEGCHECK("poll", (poll_return = poll(pfd, 2, check_msec)));
            if (poll_return == 0)
            {
                // timeout
            }
            else
            {
                // Some data can be moved
                read_possible  = (pfd[0].revents & POLLIN );
                write_possible = (pfd[0].revents & POLLOUT);
            }
        }

#if (VERBOSE >= 4)
        if ((bytes_read > 0) || (bytes_write > 0))
        {
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
            std::cerr << ((bytes_read  > 0) ? "|" : "x");
            std::cerr << ((bytes_write > 0) ? "|" : "x");
            std::cerr << std::endl;
        }
#endif

        // Only inquire  and poll if really necessary
        inquire_needed =  ((bytes_read > 0) || (bytes_write > 0));
        poll_needed    =  ((bytes_read < 0) && (bytes_write < 0));

    } while ((bytes_read || bytes_write) && continue_flag);
    // Includes negative values, meaning select() was just called.

    copyfd_stats stats;
    stats.bytes_copied = bytes_copied;
    auto result = bufr.getState();
    stats.reads = result.pushes;
    stats.writes = result.pops;
    return stats;
}

#include "ringbufr.tcc"
