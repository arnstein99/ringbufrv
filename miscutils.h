#ifndef __MISCUTILS_H_
#define __MISCUTILS_H_

#include <string>

void errorexit(const char* message);
#define ZEROCHECK(message,retval) \
    do { \
    if ((retval) != 0) \
        errorexit(message); \
    } while (false)
#define NEGCHECK(message,retval) \
    do { \
    if ((retval) < 0) \
        errorexit(message); \
    } while (false)

int mstoi(const std::string& str);

#endif // __MISCUTILS_H_
