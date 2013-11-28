#ifndef DISRUPTORPLUS_BLOCKING_WAIT_STRATEGY_HPP_INCLUDED
#define DISRUPTORPLUS_BLOCKING_WAIT_STRATEGY_HPP_INCLUDED

#include <disruptorplus/sequence.hpp>

#include <atomic>
#include <cassert>
#include <chrono>
#include <condition_variable>
#include <mutex>

namespace disruptorplus
{
    // A wait_strategy that blocks waiting threads until the respective
    // sequence barriers are have reached the desired sequence numbers.
    //
    // All currently blocked threads will be woken when any sequence
    // barrier publishes a new sequence regardless of whether those
    // threads are currently waiting on that sequence barrier or not.
    class blocking_wait_strategy
    {
    public:
    
        blocking_wait_strategy()
        {}
        
        // Wait unconditionally until all of the specified sequences
        // have at least published the specified sequence value.
        // Returns the value of the least-advanced sequence.
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[])
        {
            assert(count > 0);
            sequence_t result;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait(lock, [&]() -> bool {
                    result = minimum_sequence_after(sequence, count, sequences);
                    return difference(result, sequence) >= 0;
                    });
            }
            return result;
        }
        
        // Wait until all of the specified sequences have at least
        // published the specified sequence value.
        // Timeout if waited longer than specified duration.
        // Returns the highest sequence that all sequences have
        // published if did not time out.
        // If timed out then returns some number such that
        // difference(result, sequence) < 0.
        template<typename Rep, typename Period>
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[],
            const std::chrono::duration<Rep, Period>& timeout)
        {
            assert(count > 0);
            sequence_t result;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_for(
                    lock,
                    [&]() -> bool {
                        result = minimum_sequence_after(sequence, count, sequences);
                        return difference(result, sequence) >= 0;
                    },
                    timeout);
            }
            return result;
        }

        // Wait until all of the specified sequences have at least
        // published the specified sequence value.
        // Timeout if specified timeoutTime has passed.
        // Returns the highest sequence that all sequences have
        // published if did not time out.
        // If timed out then returns some number such that
        // difference(result, sequence) < 0.
        template<typename Clock, typename Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[],
            const std::chrono::time_point<Clock, Duration>& timeoutTime)
        {
            assert(count > 0);
            sequence_t result;
            {
                std::unique_lock<std::mutex> lock(m_mutex);
                m_cv.wait_until(
                    lock,
                    [&]() -> bool {
                        result = minimum_sequence_after(sequence, count, sequences);
                        return difference(result, sequence) >= 0;
                    },
                    timeoutTime);
            }
            return result;
        }

        // Signal any waiting threads that one of the sequences has changed.
        void signal_all_when_blocking()
        {
            // Take out a lock here because we don't want to notify other threads
            // if they are between checking the sequence values and waiting on
            // the condition-variable.
            std::unique_lock<std::mutex> lock(m_mutex);
            m_cv.notify_all();
        }
        
    private:
    
        std::mutex m_mutex;
        std::condition_variable m_cv;
    
    };
}

#endif
