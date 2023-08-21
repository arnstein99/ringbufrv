#ifndef __IOPACKAGE_TCC_
#define __IOPACKAGE_TCC_

IOPackageBase::IOPackageBase(
        int rdfd, int wrfd, size_t store_size, unsigned char* store)
    : readfd(rdfd), writefd(wrfd), bufr(store_size, store) { }

#include "ringbufr.tcc"

#endif // __IOPACKAGE_TCC_
