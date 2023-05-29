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

std::string my_time(void)
{
    time_t tt;
    struct tm tm;
    char buf[128];
    if ((tt = time (NULL)) == -1)
    {
        perror ("time failed");
        pthread_exit (NULL);
    }
    tm = *localtime (&tt);
    snprintf (buf, sizeof(buf), "%04d/%02d/%02d %02d:%02d:%02d",
        tm.tm_year+1900, tm.tm_mon+1, tm.tm_mday,
    tm.tm_hour, tm.tm_min, tm.tm_sec);
    return std::string(buf);
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
