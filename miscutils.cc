#include "miscutils.h"
#include <iostream>
#include <sstream>
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
        if (str[0] == '-')
        {
            std::out_of_range except(str);
            throw (except);
        }
        if (!represents_natural_number(str))
        {
            std::invalid_argument except(str);
            throw (except);
        }
        retval = stoi(str);
        if (retval <= 0) // redundant, a little.
        {
            std::out_of_range except(str);
            throw (except);
        }
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

bool represents_natural_number(const std::string& input)
{
    if (input.empty()) return false;
    std::string::const_iterator it = input.begin();
    while (it != input.end())
    {
        if (!std::isdigit(*it)) return false;
        ++it;
    }
    return true;
}

std::vector<std::string> mstrtok(std::string instring, char delim)
{
    std::stringstream strm(instring);
    std::string entry;
    std::vector<std::string> result;
    while (std::getline(strm, entry, delim))
    {
        result.push_back(entry);
    }
    return result;
}
