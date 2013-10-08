#ifndef DISRUPTORPLUS_SEQUENCE_BARRIER_GROUP_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_BARRIER_GROUP_HPP_INCLUDED

#include <disruptorplus/sequence.hpp>
#include <disruptorplus/sequence_barrier.hpp>

#include <cassert>
#include <chrono>
#include <vector>

namespace disruptorplus
{
    template<typename WaitStrategy>
    class sequence_barrier_group
    {
    public:
    
        sequence_barrier_group(WaitStrategy& waitStrategy)
        : m_waitStrategy(waitStrategy)
        {}
        
        void add(const sequence_barrier<WaitStrategy>& barrier)
        {
            assert(&barrier.m_waitStrategy == &m_waitStrategy);
            m_sequences.push_back(&barrier.m_lastPublished);
        }
        
        void add(const sequence_barrier_group<WaitStrategy>& barrierGroup)
        {
            m_sequences.insert(
                m_sequences.end(),
                barrierGroup.m_sequences.begin(),
                barrierGroup.m_sequences.end());
        }
        
        sequence_t last_published() const
        {
            assert(!m_sequences.empty());
            return minimum_sequence(m_sequences.size(), m_sequences.data());
        }
        
        sequence_t wait_until_published(sequence_t sequence) const
        {
            assert(!m_sequences.empty());
            
            size_t count = m_sequences.size();
            
            sequence_t current = minimum_sequence_after(sequence, count, m_sequences.data());
            if (difference(current, sequence) >= 0)
            {
                return current;
            }
            
            return m_waitStrategy.wait_until_published(sequence, count, m_sequences.data());
        }

        template<class Rep, class Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            assert(!m_sequences.empty());
            
            sequence_t current = minimum_sequence_after(sequence, count, m_sequences.data());
            if (difference(current, sequence) >= 0)
            {
                return current;
            }
            
            return m_waitStrategy.wait_until_published(sequence, count, m_sequences.data(), timeout);
        }
        
        template<class Clock, class Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            assert(!m_sequences.empty());
            
            sequence_t current = minimum_sequence_after(sequence, count, m_sequences.data());
            if (difference(current, sequence) >= 0)
            {
                return current;
            }
            
            return m_waitStrategy.wait_until_published(sequence, count, m_sequences.data(), timeoutTime);
        }
        
    private:
    
        WaitStrategy& m_waitStrategy;
        std::vector<const std::atomic<sequence_t>*> m_sequences;
    
    };
}

#endif
