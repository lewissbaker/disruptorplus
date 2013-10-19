#ifndef DISRUPTORPLUS_MULTI_THREADED_CLAIM_STRATEGY_HPP_INCLUDED
#define DISRUPTORPLUS_MULTI_THREADED_CLAIM_STRATEGY_HPP_INCLUDED

#include <disruptorplus/config.hpp>
#include <disruptorplus/sequence_barrier.hpp>
#include <disruptorplus/sequence_barrier_group.hpp>
#include <disruptorplus/sequence_range.hpp>

#include <atomic>
#include <chrono>

namespace disruptorplus
{
    // A claim strategy for slots in a ring buffer where there are multiple
    // producer threads all concurrently trying to write items to the ring
    // buffer.
    //
    // When a writer wants to write to a slot in the queue it first atomically
    // increments a counter by the number of slots it wishes to allocate.
    // It then waits until all of those slots have become available and then
    // returns the range of sequence numbers allocated back to the caller.
    // The caller then writes to those slots and when done publishes them
    // by writing the sequence numbers published to each of the slots to
    // the corresponding element of an array of equal size to the ring buffer.
    // When a reader wants to check if the next sequence number is available
    // it them simply needs to read from the corresponding slot in this array
    // to check if the value stored there is equal to the sequence number it
    // is wanting to read.
    //
    // This means concurrent writers are wait-free when there is space available
    // in the ring buffer, requiring a single atomic fetch-add operation as the
    // only contended write operation. All other writes are to memory locations
    // owned by a particular writer. Concurrent writers can publish items
    // out-of-order so that one writer does not hold up other writers until the
    // ring buffer fills up.
    template<typename WaitStrategy>
    class multi_threaded_claim_strategy
    {
    public:
    
        // Initialise a new claim strategy.
        //
        // bufferSize is the number of elements in the ring buffer and
        // must be a power of two.
        
        multi_threaded_claim_strategy(
            size_t bufferSize,
            WaitStrategy& waitStrategy)
            : m_indexMask(bufferSize - 1)
            , m_bufferSize(bufferSize)
            , m_waitStrategy(waitStrategy)
            , m_claimBarrier(waitStrategy)
            , m_published(new std::atomic<sequence_t>[bufferSize])
            , m_nextClaimable(0)
        {
            // bufferSize must be power-of-two
            assert(m_bufferSize > 0 && (m_bufferSize & (m_bufferSize - 1)) == 0);
            
            // Initialise the buffer such that sequences before 0 have not yet
            // been published.
            for (sequence_t i = 0; i < bufferSize; ++i)
            {
                m_published[i].store(static_cast<sequence_t>(i - bufferSize), std::memory_order_relaxed);
            }
        }
        
        size_t buffer_size() const { return m_bufferSize; }
        
        // Add a sequence barrier that prevents elements of the ring buffer from
        // being claimed by writers until the reader has indicated the slot is
        // available by publishing the sequence number they have finished reading
        // up to. Claimed slots will never advance more than buffer_size() ahead
        // of any of the registered claim barriers.
        // The wait strategy of the barriers added must be the same wait strategy
        // that was used to initialise the claim strategy.
        void add_claim_barrier(sequence_barrier<WaitStrategy>& barrier)
        {
            m_claimBarrier.add(barrier);
        }
        
        void add_claim_barrier(sequence_barrier_group<WaitStrategy>& barrier)
        {
            m_claimBarrier.add(barrier);
        }
        
        // Claim a single slot in the ring buffer for writing to.
        // Blocks the caller until a slot is available.
        // Returns the sequence number of the slot claimed.
        sequence_t claim_one()
        {
            sequence_t sequence = m_nextClaimable.fetch_add(1, std::memory_order_relaxed);
            m_claimBarrier.wait_until_published(
                static_cast<sequence_t>(sequence - m_bufferSize));
            return sequence;
        }
        
        // Claim a number of consecutive slots in the ring buffer.
        //
        // Blocks the caller until items are available.
        // Will allocate at most buffer_size() slots in the ring buffer.
        //
        // This operation has acquire memory semantics.
        sequence_range claim(size_t count)
        {
            count = std::min(count, m_bufferSize);
            sequence_t sequence = m_nextClaimable.fetch_add(count, std::memory_order_relaxed);
            sequence_range range(sequence, count);
            m_claimBarrier.wait_until_published(
                static_cast<sequence_t>(range.last() - m_bufferSize));
            return range;
        }
        
