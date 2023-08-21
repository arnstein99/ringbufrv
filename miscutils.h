#ifndef __MISCUTILS_H_
#define __MISCUTILS_H_

#include <string>
#include <vector>

// Operates on an active file descriptor.
void set_flags(int fd, int flags);

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

// For passing by value instead of by reference
template<typename _T, size_t _n>
struct ByValue
{
public:
    ByValue() { }
    ByValue(const _T other[_n]) {
        for (size_t index = 0 ; index < _n ; ++index)
            val[index] = other[index];
    }
    ByValue(const ByValue<_T, _n>& ByValue) {
        for (size_t index = 0 ; index < _n ; ++index)
            val[index]=ByValue.val[index];
    }
    ByValue& operator=(const _T other[_n]) {
        for (size_t index = 0 ; index < _n ; ++index)
            val[index]=other[index];
    }
    ByValue& operator=(const ByValue<_T,_n>& other) {
        if (this != &other) {
            for (size_t index = 0 ; index < _n ; ++index)
                val[index]=other.val[index];
        }
        return *this;
    }

          _T& operator[] (size_t index)       { return val[index]; }
    const _T& operator[] (size_t index) const { return val[index]; }

    // A bit of a hack
    _T* reference() { return val; }

private:
    _T val[_n];
};

int mstoi(const std::string& str, bool allow_zero=false);

std::string my_time(void);
class my_prefix
{
public:
    my_prefix(unsigned n=0) : num(n) { }
    const unsigned num;
private:
};
std::ostream& operator<<(std::ostream& ost, const my_prefix& mp);

bool represents_counting(const std::string& input);

std::vector<std::string> mstrtok(std::string instring, char delim);

#endif // __MISCUTILS_H_
