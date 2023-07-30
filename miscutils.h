#ifndef __MISCUTILS_H_
#define __MISCUTILS_H_

#include <string>
#include <vector>

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

int mstoi(const std::string& str, bool allow_zero=false);

std::string my_time(void);

bool represents_counting(const std::string& input);

std::vector<std::string> mstrtok(std::string instring, char delim);

#endif // __MISCUTILS_H_
