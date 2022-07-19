#include "copyfd.h"
#include "miscutils.h"

#include "ringbufr.h"
#include <sys/select.h>
#include <algorithm>
#include <cstring>
#include <unistd.h>

#ifdef VERBOSE
#include <iostream>
#include <iomanip>
#include <chrono>
using namespace std::chrono;
#endif // VERBOSE

copyfd_stats copyfd(
    int readfd, int writefd,
    size_t buffer_size, size_t push_pad, size_t pop_pad)
{
    std::atomic<bool> cflag(true);
    return copyfd_while(
        readfd, writefd,
        cflag, 0,
        buffer_size, push_pad, pop_pad);
}

copyfd_stats copyfd_while(
    int readfd, int writefd,
    const std::atomic<bool>& continue_flag, long check_usec,
    size_t buffer_size, size_t push_pad, size_t pop_pad)
{
    int maxfd = std::max(readfd, writefd) + 1;
    fd_set read_set;
    fd_set write_set;
    struct timeval tv;
    struct timeval* tvp = nullptr;
    if (check_usec != 0)
    {
        tv.tv_sec = 0;
        tv.tv_usec = check_usec;
        tvp = &tv;
    }


    RingbufR<unsigned char> bufr(buffer_size, push_pad, pop_pad);

    ssize_t bytes_read = 0;
    ssize_t bytes_write = 0;
    size_t bytes_copied = 0;
    size_t read_available;
    size_t write_available;
    bool l_continue = true;
    do
    {
        fd_set* p_read_set = nullptr;
        fd_set* p_write_set = nullptr;

        unsigned char* read_start;
        bufr.pushInquire(read_available, read_start);
        bytes_read = 0;
        if (read_available)
        {
#if (VERBOSE >= 3)
            auto before = system_clock::now();
#endif // VERBOSE
            bytes_read = read(readfd, read_start, read_available);
#if (VERBOSE >= 3)
            auto after = system_clock::now();
            auto dur = duration_cast<milliseconds>(after - before).count();
            std::cerr << "read time " << dur << std::endl;
#endif // VERBOSE
            if (bytes_read < 0)
            {
                if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
                {
                    // This is when to select()
                    FD_ZERO(&read_set);
                    FD_SET(readfd, &read_set);
                    p_read_set = &read_set;
                }
                else
                {
                    // Some other error on input
                    ReadException r(errno, bytes_copied);
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
                // Some data was input, no need to select.
                bufr.push(bytes_read);
            }
        }

        bytes_write = 0;
        unsigned char* write_start;
        bufr.popInquire(write_available, write_start);
        if (write_available)
        {
#if (VERBOSE >= 3)
            auto before = system_clock::now();
#endif // VERBOSE
            bytes_write = write(writefd, write_start, write_available);
#if (VERBOSE >= 3)
            auto after = system_clock::now();
            auto dur = duration_cast<milliseconds>(after - before).count();
            std::cerr << "write time " << dur << std::endl;
#endif // VERBOSE
            if (bytes_write < 0)
            {
                if ((errno == EWOULDBLOCK) || (errno == EAGAIN))
                {
                    // This is when to select().
                    FD_ZERO(&write_set);
                    FD_SET(writefd, &write_set);
                    p_write_set = &write_set;
                }
                else
                {
                    // Some other error on write
                    WriteException w(errno, bytes_copied);
                    throw(w);
                }
            }
            else if (bytes_write == 0)
            {
                // EOF on write.
                WriteException w(0, bytes_copied);
                throw(w);
            }
            else
            {
                // Some data was output, no need to select.
                bufr.pop(bytes_write);
                bytes_copied += bytes_write;
            }
        }

        // Only block if really necessary
        if (bytes_read  > 0) p_write_set = nullptr;
        if (bytes_write > 0) p_read_set  = nullptr;

        if (p_read_set || p_write_set)
        {
            // Optimization
            l_continue = continue_flag;
            int select_return;
            NEGCHECK("select",
                (select_return = select(
                    maxfd, p_read_set, p_write_set, nullptr, tvp)));
        }

#if (VERBOSE >= 2)
        if (read_available)
        {
            std::cerr << std::setw(7) << std::left << "read" <<
                std::setw(6) << std::right << bytes_read;
        }
        else
            std::cerr << std::setw(13) << " ";
        std::cerr << "    ";
        if (write_available)
        {
            std::cerr << std::setw(7) << std::left << "write" <<
                std::setw(6) << std::right << bytes_write;
        }
        else
            std::cerr << std::setw(13) << "  ";
        std::cerr << "    ";
        std::cerr << (p_read_set  ? "x" : "|");
        std::cerr << (p_write_set ? "x" : "|");
        std::cerr << std::endl;
#endif

    } while ((bytes_read || bytes_write) && l_continue);
    // Includes negative values, meaning select() was just called.

    copyfd_stats stats;
    stats.bytes_copied = bytes_copied;
    auto result = bufr.getState();
    stats.reads = result.pushes;
    stats.writes = result.pops;
    stats.limit_reads = result.limit_pushes;
    stats.limit_writes = result.limit_pops;
    stats.internal_copies = result.internal_copies;
    return stats;
}

#include "ringbufr.tcc"
