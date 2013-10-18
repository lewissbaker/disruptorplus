#ifndef DISRUPTORPLUS_SINGLE_THREADED_CLAIM_STRATEGY_HPP_INCLUDED
#define DISRUPTORPLUS_SINGLE_THREADED_CLAIM_STRATEGY_HPP_INCLUDED

#include <disruptorplus/sequence_range.hpp>
#include <disruptorplus/sequence_barrier.hpp>
#include <disruptorplus/sequence_barrier_group.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>

namespace disruptorplus
{
    template<typename WaitStrategy>
    class single_threaded_claim_strategy
    {
    public:
    
        single_threaded_claim_strategy(
            size_t bufferSize,
            WaitStrategy& waitStrategy)
        : m_bufferSize(bufferSize)
        , m_nextSequenceToClaim(0)
        , m_lastKnownClaimableSequence(static_cast<sequence_t>(-1))
        , m_claimBarrier(waitStrategy)
        , m_readBarrier(waitStrategy)
        {}

        size_t buffer_size() const
        {
            return m_bufferSize;
        }
        
        void add_claim_barrier(sequence_barrier<WaitStrategy>& barrier)
        {
            m_claimBarrier.add(barrier);
            m_lastKnownClaimableSequence = m_claimBarrier.last_published() + m_bufferSize;
        }
        
        void add_claim_barrier(sequence_barrier_group<WaitStrategy>& barrier)
        {
            m_claimBarrier.add(barrier);
            m_lastKnownClaimableSequence = m_claimBarrier.last_published() + m_bufferSize;
        }

        sequence_t claim_one()
        {
            return claim(1).first();
        }
        
        sequence_range claim(size_t count)
        {
            sequence_range result;
            if (try_claim(count, result))
            {
                return result;
            }

            sequence_t claimable = static_cast<sequence_t>(
                m_claimBarrier.wait_until_published(
                    static_cast<sequence_t>(m_nextSequenceToClaim - m_bufferSize)) +
                m_bufferSize);
                
            sequence_diff_t diff = difference(claimable, m_nextSequenceToClaim);
            assert(diff >= 0);
            
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            result = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            m_lastKnownClaimableSequence = claimable;
            
            return result;
        }
        
        bool try_claim(size_t count, sequence_range& range)
        {
            sequence_diff_t diff = difference(m_lastKnownClaimableSequence, m_nextSequenceToClaim);
            if (diff < 0)
            {
                sequence_t seq = static_cast<sequence_t>(m_claimBarrier.last_published() + m_bufferSize);
                diff = difference(seq, m_nextSequenceToClaim);
                if (diff < 0)
                {
                    return false;
                }
                
                // Only bother updating our cached claimable seq if we will actually be
                // claiming something. Otherwise our existing cached value already indicates
                // that we need to check again next time already.
                m_lastKnownClaimableSequence = seq;
            }
            assert(diff >= 0);
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            range = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            return true;
        }
        
        template<class Rep, class Period>
        bool try_claim_for(
            size_t count,
            sequence_range& range,
            const std::chrono::duration<Rep, Period>& timeout)
        {
            if (try_claim(count, range))
            {
                return true;
            }
            
            sequence_t claimable =
                static_cast<sequence_t>(
                    m_claimBarrier.wait_until_published(
                        static_cast<sequence_t>(m_nextSequenceToClaim - m_bufferSize),
                        timeout) + m_bufferSize);
            sequence_diff_t diff = difference(claimable, m_nextSequenceToClaim);
            if (diff < 0)
            {
                // Timeout
                return false;
            }
            
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            range = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            m_lastKnownClaimableSequence = claimable;
            
            return true;
        }
        
        template<class Clock, class Duration>
        bool try_claim_until(
            size_t count,
            sequence_range& range,
            const std::chrono::time_point<Clock, Duration>& timeoutTime)
        {
            if (try_claim(count, range))
            {
                return true;
            }
            
            sequence_t claimable =
                static_cast<sequence_t>(
                    m_claimBarrier.wait_until_published(
                        static_cast<sequence_t>(m_nextSequenceToClaim - m_bufferSize),
                        timeoutTime) + m_bufferSize);
            sequence_diff_t diff = difference(claimable, m_nextSequenceToClaim);
            if (diff < 0)
            {
                // Timeout
                return false;
            }
            
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            range = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            m_lastKnownClaimableSequence = claimable;
            
            return true;
        }
        
        void publish(sequence_t sequence)
        {
            m_readBarrier.publish(sequence);
        }

        sequence_t last_published() const
        {
            return m_readBarrier.last_published();
        }

        sequence_t wait_until_published(sequence_t sequence) const
        {
            return m_readBarrier.wait_until_published(sequence);
        }

        template<typename Rep, typename Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            return m_readBarrier.wait_until_published(sequence, timeout);
        }

        template<typename Clock, typename Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            return m_readBarrier.wait_until_published(sequence, timeoutTime);
        }
        
    private:
    
        const size_t m_bufferSize;
        
        // The next sequence to be claimed (may not yet be available).
        sequence_t m_nextSequenceToClaim;
        
        // A cache of the last-known available sequence value
        // queried using m_claimBarrier.
        sequence_t m_lastKnownClaimableSequence;

        sequence_barrier_group<WaitStrategy> m_claimBarrier;
        
        // Barrier used to publish items to the 
        sequence_barrier<WaitStrategy> m_readBarrier;
    
    };
    
}

#endif
