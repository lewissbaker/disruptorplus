#include <disruptorplus/ring_buffer.hpp>
#include <disruptorplus/single_threaded_claim_strategy.hpp>
#include <disruptorplus/multi_threaded_claim_strategy.hpp>
#include <disruptorplus/blocking_wait_strategy.hpp>
#include <disruptorplus/spin_wait_strategy.hpp>
#include <disruptorplus/sequence_barrier.hpp>

#include <iostream>
#include <thread>
#include <cassert>
#include <vector>
#include <algorithm>
#include <chrono>

#ifdef _MSC_VER
# include <windows.h>
#endif

using namespace disruptorplus;

#ifdef _MSC_VER

// Workaround inadequate precision of builtin high res clock under msvc.
struct high_resolution_clock
{
    typedef int64_t                                rep;
    typedef std::nano                               period;
    typedef std::chrono::duration<rep, period>      duration;
    typedef std::chrono::time_point<high_resolution_clock>   time_point;
    static const bool is_steady = true;

    static time_point now()
    {
        LARGE_INTEGER count;
        QueryPerformanceCounter(&count);
        return time_point(duration(count.QuadPart * static_cast<rep>(period::den) / s_freq));
    }
    
private:
    
    static const int64_t s_freq;
    
};

const int64_t high_resolution_clock::s_freq = []() -> uint64_t
{
    LARGE_INTEGER frequency;
    QueryPerformanceFrequency(&frequency);
    return frequency.QuadPart;
}();

#else

typedef std::chrono::high_resolution_clock high_resolution_clock;

#endif

struct message
{
    uint32_t m_type;
    high_resolution_clock::time_point m_time;
};

template<size_t writerBatchSize, typename WaitStrategy>
void RunSingleThreadClaimStrategyBenchmark(size_t bufferSize, int runCount, int itemCount)
{
    WaitStrategy waitStrategy;
    single_threaded_claim_strategy<WaitStrategy> claimStrategy(bufferSize, waitStrategy);
    sequence_barrier<WaitStrategy> finishedReading(waitStrategy);
    claimStrategy.add_claim_barrier(finishedReading);
    ring_buffer<message> buffer(bufferSize);

    std::vector<std::chrono::nanoseconds> times;
    std::vector<uint64_t> results;
    times.reserve(runCount);
    results.reserve(runCount);
    
    sequence_t nextToRead = 0;
    
    const size_t maxLatencyCount = 10000000;
    std::vector<uint64_t> latencies(maxLatencyCount + 2, 0);
    
    for (int run = 0; run < runCount; ++run)
    {
        auto start = high_resolution_clock::now();
        
        uint64_t result;
        std::thread reader([&]() {
            bool exit = false;
            uint64_t sum = 0;
            while (!exit)
            {
                sequence_t available = claimStrategy.wait_until_published(nextToRead);
                assert(difference(available, nextToRead) >= 0);
                auto readTime = high_resolution_clock::now();
                do
                {
                    auto& message = buffer[nextToRead];
                    //auto latency = readTime - message.m_time;
                    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(readTime - message.m_time);
                    ++latencies[std::max<uint64_t>(std::min<uint64_t>(latency.count(), maxLatencyCount) + 1, 0)];
                    if (message.m_type == 0xdead)
                    {
                        exit = true;
                    }
                } while (nextToRead++ != available);
                finishedReading.publish(available);
            }
            result = sum;
        });
        
        std::thread writer([&]() {
            size_t remaining = itemCount;
            while (remaining > 0)
            {
                sequence_range range;
                if (writerBatchSize == 1)
                {
                    range = sequence_range(claimStrategy.claim_one(), 1);
                }
                else
                {
                    range = claimStrategy.claim(std::min(writerBatchSize, remaining));
                }
                sequence_t seq = range.first();
                const sequence_t seqEnd = range.end();
                auto writeTime = high_resolution_clock::now();
                while (seq != seqEnd)
                {
                    auto& item = buffer[seq];
                    item.m_type = 0;
                    item.m_time = writeTime;
                    ++seq;
                }
                claimStrategy.publish(range.last());
                remaining -= range.size();
            }
            
            sequence_t seq = claimStrategy.claim_one();
            auto& item = buffer[seq];
            item.m_type = 0xdead;
            item.m_time = high_resolution_clock::now();
            claimStrategy.publish(seq);
        });
        
        reader.join();
        writer.join();
        
        auto end = high_resolution_clock::now();
        
        auto dur = (end - start);
        auto durNS = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
        
        times.push_back(durNS);
        results.push_back(result);
    }
    
    if (results.size() > 1)
    {
        auto firstResult = results.front();
        for (size_t i = 1; i < results.size(); ++i)
        {
            assert(results[i] == firstResult);
        }
    }
    
    auto minTime = *std::min_element(times.begin(), times.end());
    auto maxTime = *std::max_element(times.begin(), times.end());
    
    uint64_t totalItems = itemCount + 1;
    auto minItemsPerSecond = (totalItems * 1000000000) / maxTime.count();
    auto maxItemsPerSecond = (totalItems * 1000000000) / minTime.count();
    
    int64_t minLatency = latencies.size();
    int64_t maxLatency = 0;
    int64_t totalLatency = 0;
    int64_t totalCount = 0;
    for (size_t i = 1; i < latencies.size(); ++i)
    {
        int64_t latency = i - 1;
        if (latencies[i] > 0)
        {
            totalLatency += latencies[i] * latency;
            totalCount += latencies[i];
            minLatency = std::min(minLatency, latency);
            maxLatency = std::max(maxLatency, latency);
        }
    }
    int64_t avgLatency = totalLatency / totalCount;

    std::cout << bufferSize << ", "
              << writerBatchSize << ", "
              << itemCount << ", "
              << runCount << ", "
              << results.front() << ", "
              << minItemsPerSecond << ", "
              << maxItemsPerSecond << ", "
              << minLatency << ", "
              << avgLatency << ", "
              << maxLatency << std::endl;
}

