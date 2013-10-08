#ifndef DISRUPTORPLUS_SEQUENCE_RANGE_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_RANGE_HPP_INCLUDED

#include <disruptorplus/sequence.hpp>

namespace disruptorplus
{
    class sequence_range
    {
    public:
    
        // Construct to the empty range.
        sequence_range()
        : m_first(0)
        , m_size(0)
        {}
        
        sequence_range(sequence_t first, size_t size)
        : m_first(first)
        , m_size(size)
        {}

        size_t size() const { return m_size; }
        
        sequence_t first() const { return m_first; }
        sequence_t last() const { return end() - 1; }
        sequence_t end() const { return static_cast<sequence_t>(m_first + m_size); }
        
        sequence_t operator[](size_t index) const { return static_cast<sequence_t>(m_first + index); }
        
    private:
    
        sequence_t m_first;
        size_t m_size;
        
    };
}

#endif
