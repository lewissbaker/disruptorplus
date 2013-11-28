#ifndef DISRUPTORPLUS_SPIN_WAIT_STRATEGY_HPP_INCLUDED
#define DISRUPTORPLUS_SPIN_WAIT_STRATEGY_HPP_INCLUDED

#include <disruptorplus/spin_wait.hpp>
#include <disruptorplus/sequence.hpp>

#include <chrono>
#include <atomic>
#include <cassert>

namespace disruptorplus
{
    // This wait strategy uses busy-waits to wait for sequences to be
    // published.
    class spin_wait_strategy
    {
    public:
    
        // Wait unconditionally until all of the specified sequences
        // have at least published the specified sequence value.
        // Returns the value of the least-advanced sequence.
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[])
        {
            assert(count > 0);
            spin_wait spinner;
            sequence_t result = minimum_sequence_after(sequence, count, sequences);
            while (difference(result, sequence) < 0)
            {
                spinner.spin_once();
                result = minimum_sequence_after(sequence, count, sequences);
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
            return wait_until_published(
                sequence,
                count,
                sequences,
                std::chrono::high_resolution_clock::now() + timeout);
        }

        // Wait until either all of the values in the 'sequences' array are at
        // least after the specified 'sequence' or until the specified 'timeoutTime'
        // has passed.
        //
        // The 'sequences' array is assumed to have 'count' elements.
        //
        // Returns the minimum sequence number from 'sequences'. This will be
        // prior to the specified 'sequence' if the operation timed out before
        // the desired sequence number was reached by all sequences.
        template<typename Clock, typename Duration>
        sequence_t wait_until_published(
            sequence_t sequence,
            size_t count,
            const std::atomic<sequence_t>* const sequences[],
            const std::chrono::time_point<Clock, Duration>& timeoutTime)
        {
            assert(count > 0);
            spin_wait spinner;
            sequence_t result = minimum_sequence_after(sequence, count, sequences);
            while (difference(result, sequence) < 0)
            {
                if (spinner.next_spin_will_yield() && timeoutTime < Clock::now())
                {
                    // Out of time.
                    return result;
                }
                spinner.spin_once();
                result = minimum_sequence_after(sequence, count, sequences);
            }
            return result;
        }

        // Signal any waiting threads that one of the sequences has changed.
        void signal_all_when_blocking()
        {
            // This is requred as part of the wait_strategy interface but does nothing
            // for the spin_wait_strategy since all waiting threads are continuously
            // checking the sequence values in a spin-wait loop.
        }
    
    };
}

#endif
