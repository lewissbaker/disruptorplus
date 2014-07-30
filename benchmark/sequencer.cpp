#include <disruptorplus/single_threaded_claim_strategy.hpp>
#include <disruptorplus/multi_threaded_claim_strategy.hpp>
#include <disruptorplus/blocking_wait_strategy.hpp>
#include <disruptorplus/spin_wait_strategy.hpp>
#include <disruptorplus/ring_buffer.hpp>

#include <thread>
#include <chrono>
#include <cstdint>
#include <iostream>

namespace
{
    template<typename WaitStrategy, template<typename T> class ClaimStrategy>
    uint64_t CalculateOpsPerSecond(size_t bufferSize, uint64_t iterationCount, int producerCount)
    {
        WaitStrategy waitStrategy;
        ClaimStrategy<WaitStrategy> claimStrategy(bufferSize, waitStrategy);
        disruptorplus::sequence_barrier<WaitStrategy> consumed(waitStrategy);
        claimStrategy.add_claim_barrier(consumed);
        disruptorplus::ring_buffer<uint64_t> buffer(bufferSize);

        const uint64_t expectedResult = producerCount * (iterationCount * (iterationCount - 1)) / 2;

        std::vector<std::thread> producers;
        producers.reserve(producerCount);

        const auto start = std::chrono::high_resolution_clock::now();
        
        // Producers
        for (int producerIndex = 0; producerIndex < producerCount; ++producerIndex)
        {
            producers.emplace_back([&]()
            {
                for (uint64_t i = 0; i < iterationCount; ++i)
                {
                    const auto seq = claimStrategy.claim_one();
                    buffer[seq] = i;
                    claimStrategy.publish(seq);
                }
            });
        }

        // Consumer
        uint64_t sum = 0;
        disruptorplus::sequence_t nextToRead = 0;
        uint64_t itemsRemaining = iterationCount * producerCount;
        while (itemsRemaining > 0)
        {
            const auto available = claimStrategy.wait_until_published(nextToRead, nextToRead - 1);
            do
            {
                sum += buffer[nextToRead];
                --itemsRemaining;
            } while (nextToRead++ != available);
            consumed.publish(available);
        }

		for (int i = 0; i < producerCount; ++i)
		{
			producers[i].join();
		}

        if (sum != expectedResult)
        {
            throw std::domain_error("Unexpected test result.");
        }

        const auto timeTaken = std::chrono::high_resolution_clock::now() - start;
        const auto timeTakenUS = std::chrono::duration_cast<std::chrono::microseconds>(timeTaken).count();

        return (producerCount * iterationCount * 1000 * 1000) / timeTakenUS;
    }
}

int main()
{
    const int producerCount = 3;
    const size_t bufferSize = 64 * 1024;
    const uint64_t iterationCount = 10 * 1000 * 1000;
    const uint32_t runCount = 5;
    
    std::cout << "Multicast Throughput Benchmark" << std::endl
              << "Producer count: " << producerCount << std::endl
              << "Buffer size: " << bufferSize << std::endl
              << "Iteration count: " << iterationCount << std::endl
              << "Run count: " << runCount << std::endl;

    try
    {
#define BENCHMARK(CS,WS) \
        do { \
            std::cout << #CS "/" #WS << std::endl; \
            for (uint32_t run = 1; run <= runCount; ++run) \
            { \
                const auto opsPerSecond = CalculateOpsPerSecond<disruptorplus::WS, disruptorplus::CS>(bufferSize, iterationCount, producerCount); \
                std::cout << "run " << run << " " << opsPerSecond << " ops/sec" << std::endl; \
            } \
        } while (false)

        BENCHMARK(multi_threaded_claim_strategy, spin_wait_strategy);
        BENCHMARK(multi_threaded_claim_strategy, blocking_wait_strategy);
    }
    catch (std::exception& e)
    {
        std::cout << "error: " << e.what() << std::endl;
        return 1;
    }

    return 0;
}