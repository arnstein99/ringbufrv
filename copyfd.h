#ifndef __FD_COPY_H_
#define __FD_COPY_H_

#include "iopackage.h"  // just for definition of iopackage_stats
#include <cstddef>      // just for definition of size_t

template<size_t STORE_SIZE>
iopackage_stats copyfd(int readfd, int writefd);

template<size_t STORE_SIZE>
void copyfd2(
    int leftfd, int rightfd, int max_msec, iopackage_stats stats[2]=nullptr);

#endif // __FD_COPY_H_
