// RingbufR classes
// A ring buffer and associated classes that are mostly in the style
// of the C++ Standard Template Library

#ifndef __RINGBUFR_H_
#define __RINGBUFR_H_

#include <cstddef>

// Class RingbufR
template<typename _T>
class RingbufR
{
public:

    RingbufR (size_t capacity);
    virtual ~RingbufR ();
    RingbufR(const RingbufR&) = delete;
    RingbufR() = delete;
    RingbufR(RingbufR&&) = delete;
    RingbufR& operator=(const RingbufR&) = delete;
    RingbufR& operator=(RingbufR&&) = delete;

    virtual size_t pushInquire(
        size_t& available1, _T*& start1, size_t& available2, _T*& start2) const;
    void push(size_t newContent);
    virtual size_t popInquire(
        size_t& available1, _T*& start1, size_t& available2, _T*& start2) const;
    void pop(size_t oldContent);
    size_t size() const;

    // For debugging
    const _T* ring_start() const;
    const _T* ring_end() const;
    static void validate(const _T* start, size_t count);
    struct debugState
    {
        size_t push_next;
        size_t pop_next;
        size_t pushes;
        size_t pops;
        bool  empty;
    };
    debugState getState() const;

private:

    const size_t _capacity;
    _T* _ring_start;
    _T* _ring_end;
    bool _empty;
    size_t _pushes;
    size_t _pops;
    _T* _push_next;
    _T* _pop_next;
};

#endif // __RINGBUFR_H_
