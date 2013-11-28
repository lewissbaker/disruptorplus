#ifndef DISRUPTORPLUS_RING_BUFFER_HPP_INCLUDED
#define DISRUPTORPLUS_RING_BUFFER_HPP_INCLUDED

#include <disruptorplus/sequence.hpp>

#include <memory>
#include <cassert>

namespace disruptorplus
{
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
