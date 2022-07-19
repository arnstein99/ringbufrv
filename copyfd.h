#ifndef __FD_COPY_H_
#define __FD_COPY_H_
#include <unistd.h>
#include <atomic>

// For read and write errors
class IOException
{
public:
    IOException(int ern, size_t bc) : errn(ern), byte_count(bc) {}
    int errn;
    size_t byte_count;
};
class ReadException : public IOException
{
public:
    ReadException(int ern, size_t bc) : IOException(ern, bc) {}
};
class WriteException : public IOException
{
public:
    WriteException(int ern, size_t bc) : IOException(ern, bc) {}
};

struct copyfd_stats
{
    size_t internal_copies;
    size_t reads;
    size_t writes;
    size_t limit_reads;
    size_t limit_writes;
    size_t bytes_copied;
};

copyfd_stats copyfd(
    int readfd, int writefd,
    size_t buffer_size, size_t push_pad=0, size_t pop_pad=0);

copyfd_stats copyfd_while(
    int readfd, int writefd,
    const std::atomic<bool>& continue_flag, long check_usec,
    size_t buffer_size, size_t push_pad=0, size_t pop_pad=0);

#endif // __FD_COPY_H_
