#ifndef __MCLEANER_H_
#define __MCLEANER_H_

#include <fcntl.h>
#include <memory>
#include <semaphore>

namespace MCleaner
{
template<typename _T>
class CleanerBase
{
public:
    CleanerBase() = delete;
    CleanerBase(const CleanerBase&) = delete;
    CleanerBase& operator=(const CleanerBase&) = delete;
    CleanerBase(CleanerBase&&) = delete;
    CleanerBase& operator=(CleanerBase&&) = delete;
protected:
    CleanerBase(_T& obj) : _obj(obj) { }
    _T& _obj;
};

struct FileCloser : public CleanerBase<int>
{
    FileCloser(int& i) : CleanerBase<int>(i) { }
    ~FileCloser() { if (_obj >= 4) close(_obj); }
};

template<std::ptrdiff_t _d>
struct SemaphoreReleaser : public CleanerBase< std::counting_semaphore<_d> >
{
    SemaphoreReleaser(std::counting_semaphore<_d>& cs) :
        CleanerBase< std::counting_semaphore<_d> >(cs) { }
    ~SemaphoreReleaser() { this->_obj.release(); }
};

}; // namespace MCleaner

#endif //  __MCLEANER_H_
