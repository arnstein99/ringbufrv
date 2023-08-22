#ifndef __IOPACKAGE_H_
#define __IOPACKAGE_H_

#include "ringbufr.h"
#include <poll.h>
#include <sys/uio.h>

struct iopackage_stats
{
    size_t reads;
    size_t writes;
    size_t bytes_copied;
};

// For read and write errors
struct IOPackageException
{
    IOPackageException(int ern, size_t bc) : errn(ern), byte_count(bc) {}
    int errn;
    size_t byte_count;
};
struct IOPackageReadException : public IOPackageException
{
    IOPackageReadException(int ern, size_t bc) : IOPackageException(ern, bc) {}
};
struct IOPackageWriteException : public IOPackageException
{
    IOPackageWriteException(int ern, size_t bc) : IOPackageException(ern, bc) {}
};

class IOPackageBase
{
public:
    IOPackageBase(int rdfd, int wrfd, size_t store_size, unsigned char* store);
    bool cycle(pollfd pfd[2]); // Read and write
    iopackage_stats report() const;

private:
    int readfd;
    int writefd;

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
class IOPackage : public IOPackageBase
{
public:
    IOPackage(int rdfd, int wrfd)
        : IOPackageBase(rdfd, wrfd, STORE_SIZE, store) { }

private:
    unsigned char store[STORE_SIZE];
};

#endif // __IOPACKAGE_H_
