// RingbufR classes
// A ring buffer and associated classes that are mostly in the style
// of the C++ Standard Template Library

#ifndef __RINGBUFR_H_
#define __RINGBUFR_H_

#include <cstddef>

// Some exceptions for use with this class
class RingbufRException
{
};
class RingbufRArgumentException
    : public RingbufRException
{
};
class RingbufREmptyException
    : public RingbufRException
{
};
class RingbufRFullException
    : public RingbufRException
{
};

// Class RingbufR
template<typename _T>
class RingbufR
{
public:

    RingbufR (size_t capacity, size_t push_pad=0, size_t pop_pad=0);
    virtual ~RingbufR ();
    RingbufR(const RingbufR&) = delete;
    RingbufR() = delete;
    RingbufR(RingbufR&&) = delete;
    RingbufR& operator=(const RingbufR&) = delete;
    RingbufR& operator=(RingbufR&&) = delete;

    virtual void pushInquire(size_t& available, _T*& start) const;
    void push(size_t newContent);
    virtual void popInquire(size_t& available, _T*& start) const;
    void pop(size_t oldContent);
    size_t size() const;

    // For debugging
    const _T* buffer_start() const;
    const _T* ring_start() const;
    const _T* ring_end() const;
    static void validate(const _T* start, size_t count);
    struct debugState
    {
        size_t ring_start;
        size_t ring_end;
        size_t neutral_start;
        size_t neutral_end;
        size_t push_next;
        size_t pop_next;
        size_t limit_pops;
        size_t limit_pushes;
        size_t internal_copies;
        size_t pushes;
        size_t pops;
        bool  empty;
    };
    debugState getState() const;

private:

    // Should only be called if wrap-around is in effect and
    // buffer is not empty.
    void adjustStart();

    // Should not be called if wrap-around is already in effect.
    // return value: wrap-around is in effect.
    bool adjustEnd();

    const size_t _capacity;
    const size_t _push_pad;
    const size_t _pop_pad;
    _T* const _edge_start;
    _T* const _edge_end;
    _T* const _neutral_start;
    _T* const _neutral_end;
    bool _empty;
    size_t _limit_pops;
    size_t _limit_pushes;
    size_t _internal_copies;
    size_t _pushes;
    size_t _pops;
    _T* _push_next;
    _T* _pop_next;
    _T* _ring_start;
    _T* _ring_end;
};

#endif // __RINGBUFR_H_
