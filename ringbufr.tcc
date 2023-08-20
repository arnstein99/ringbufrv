// Implementation of RingbufR and RingbufRbase
#ifndef __RINGBUFR_TCC
#define __RINGBUFR_TCC

#include <cassert>
#include <algorithm>
#include <string.h>

template<typename _T>
RingbufR<_T>::RingbufR(size_t capacity)
    : RingbufRbase<_T>(capacity, new _T[capacity])
{
}

template<typename _T>
RingbufR<_T>::~RingbufR()
{
    delete[] RingbufRbase<_T>::_ring_start;
}

template<typename _T>
void RingbufR<_T>::validate(const _T* /*start*/, size_t /*count*/)
{
    // The user can override as desired.
}

template<typename _T>
RingbufRbase<_T>::RingbufRbase (size_t capacity, _T* store)
    : _ring_start(store),
      _capacity(capacity),
      _ring_end(_ring_start + capacity)
{
    _push_next  = _ring_start;
    _pop_next   = _ring_start;
    _pushes = 0;
    _pops = 0;
    _empty = true;
}

template<typename _T>
size_t RingbufRbase<_T>::pushInquire(
        size_t& available1, _T*& start1, size_t& available2, _T*& start2) const
{
    size_t nseg;
    if (_empty)
    {
        assert(_push_next == _pop_next);
        start1 = _pop_next;
        available1 = _ring_end - _push_next;
        if (_push_next == _ring_start)
        {
            start2 = nullptr;
            available2 = 0;
            nseg = 1;
        }
        else
        {
            start2 = _ring_start;
            available2 = _pop_next - _ring_start;
            nseg = 2;
        }
    }
    else
    {
        if (_push_next == _pop_next)
        {
            // Full
            start1 = nullptr;
            available1 = 0;
            start2 = nullptr;
            available2 = 0;
            nseg = 0;
        }
        else
        {
            start1 = _push_next;
            if (_push_next > _pop_next)
            {
                // No wrap-around
                available1 = _ring_end - _push_next;
                start2 = _ring_start;
                available2 = _pop_next - _ring_start;
                nseg = 2;
            }
            else
            {
                // Wrap-around is in effect
                available1 = _pop_next - _push_next;
                start2 = nullptr;
                available2 = 0;
                nseg = 1;
            }
        }
    }
    return nseg;
}

template<typename _T>
void RingbufRbase<_T>::push(size_t increment)
{
    if (increment == 0) return;

    ++_pushes;
    size_t limit1;
    if (_empty)
    {
        assert(_push_next == _pop_next);
        limit1 = _ring_end - _push_next;
        _empty = false;
    }
    else
    {
        if (_push_next <= _pop_next)
        {
            // Wrap-around is in effect
            limit1 = _pop_next - _push_next;
        }
        else
        {
            limit1 = _ring_end - _push_next;
        }
    }

    if (increment <= limit1)
    {
        _push_next += increment;
    }
    else
    {
        increment -= limit1;
        _push_next = _ring_start + increment;
    }
}

template<typename _T>
size_t RingbufRbase<_T>::popInquire(
        size_t& available1, _T*& start1, size_t& available2, _T*& start2) const
{
    size_t nseg;
    if (_empty)
    {
        assert(_push_next == _pop_next);
        available1 = 0;
        start1 = nullptr;
        available2 = 0;
        start2 = nullptr;
        nseg = 0;
    }
    else
    {
        start1 = _pop_next;
        if (_push_next <= _pop_next)
        {
            // Wrap-around is in effect
            available1 = _ring_end - _pop_next;
            start2 = _ring_start;
            available2 = _push_next - _ring_start;
            nseg = 2;
        }
        else
        {
            available1 = _push_next - _pop_next;
            start2 = nullptr;
            available2 = 0;
            nseg = 1;
        }
    }
    return nseg;
}

template<typename _T>
void RingbufRbase<_T>::pop(size_t increment)
{
    if (increment == 0) return;

    ++_pops;
    size_t limit1;
    if (_push_next <= _pop_next)
    {
        // Wrap-around is in effect;
        limit1 = _ring_end - _pop_next;
    }
    else
    {
        limit1 = _push_next - _pop_next;
    }
    if (increment <= limit1)
    {
        _pop_next += increment;
    }
    else
    {
        increment -= limit1;
        _pop_next = _ring_start + increment;
    }
    _empty = (_pop_next == _push_next);
}

template<typename _T>
size_t RingbufRbase<_T>::size() const
{
    if (_empty) return 0;
    if (_push_next < _pop_next)
    {
        // Wrap-around is in effect
        return (_ring_end - _pop_next) + (_push_next - _ring_start);
    }
    if (_push_next == _pop_next)
    {
        // Ring buffer is full
        return (_ring_end - _ring_start);
    }
    // (_push_next > _pop_next)
    // Wrap-around is not in effect
    return (_push_next - _pop_next);
}

template<typename _T>
const _T* RingbufRbase<_T>::ring_start() const
{
    return _ring_start;
}

template<typename _T>
const _T* RingbufRbase<_T>::ring_end() const
{
    return _ring_end;
}

template<typename _T>
typename RingbufRbase<_T>::debugState RingbufRbase<_T>::getState() const
{
    debugState state;
    state.pop_next = _pop_next - _ring_start;
    state.push_next = _push_next - _ring_start;
    state.pushes = _pushes;
    state.pops = _pops;
    state.empty = _empty;
    return state;
}

template<typename _T>
void RingbufRbase<_T>::validatebase(const _T* /*start*/, size_t /*count*/)
{
    // The user can override as desired.
}

#endif // __RINGBUFR_TCC
