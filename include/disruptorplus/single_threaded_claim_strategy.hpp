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
    // A claim strategy for use where only a single thread will be publishing
    // items to a ring buffer.
    //
    // This strategy avoids the overhead required to synchronise multiple
    // threads trying to claim slots in the ring buffer.
    //
    // A writer thread starts by first claiming one or more slots in the ring buffer
    // by calling one of the 'wait' methods. The writer then writes to the slots
    // and indicates to readers it has finished writing to the slots by calling
    // publish().
    //
    // Reader threads can wait until a writer thread has published an element
    // to the ring buffer by calling one of the wait_until_published() methods.
    //
    // Readers indicate they are finished reading from a slot in the ring buffer
    // by publishing the sequence number of the latest item they are finished
    // with in the 'claim barrier'. This frees the slot up for writers to
    // subsequently claim it for future writers.
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

        // Block the caller until a single slot in the buffer has become
        // available. Returns the sequence number of the slot.
        //
        // The caller may write to the returned slot and once finished
        // must call publish() passing the returned sequence number to
        // make it available for readers.
        sequence_t claim_one()
        {
            return claim(1).first();
        }
        
        // Request one or more slots in the buffer to write to, blocking
        // until at least one slot is available. The returned sequence
        // range may contain less slots than requested if fewer are
        // available.
        //
        // The caller may write to the returned slots and once finished
        // must call publish(), passing the last sequence number that
        // has been written to.
        //
        // This operation has 'acquire' memory semantics.
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
        
        // Attempt to claim up to 'count' slots in the buffer for writing.
        // Returns true if any slots were claimed, in which case 'range'
        // is updated with the sequence of slots that were claimed. Note
        // that the range may contain fewer slots than requested if fewer
        // slots were available.
        // Returns false if no slots were available, in which case 'range'
        // is left unmodified.
        //
        // This call does not block and returns immediately.
        //
        // This operation has 'acquire' memory semantics if it returns
        // true and 'relaxed' memory semantics if it returns false.
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
                // that we need to check again next time.
                m_lastKnownClaimableSequence = seq;
            }
            assert(diff >= 0);
            size_t available = static_cast<size_t>(diff + 1);
            count = std::min(count, available);
            range = sequence_range(m_nextSequenceToClaim, count);
            m_nextSequenceToClaim += count;
            return true;
        }
        
        // Attempt to claim up to 'count' slots in the buffer for writing.
        // Waits for up to 'timeout' for a slot to become available if no
        // slots currently available.
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
        
        // Flag that all sequences up to and including 'sequence' have been
        // published and are now available for readers to access.
        //
        // This operation has 'release' memory semantics.
        void publish(sequence_t sequence)
        {
            m_readBarrier.publish(sequence);
        }

        // Return the last sequence that was published.
        //
        // All sequences up to and including returned sequence value
        // have been published and are available for readers to access.
        //
        // This operation has 'acquire' memory semantics.
        sequence_t last_published() const
        {
            return m_readBarrier.last_published();
        }

        // Block the caller until the specified sequence has been
        // published by the writer thread.
        //
        // Returns the value of last_published() which may be in
        // advance of the requested sequence.
        //
        // This operation has 'acquire' memory semantics.
        sequence_t wait_until_published(sequence_t sequence) const
        {
            return m_readBarrier.wait_until_published(sequence);
        }

        // Block the caller until either the specified sequence has been
        // published by the writer thread or until the specified timeout
        // has elapsed.
        //
        // Returns the value of last_published() which may be prior to
        // the specified sequence in the case of a timeout or equal to
        // or after the specified sequence in the case of that item being
        // published.
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
