#ifndef DISRUPTORPLUS_SPIN_WAIT_HPP_INCLUDED
#define DISRUPTORPLUS_SPIN_WAIT_HPP_INCLUDED

#include <thread>

#ifdef _MSC_VER
# include <intrin.h>
#endif

namespace disruptorplus
{
    class spin_wait
    {
    public:
    
        spin_wait()
        {
            reset();
        }
        
        void reset()
        {
            m_value = std::thread::hardware_concurrency() > 1 ? 0 : 10;
        }
        
        void spin_once()
        {
            // Exponentially longer sequences of busy-waits each
            // time we're called for first 10 calls, then graduating
            // to yielding our time slice with every 20th call then
            // putting the thread to sleep for a short while.
            if (next_spin_will_yield())
            {
                uint32_t count = m_value - 10;
                if (count % 20 == 19)
                {
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                }
                else
                {
                    std::this_thread::yield();
                }
            }
            else
            {
                uint32_t count = 4 << m_value;
                while (count-- != 0)
                {
                    yield_processor();
                }
            }
            m_value = (m_value == 0xFFFFFFFF) ? 10 : m_value + 1;
        }
        
        bool next_spin_will_yield() const
        {
            return m_value >= 10;
        }
    
    private:

        static void yield_processor()
        {
            // Ideally we want to put this processor into idle mode for a few cycles.
#if defined(_MSC_VER) && (defined(_M_IX86) || defined(_M_X64))
            _mm_pause();
#endif
        }
    
        uint32_t m_value;
    
    };
}

#endif
