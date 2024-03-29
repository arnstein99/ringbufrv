#include "iopackage.h"

#include <chrono>
using namespace std::chrono;

#ifdef VERBOSE
#include <iomanip>
#include <iostream>
#endif

IOPackageBase::IOPackageBase(
        int rdfd, int wrfd, size_t store_size, unsigned char* store)
    : readfd(rdfd), writefd(wrfd), bufr(store_size, store) { }

bool IOPackageBase::cycle(pollfd pfd[2])
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
                IOPackageReadException r(errno, bytes_copied);
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
                IOPackageWriteException w(errno, bytes_copied);
                throw(w);
            }
        }
        else if (bytes_write == 0)
        {
            // EOF on write.
            IOPackageWriteException w(0, bytes_copied);
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

iopackage_stats IOPackageBase::report() const
{
    iopackage_stats stats;
    stats.bytes_copied = bytes_copied;
    auto result = bufr.getState();
    stats.reads = result.pushes;
    stats.writes = result.pops;
    return stats;
}

#include "ringbufr.tcc"
