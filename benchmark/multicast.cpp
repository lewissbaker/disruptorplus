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
    uint64_t CalculateOpsPerSecond(size_t bufferSize, uint64_t iterationCount, int consumerCount)
    {
        WaitStrategy waitStrategy;
        std::vector<std::unique_ptr<disruptorplus::sequence_barrier<WaitStrategy>>> consumedBarriers(consumerCount);
        ClaimStrategy<WaitStrategy> claimStrategy(bufferSize, waitStrategy);
        disruptorplus::ring_buffer<uint64_t> buffer(bufferSize);

        for (int i = 0; i < consumerCount; ++i)
        {
            consumedBarriers[i].reset(new disruptorplus::sequence_barrier<WaitStrategy>(waitStrategy));
            claimStrategy.add_claim_barrier(*consumedBarriers[i]);
        }

        const uint64_t expectedResult = (iterationCount * (iterationCount - 1)) / 2;

        std::vector<uint64_t> results(consumerCount);

        std::vector<std::thread> consumers;
        consumers.reserve(consumerCount);

        // Consumers
        for (int consumerIndex = 0; consumerIndex < consumerCount; ++consumerIndex)
        {
            consumers.emplace_back([&,consumerIndex]()
            {
                uint64_t sum = 0;
                disruptorplus::sequence_t nextToRead = 0;
                uint64_t itemsRemaining = iterationCount;
                auto& barrier = *consumedBarriers[consumerIndex];
                while (itemsRemaining > 0)
                {
                    const auto available = claimStrategy.wait_until_published(nextToRead, nextToRead - 1);
                    do
                    {
                        sum += buffer[nextToRead];
                        --itemsRemaining;
                    } while (nextToRead++ != available);
                    barrier.publish(available);
                }
                
                results[consumerIndex] = sum;
            });
        }

        const auto start = std::chrono::high_resolution_clock::now();

        // Publisher
        for (uint64_t i = 0; i < iterationCount; ++i)
        {
            const auto seq = claimStrategy.claim_one();
            buffer[seq] = i;
            claimStrategy.publish(seq);
        }

        bool resultsOk = true;
        for (int i = 0; i < consumerCount; ++i)
        {
            consumers[i].join();
            if (results[i] != expectedResult)
            {
                resultsOk = false;
            }
        }

        if (!resultsOk)
        {
            throw std::domain_error("Unexpected test result.");
        }

        const auto timeTaken = std::chrono::high_resolution_clock::now() - start;
        const auto timeTakenUS = std::chrono::duration_cast<std::chrono::microseconds>(timeTaken).count();

        return (iterationCount * 1000 * 1000) / timeTakenUS;
    }
}

int main()
{
    const int consumerCount = 3;
    const size_t bufferSize = 64 * 1024;
    const uint64_t iterationCount = 10 * 1000 * 1000;
    const uint32_t runCount = 5;
    
    std::cout << "Multicast Throughput Benchmark" << std::endl
              << "Consumer count: " << consumerCount << std::endl
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
                const auto opsPerSecond = CalculateOpsPerSecond<disruptorplus::WS, disruptorplus::CS>(bufferSize, iterationCount, consumerCount); \
                std::cout << "run " << run << " " << opsPerSecond << " ops/sec" << std::endl; \
            } \
        } while (false)

        BENCHMARK(single_threaded_claim_strategy, spin_wait_strategy);
        BENCHMARK(single_threaded_claim_strategy, blocking_wait_strategy);
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