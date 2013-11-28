#ifndef DISRUPTORPLUS_RING_BUFFER_HPP_INCLUDED
#define DISRUPTORPLUS_RING_BUFFER_HPP_INCLUDED

#include <disruptorplus/sequence.hpp>

#include <memory>
#include <cassert>

namespace disruptorplus
{
    // A ring buffer is a buffer of size power-of-two that can
    // be indexed using a sequence number.
    //
    // A given slot, i, in the ring buffer is addressed by any sequence
    // number that has the form n * size() + i for some n.
    //
    // A ring buffer is typically used in conjunction with a claim-strategy
    // for writers to claim a slot in the ring buffer, and one or more
    // sequence-barriers for readers to indicate where in the ring buffer
    // they have processed up to.
    template<typename T>
    class ring_buffer
    {
    public:
    
        typedef T value_type;
        typedef T& reference_type;
    
        ring_buffer(size_t size)
        : m_size(size)
        , m_mask(size - 1)
        , m_data(new T[size])
        {
            // Check that size was a power-of-two.
            assert(m_size > 0 && (m_size & m_mask) == 0);
        }
        
        size_t size() const
        {
            return m_size;
        }
        
        T& operator[](sequence_t seq)
        {
            return m_data[static_cast<size_t>(seq) & m_mask];
        }
        
        const T& operator[](sequence_t seq) const
        {
            return m_data[static_cast<size_t>(seq) & m_mask];
        }
        
    private:

        ring_buffer(const ring_buffer&);
    
        const size_t m_size;
        const size_t m_mask;
        std::unique_ptr<T[]> m_data;
    
    };
}

#endif
