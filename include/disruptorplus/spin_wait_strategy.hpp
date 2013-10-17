#ifndef DISRUPTORPLUS_SPIN_WAIT_STRATEGY_HPP_INCLUDED
#define DISRUPTORPLUS_SPIN_WAIT_STRATEGY_HPP_INCLUDED

#include <disruptorplus/spin_wait.hpp>
#include <disruptorplus/sequence.hpp>

#include <chrono>
#include <atomic>
#include <cassert>

namespace disruptorplus
{
    class spin_wait_strategy
    {
    public:
    
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
        
        void signal_all_when_blocking()
        {
            // No need to signal any threads, they are all busy-waiting.
        }
    
    };
}

#endif