        // Attempt to claim up to 'count' slots in the ring buffer without blocking.
        //
        // Returns true if any elements were claimed in which case the range of
        // slots claimed is written to the 'range' out-parameter. May claim less
        // slots than requested if fewer slots were available.
        //
        // Returns false if no elements were claimed. The 'range' out-parameter is
        // not modified in this case.
        //
        // This operation has 'acquire' memory semantics.
        bool try_claim(size_t count, sequence_range& range)
        {
            sequence_t published =
                static_cast<sequence_t>(m_claimBarrier.last_published() + m_bufferSize);
            
            sequence_t sequence = m_nextClaimable.load(std::memory_order_relaxed);
            do
            {
                sequence_diff_t diff = difference(published, sequence);
                if (diff < 0)
                {
                    return false;
                }
                count = std::min(count, static_cast<size_t>(diff + 1));
            } while (!m_nextClaimable.compare_exchange_weak(
                sequence,
                static_cast<sequence_t>(sequence + count),
                std::memory_order_relaxed,
                std::memory_order_relaxed));
                
            range = sequence_range(sequence, count);
            return true;
        }
        
        // Attempt to claim up to 'count' slots in the ring buffer.
        //
        // Blocks until either at least one slot was claimed or until
        // the specified timeout period has elapsed, whichever comes first.
        //
        // Returns true if any slots were claimed in which case the range
        // of slots claimed is written to the 'range' out-parameter.
        //
        // Returns false if the timeout duration was exceeded without
        // claiming any slots.
        //
        // This operation has 'acquire' memory semantics.
        template<class Rep, class Period>
        bool try_claim_for(
            size_t count,
            sequence_range& range,
            const std::chrono::duration<Rep, Period>& timeout)
        {
            return try_claim_until(
                count,
                range,
                std::chrono::high_resolution_clock::now() + timeout);
        }
        
        // Attempt to claim up to 'count' slots in the ring buffer.
        //
        // Blocks until either at least one slot was claimed or until
        // the specified timeout time has passed, whichever comes first.
        //
        // Returns true if any slots were claimed in which case the range
        // of slots claimed is written to the 'range' out-parameter.
        //
        // Returns false if the timeout was exceeded without claiming any slots.
        //
        // This operation has 'acquire' memory semantics.
        template<class Clock, class Duration>
        bool try_claim_until(
            size_t count,
            sequence_range& range,
            const std::chrono::time_point<Clock, Duration>& timeoutTime)
        {
            sequence_t published =
                static_cast<sequence_t>(m_claimBarrier.last_published() + m_bufferSize);
            
            sequence_t sequence = m_nextClaimable.load(std::memory_order_relaxed);
            size_t reducedCount;
            do
            {
                sequence_diff_t diff = difference(published, sequence);
                if (diff < 0)
                {
                    published = static_cast<sequence_t>(
                        m_claimBarrier.wait_until_published(
                            static_cast<sequence_t>(sequence - m_bufferSize),
                            timeoutTime) + m_bufferSize);
                    diff = difference(published, sequence);
                    if (diff < 0)
                    {
                        // Timeout
                        return false;
                    }
                }
                reducedCount = std::min(count, static_cast<sequence_t>(diff + 1));
            } while (!m_nextClaimable.compare_exchange_weak(
                sequence,
                static_cast<sequence_t>(sequence + reducedCount),
                std::memory_order_relaxed,
                std::memory_order_relaxed));
                
            range = sequence_range(sequence, reducedCount);
            
            return true;
        }
        
        // Publish the element written to a slot in the ring buffer,
        // signalling that the element is available to be accessed by
        // reader threads.
        //
        // A reader will not be notified that this element is available
        // until all other sequences prior to this element have also
        // been published.
        //
        // A writer must call this method when they have finished
        // writing data to the ring-buffer element they previously
        // claimed by calling one of the 'claim' methods above.
        //
        // This operation has 'release' memory semantics.
        void publish(sequence_t sequence)
        {
            set_published(sequence);
            m_waitStrategy.signal_all_when_blocking();
        }
        
        // Publish a sequence of elements written to slots in the ring buffer,
        // signalling that the elements are available to be accessed by
        // reader threads.
        //
        // A reader will not be notified that these elements are available
        // until all other sequences prior to these elements have also
        // been published.
        //
        // A writer must call this method when they have finished
        // writing data to the ring-buffer elements they previously
        // claimed by calling one of the 'claim' methods above.
        //
        // This operation has 'release' memory semantics.
        void publish(const sequence_range& range)
        {
            for (size_t i = 0, j = range.size(); i < j; ++i)
            {
                set_published(range[i]);
            }
            m_waitStrategy.signal_all_when_blocking();
        }
        
