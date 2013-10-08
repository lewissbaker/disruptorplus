#ifndef DISRUPTORPLUS_SEQUENCE_BARRIER_HPP_INCLUDED
#define DISRUPTORPLUS_SEQUENCE_BARRIER_HPP_INCLUDED

#include <disruptorplus/config.hpp>
#include <disruptorplus/sequence.hpp>

#include <atomic>
#include <chrono>

namespace disruptorplus
{
    template<typename WaitStrategy>
    class sequence_barrier_group;

    template<typename WaitStrategy>
    class sequence_barrier
    {
    public:
    
        sequence_barrier(WaitStrategy& waitStrategy)
        : m_waitStrategy(waitStrategy)
        , m_lastPublished(static_cast<sequence_t>(-1))
        {}
        
        sequence_t last_published() const
        {
            return m_lastPublished.load(std::memory_order_acquire);
        }
        
        sequence_t wait_until_published(sequence_t sequence) const
        {
            sequence_t current = last_published();
            sequence_diff_t diff = difference(current, sequence);
            if (diff >= 0)
            {
                return current;
            }
            const std::atomic<sequence_t>* const sequences[] = { &m_lastPublished };
            return m_waitStrategy.wait_until_published(sequence, 1, sequences);
        }

        template<class Rep, class Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            sequence_t current = last_published();
            sequence_diff_t diff = difference(current, sequence);
            if (diff >= 0)
            {
                return current;
            }
            const std::atomic<sequence_t>* const sequences[] = { &m_lastPublished };
            return m_waitStrategy.wait_until_published(sequence, 1, sequences, timeout);
        }
        
        template<class Clock, class Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            sequence_t current = last_published();
            sequence_diff_t diff = difference(current, sequence);
            if (diff >= 0)
            {
                return current;
            }
            const std::atomic<sequence_t>* const sequences[] = { &m_lastPublished };
            return m_waitStrategy.wait_until_published(sequence, 1, sequences, timeoutTime);
        }
        
        void publish(sequence_t sequence)
        {
            m_lastPublished.store(sequence, std::memory_order_release);
            m_waitStrategy.signal_all_when_blocking();
        }
        
    private:
    
        friend class sequence_barrier_group<WaitStrategy>;
    
        WaitStrategy& m_waitStrategy;
    
        // Pad before/after to prevent false-sharing
        uint8_t m_pad1[CacheLineSize - sizeof(sequence_t)];
        std::atomic<sequence_t> m_lastPublished;
        uint8_t m_pad2[CacheLineSize - sizeof(sequence_t)];
        
    };
}

#endif