template<typename WaitStrategy>
void RunSingleThreadClaimStrategyBenchmarkVariousBatchSizes(size_t bufferSize, int runCount, int itemCount)
{
    RunSingleThreadClaimStrategyBenchmark<1, WaitStrategy>(bufferSize, runCount, itemCount);
    RunSingleThreadClaimStrategyBenchmark<2, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<3, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<4, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<8, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<16, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<32, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<64, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<128, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<256, WaitStrategy>(bufferSize, runCount, itemCount);
    //RunSingleThreadClaimStrategyBenchmark<500, WaitStrategy>(bufferSize, runCount, itemCount);
}

template<typename WaitStrategy>
void RunSingleThreadClaimStrategyBenchmarkVariousBufferSizes(int runCount, int itemCount)
{
    std::cout << "BufferSize" << ", "
              << "WriterBatchSize" << ", "
              << "ItemCount" << ", "
              << "RunCount" << ", "
              << "Result" << ", "
              << "MinNSPerItem" << ", "
              << "MaxNSPerItem" << ", "
              << "MinLatency" << ", "
              << "AvgLatency" << ", "
              << "MaxLatency" << std::endl;

    for (size_t bufferSize = 256; bufferSize <= 1024 * 1024; bufferSize *= 8)
    {
        RunSingleThreadClaimStrategyBenchmarkVariousBatchSizes<WaitStrategy>(bufferSize, runCount, itemCount);
    }
}

