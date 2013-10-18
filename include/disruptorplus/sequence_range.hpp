#ifndef DISRUPTORPLUS_SEQUENCE_RANGE_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_RANGE_HPP_INCLUDED

#include <disruptorplus/sequence.hpp>

namespace disruptorplus
{
    // A sequence range represents a contiguous range of sequence numbers.
    //
    // The range of sequence numbers may overflow the underlying integer
    // storage type in which case the values wrap back around to zero.
    class sequence_range
    {
    public:
    
        // Construct to the empty range.
        sequence_range()
        : m_first(0)
        , m_size(0)
        {}
        
        // Construct to a range of 'size' sequence numbers starting at 'first'.
        sequence_range(sequence_t first, size_t size)
        : m_first(first)
        , m_size(size)
        {}

        // Number of sequence numbers in this range.
        size_t size() const { return m_size; }
        
        // The first sequence number in the range.
        sequence_t first() const { return m_first; }
        
        // The last sequence in the range.
        sequence_t last() const { return end() - 1; }
        
        // One-past the last sequence number in the range.
        sequence_t end() const { return static_cast<sequence_t>(m_first + m_size); }
        
        // Lookup the nth sequence number in the range.
        sequence_t operator[](size_t index) const { return static_cast<sequence_t>(m_first + index); }
        
    private:
    
        sequence_t m_first;
        size_t m_size;
        
    };
}

#endif
