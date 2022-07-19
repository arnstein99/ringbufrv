#include "miscutils.h"
#include <iostream>
#include <cstring>

void errorexit(const char* message)
{
    std::cerr << message << ": " << strerror(errno) << std::endl;
    exit(errno);
}

int mstoi(const std::string& str)
{
    int retval = 0;
    try
    {
        retval = stoi(str);
    }
    catch (const std::invalid_argument&)
    {
        std::cerr << "Invalid integer expression \"" << str << "\"" <<
            std::endl;
        exit(1);
    }
    catch (const std::out_of_range&)
    {
        std::cerr << "out-of-range integer expression \"" << str << "\"" <<
            std::endl;
        exit(1);
    }
    return retval;
}