template<size_t writerBatchSize, typename WaitStrategy>
void RunMultiThreadClaimStrategyBenchmark(size_t bufferSize, int writerCount, int runCount, int itemCount)
{
    WaitStrategy waitStrategy;
    multi_threaded_claim_strategy<WaitStrategy> claimStrategy(bufferSize, waitStrategy);
    sequence_barrier<WaitStrategy> finishedReading(waitStrategy);
    claimStrategy.add_claim_barrier(finishedReading);
    ring_buffer<message> buffer(bufferSize);

    std::vector<std::chrono::nanoseconds> times;
    std::vector<uint64_t> results;
    times.reserve(runCount);
    results.reserve(runCount);
    
    sequence_t nextToRead = 0;
    
    const size_t maxLatencyCount = 1000000;
    std::vector<uint64_t> latencies(maxLatencyCount + 2, 0);
    
    for (int run = 0; run < runCount; ++run)
    {
        auto start = high_resolution_clock::now();
        
        uint64_t result;
        std::thread reader([&]() {
            int exitCount = writerCount;
            uint64_t sum = 0;
            while (exitCount > 0)
            {
                sequence_t available = claimStrategy.wait_until_published(nextToRead, nextToRead - 1);
                assert(difference(available, nextToRead) >= 0);
                auto readTime = high_resolution_clock::now();
                do
                {
                    auto& message = buffer[nextToRead];
                    //auto latency = readTime - message.m_time;
                    auto latency = std::chrono::duration_cast<std::chrono::nanoseconds>(readTime - message.m_time);
                    ++latencies[std::max<uint64_t>(std::min<uint64_t>(latency.count(), maxLatencyCount) + 1, 0)];
                    if (message.m_type == 0xdead)
                    {
                        --exitCount;
                    }
                } while (nextToRead++ != available);
                finishedReading.publish(available);
            }
            result = sum;
        });
        
        std::vector<std::thread> writers;
        for (int w = 0; w < writerCount; ++w)
        {
            writers.emplace_back([&]() {
                size_t remaining = itemCount;
                while (remaining > 0)
                {
                    sequence_range range;
                    if (writerBatchSize == 1)
                    {
                        range = sequence_range(claimStrategy.claim_one(), 1);
                    }
                    else
                    {
                        range = claimStrategy.claim(std::min(writerBatchSize, remaining));
                    }
                    sequence_t seq = range.first();
                    const sequence_t seqEnd = range.end();
                    auto writeTime = high_resolution_clock::now();
                    while (seq != seqEnd)
                    {
                        auto& item = buffer[seq];
                        item.m_type = 0;
                        item.m_time = writeTime;
                        ++seq;
                    }
                    claimStrategy.publish(range);
                    remaining -= range.size();
                }
                
                sequence_t seq = claimStrategy.claim_one();
                auto& item = buffer[seq];
                item.m_type = 0xdead;
                item.m_time = high_resolution_clock::now();
                claimStrategy.publish(seq);
            });
        }
        
        reader.join();
        for (auto& writer : writers)
        {
            writer.join();
        }
        
        auto end = high_resolution_clock::now();
        
        auto dur = (end - start);
        auto durNS = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
       
        times.push_back(durNS);
        results.push_back(result);
    }
    
    if (results.size() > 1)
    {
        auto firstResult = results.front();
        for (size_t i = 1; i < results.size(); ++i)
        {
            assert(results[i] == firstResult);
        }
    }
    
    auto minTimeNS = *std::min_element(times.begin(), times.end());
    auto maxTimeNS = *std::max_element(times.begin(), times.end());
    
    int64_t minLatency = latencies.size();
    int64_t maxLatency = 0;
    int64_t totalLatency = 0;
    int64_t totalCount = 0;
    for (size_t i = 1; i < latencies.size(); ++i)
    {
        int64_t latency = i - 1;
        if (latencies[i] > 0)
        {
            totalLatency += latencies[i] * latency;
            totalCount += latencies[i];
            minLatency = std::min(minLatency, latency);
            maxLatency = std::max(maxLatency, latency);
        }
    }
    int64_t avgLatency = totalLatency / totalCount;

    uint64_t totalItemCount = (itemCount + 1) * writerCount;
    uint64_t minItemsPerSecond = (totalItemCount * 1000000000) / maxTimeNS.count();
    uint64_t maxItemsPerSecond = (totalItemCount * 1000000000) / minTimeNS.count();
    
    std::cout << bufferSize << ", "
              << writerCount << ", "
              << writerBatchSize << ", "
              << itemCount << ", "
              << runCount << ", "
              << results.front() << ", "
              << minItemsPerSecond << ", "
              << maxItemsPerSecond << ", "
              << minLatency << ", "
              << avgLatency << ", "
              << maxLatency << std::endl;
}

template<typename WaitStrategy>
void RunMultiThreadClaimStrategyBenchmarkVariousBatchSizes(size_t bufferSize, int writerCount, int runCount, int itemCount)
{
    RunMultiThreadClaimStrategyBenchmark<1, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    RunMultiThreadClaimStrategyBenchmark<2, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<3, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<4, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<8, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<16, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<32, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<64, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<128, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<256, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
    //RunMultiThreadClaimStrategyBenchmark<500, WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
}

template<typename WaitStrategy>
void RunMultiThreadClaimStrategyBenchmarkVariousBufferSizes(int runCount, int itemCount)
{
    std::cout << "BufferSize" << ", "
              << "WriterThreads" << ", "
              << "WriterBatchSize" << ", "
              << "ItemCount" << ", "
              << "RunCount" << ", "
              << "Result" << ", "
              << "MinItems/Sec" << ", "
              << "MaxItems/Sec" << ", "
              << "MinLatency" << ", "
              << "AvgLatency" << ", "
              << "MaxLatency" << std::endl;

    for (size_t bufferSize = 256; bufferSize <= 1024 * 1024; bufferSize *= 8)
    {
        for (int writerCount = 1; writerCount < 4; ++writerCount)
        {
            RunMultiThreadClaimStrategyBenchmarkVariousBatchSizes<WaitStrategy>(bufferSize, writerCount, runCount, itemCount);
        }
    }
}

int main(int argc, char* argv[])
{
    std::cout << "Single Blocking Wait Strategy\n"
              << "----------------------" << std::endl;
    RunSingleThreadClaimStrategyBenchmarkVariousBufferSizes<blocking_wait_strategy>(2, 1000 * 1000);
    
    std::cout << "Single Spin Wait Strategy\n"
              << "------------------" << std::endl;
    RunSingleThreadClaimStrategyBenchmarkVariousBufferSizes<spin_wait_strategy>(2, 1000 * 1000);

    std::cout << "Multi Blocking Wait Strategy\n"
              << "----------------------" << std::endl;
    RunMultiThreadClaimStrategyBenchmarkVariousBufferSizes<blocking_wait_strategy>(2, 1000 * 1000);
    
    std::cout << "Multi Spin Wait Strategy\n"
              << "------------------" << std::endl;
    RunMultiThreadClaimStrategyBenchmarkVariousBufferSizes<spin_wait_strategy>(2, 1000 * 1000);
    
    return 0;
}
