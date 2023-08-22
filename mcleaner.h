#ifndef __MCLEANER_H_
#define __MCLEANER_H_

#include <fcntl.h>
#include <memory>
#include <semaphore>
#include <sys/socket.h>

namespace MCleaner
{
struct CollisionException
{
};
template<typename _T>
class CleanerBase
{
public:
    CleanerBase()  : _obj(nullptr) { }
    CleanerBase& operator=(CleanerBase&& other) {
        if (_obj != nullptr) {
            CollisionException ce;
            throw(ce);
        }
        _obj = other._obj;
        other._obj = nullptr;
        return *this;
    }
    CleanerBase(CleanerBase&& other)
        : _obj(other._obj)
    {
        other._obj = nullptr;
    }
    void disable() {
        _obj = nullptr;
    }
    CleanerBase(const CleanerBase&) = delete;
    CleanerBase& operator=(const CleanerBase&) = delete;

protected:
    CleanerBase(_T& obj) : _obj(&obj) { }
    _T* _obj;
};

struct FileCloser : public CleanerBase<const int>
{
    FileCloser(const int& i) : CleanerBase<const int>(i) { }
    ~FileCloser() { if ( _obj && (*_obj > 2)) close(*_obj); }
};

struct SocketCloser : public CleanerBase<const int>
{
    SocketCloser(const int& i) : CleanerBase<const int>(i) { }
    ~SocketCloser() {
        if ( _obj && (*_obj > 2)) {
            shutdown(*_obj, SHUT_RDWR);
            close(*_obj);
        }
    }
};

template<std::ptrdiff_t _d>
struct SemaphoreReleaser : public CleanerBase< std::counting_semaphore<_d> >
{
    SemaphoreReleaser(std::counting_semaphore<_d>& cs) :
        CleanerBase< std::counting_semaphore<_d> >(cs) { }
    ~SemaphoreReleaser() { if (this->_obj) this->_obj->release(); }
};

}; // namespace MCleaner

#endif //  __MCLEANER_H_