        // Return the highest sequence number published after the specified
        // last-known published sequence.
        //
        // The 'lastKnownPublished' value is assumed to have already been
        // published. The initial value passed in here on first call should
        // be sequence_t(-1).
        //
        // Returns the passed value if no additional sequences have been
        // published.
        sequence_t last_published_after(sequence_t lastKnownPublished) const
        {
            sequence_t seq = lastKnownPublished + 1;
            while (is_published(seq))
            {
                lastKnownPublished = seq;
                ++seq;
            }
            return lastKnownPublished;
        }
        
        // Block the caller until the specified sequence number has been
        // published.
        //
        // This method is called by reader threads waiting to consume
        // items written to the ring buffer.
        //
        // The 'lastKnownPublished' value is assumed to have already been
        // published. The initial value passed in here on first call should
        // be sequence_t(-1).
        //
        // Returns the sequence number of the latest available published
        // sequence, guaranteed to be equal to or later than the specified
        // 'sequence' parameter.
        sequence_t wait_until_published(
            sequence_t sequence,
            sequence_t lastKnownPublished) const
        {
            assert(difference(sequence, lastKnownPublished) > 0);
            
            for (sequence_t seq = lastKnownPublished + 1;
                 difference(seq, sequence) <= 0;
                 ++seq)
            {
                if (!is_published(seq))
                {
                    const std::atomic<sequence_t>* const sequences[1] =
                        { &m_published[seq & m_indexMask] };
                    m_waitStrategy.wait_until_published(seq, 1, sequences);
                }
            }
            return last_published_after(sequence);
        }
        
        // Block the caller until the specified sequence number has been
        // published or until a specified timeout has elapsed.
        //
        // This method is called by reader threads waiting to consume
        // items written to the ring buffer.
        //
        // The 'lastKnownPublished' value is assumed to have already been
        // published. The initial value passed in here on first call should
        // be sequence_t(-1).
        //
        // Returns the sequence number of the latest available published
        // sequence, guaranteed to be equal to or later than the specified
        // 'sequence' parameter.
        template<typename Rep, typename Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            sequence_t lastKnownPublished,
            const std::chrono::duration<Rep, Period>& timeout) const
        {
            return wait_until_published(
                sequence,
                lastKnownPublished,
                std::chrono::high_resolution_clock::now() + timeout);
        }
        
        // Block the caller until the specified sequence number has been
        // published or until a specified timeout time has passed.
        //
        // This method is called by reader threads waiting to consume
        // items written to the ring buffer.
        //
        // The 'lastKnownPublished' value is assumed to have already been
        // published. The initial value passed in here on first call should
        // be sequence_t(-1).
        //
        // Returns the sequence number of the latest available published
        // sequence, guaranteed to be equal to or later than the specified
        // 'sequence' parameter.
        template<typename Clock, typename Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            sequence_t lastKnownPublished,
            const std::chrono::time_point<Clock, Duration>& timeoutTime) const
        {
            assert(difference(sequence, lastKnownPublished) > 0);
            
            for (sequence_t seq = lastKnownPublished + 1;
                 difference(seq, sequence) <= 0;
                 ++seq)
            {
                if (!is_published(seq))
                {
                    const std::atomic<sequence_t>* const sequences[1] =
                        { &m_published[seq & m_indexMask] };
                    sequence_t result =
                        m_waitStrategy.wait_until_published(seq, 1, sequences);
                    if (difference(result, seq) < 0)
                    {
                        // Timeout. seq is the first non-published sequence
                        return seq - 1;
                    }
                }
            }
            return last_published_after(sequence);
        }
        
    private:
    
        bool is_published(sequence_t sequence) const
        {
            return m_published[sequence & m_indexMask].load(std::memory_order_acquire) == sequence;
        }
        
        void set_published(sequence_t sequence)
        {
            auto& entry = m_published[sequence & m_indexMask];
            assert(entry.load(std::memory_order_relaxed) == static_cast<sequence_t>(sequence - m_bufferSize));
            entry.store(sequence, std::memory_order_release);
        }
    
        const sequence_t m_indexMask;
        const size_t m_bufferSize;
        
        WaitStrategy& m_waitStrategy;
        
        sequence_barrier_group<WaitStrategy> m_claimBarrier;
        
        const std::unique_ptr<std::atomic<sequence_t>[]> m_published;

        // Since this m_nextClaimable is going to be written to by multiple
        // threads, we don't want false sharing with m_published or other
        // variables that occur after it in the heap/stack.
        uint8_t m_pad0[CacheLineSize - sizeof(sequence_t)];
        std::atomic<sequence_t> m_nextClaimable;
        uint8_t m_pad1[CacheLineSize - sizeof(sequence_t)];
        
    };
}

#endif
