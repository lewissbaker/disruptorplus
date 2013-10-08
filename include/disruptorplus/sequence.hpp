#ifndef DISRUPTORPLUS_SEQUENCE_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_HPP_INCLUDED

#include <atomic>
#include <cstdint>
#include <cassert>

namespace disruptorplus
{
    typedef uint64_t sequence_t;
    typedef int64_t sequence_diff_t;
    
    inline sequence_diff_t difference(sequence_t a, sequence_t b)
    {
        return static_cast<sequence_diff_t>(a - b);
    }
    
    // Calculate the minimum sequence number of the array of sequences
    // taking into account wrapping by using the first sequence as the zero-point.
    // This assumes no active sequence value will be more than
    // (1 << (sizeof(sequence_t) * 8 - 2)) - 1 different from each other.
    //
    // Implies acquire memory semantics on each of the sequences.
    inline sequence_t minimum_sequence(
        size_t count,
        const std::atomic<sequence_t>* const sequences[])
    {
        assert(count > 0);
        sequence_t current = sequences[0]->load(std::memory_order_acquire);
        for (size_t i = 1; i < count; ++i)
        {
            sequence_t seq = sequences[i]->load(std::memory_order_acquire);
            if (difference(seq, current) < 0)
            {
                current = seq;
            }
        }
        return current;
    }
    
    // Calculate the minimum sequence number of the array of sequences
    // taking into account wrapping by using a current value as the zero-point.
    // This assumes no active sequence value will be more than
    // (1 << (sizeof(sequence_t) * 8 - 2)) different from current.
    //
    // Implies acquire memory semantics on each of the sequences.
    // If any of the sequences precede 'current' then return its value
    // immediately without checking the others.
    inline sequence_t minimum_sequence_after(
        sequence_t current,
        size_t count,
        const std::atomic<sequence_t>* const sequences[])
    {
        assert(count > 0);
        sequence_diff_t minDelta = difference(sequences[0]->load(std::memory_order_acquire), current);
        for (size_t i = 1; i < count && minDelta >= 0; ++i)
        {
            sequence_t seq = sequences[i]->load(std::memory_order_acquire);
            minDelta = std::min(minDelta, difference(seq, current));
        }
        return static_cast<sequence_t>(minDelta + current);
    }
}

#endif
