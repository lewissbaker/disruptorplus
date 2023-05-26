Disruptor++
===========

Disruptor++ is a C++11 header-only implementation of the 'disruptor' data structure used
to communicate between threads in a high-performance producer/consumer arrangement.

See the LMAX [technical paper](https://lmax-exchange.github.io/disruptor/files/Disruptor-1.0.pdf)
for a description of the theory behind the disruptor.

See the [Java LMAX Disruptor project](http://lmax-exchange.github.io/disruptor/) for
more resources relating to the disruptor.

Description
-----------

A disruptor data structure is essentially a ring buffer that uses different cursors
to keep track of where consumers or producers have processed up to.

A disruptor can be used with either single producer thread or multiple producer
threads. Multiple producer threads are able to publish items out-of-sequence
so that producer threads do not hold up other producer threads unless the
entire ring buffer fills up while waiting for a slow producer to publish its item.
Consumers, however, always process items in the sequence they were enqueued.

The disruptor data structure supports batched operations with producers able to
enqueue multiple items with a single synchronisation operation and consumers
that fall behind are able to catch up by consuming batches of consecutive
items with a single synchronisation operation.

When used with a single producer thread with the spin wait-strategy the disruptor
uses only atomic reads/writes of integers and acquire/release memory barriers for
synchronisation. It does not use CAS operations or locks that require kernel
arbitration.

This implementation of the disruptor data-structure is defined fully in header files.
There are no libraries to link against and many functions should be able to be inlined.
Also, this implementation does not make use of abstract interfaces or virtual function
calls, which can inhibit inlining and incur additional runtime overhead, but instead
prefers to use templates for compile-time polymorphism.

Performance
-----------

TODO: Put some performance results in here.

Synopsis
--------

A single producer/single consumer use of a disruptor for communication.

```
#include <disruptorplus/ring_buffer.hpp>
#include <disruptorplus/single_threaded_claim_strategy.hpp>
#include <disruptorplus/spin_wait_strategy.hpp>
#include <disruptorplus/sequence_barrier.hpp>

#include <iostream>
#include <thread>

using namespace disruptorplus;

struct Event
{
    uint32_t data;
};

int main()
{
    const size_t bufferSize = 1024; // Must be power-of-two
    
    ring_buffer<Event> buffer(bufferSize);
    
    spin_wait_strategy waitStrategy;
    single_threaded_claim_strategy<spin_wait_strategy> claimStrategy(bufferSize, waitStrategy);
    sequence_barrier<spin_wait_strategy> consumed(waitStrategy);
    claimStrategy.add_claim_barrier(consumed);
    
    std::thread consumer([&]()
    {
        uint64_t sum = 0;
        sequence_t nextToRead = 0;
        bool done = false;
        while (!done)
        {
            // Wait until more items available
            sequence_t available = claimStrategy.wait_until_published(nextToRead);
            
            // Process all available items in a batch
            do
            {
                auto& event = buffer[nextToRead];
                sum += event.data;
                if (event.data == 0)
                {
                    done = true;
                }
            } while (nextToRead++ != available);
            
            // Notify producer we've finished consuming some items
            consumed.publish(available);
        }
        std::cout << "sum is " << sum << std::endl;
    });
    
    std::thread producer([&]()
    {
        for (uint32_t i = 1; i <= 1000000; ++i)
        {
            // Claim a slot in the ring buffer, waits if buffer is full
            sequence_t seq = claimStrategy.claim_one();
            
            // Write to the slot in the ring buffer
            buffer[seq].data = i;
            
            // Publish the event to the consumer
            claimStrategy.publish(seq);
        }
        
        // Publish the terminating event.
        sequence_t seq = claimStrategy.claim_one();
        buffer[seq].data = 0;
        claimStrategy.publish(seq);
    });
    
    consumer.join();
    producer.join();
    
    return 0;
}
```

License
-------

The Discruptor++ library is available under the MIT open source license.
See LICENSE.txt for details.

Building
--------

As this library is a header-only library there is no library component
that needs to be built separately. Simply add the include/ directory
to your include path and include the appropriate headers.

If you want to build the samples/tests then you can use the
[Cake](https://github.com/lewissbaker/cake) build system to build
this code.

Once you have installed cake you can simply run 'cake' in the root
directory to build everything.
