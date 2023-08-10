#ifndef __FD_COPY_H_
#define __FD_COPY_H_
#include <unistd.h>
#include <atomic>

// For read and write errors
class CopyFDException
{
public:
    CopyFDException(int ern, size_t bc) : errn(ern), byte_count(bc) {}
    int errn;
    size_t byte_count;
};
class CopyFDReadException : public CopyFDException
{
public:
    CopyFDReadException(int ern, size_t bc) : CopyFDException(ern, bc) {}
};
class CopyFDWriteException : public CopyFDException
{
public:
    CopyFDWriteException(int ern, size_t bc) : CopyFDException(ern, bc) {}
};

struct copyfd_stats
{
    size_t reads;
    size_t writes;
    size_t bytes_copied;
};

copyfd_stats copyfd(
    int readfd, int writefd, size_t buffer_size);

copyfd_stats copyfd_while(
    int readfd, int writefd,
    const std::atomic<bool>& continue_flag, int check_msec, size_t buffer_size);

#endif // __FD_COPY_H_
