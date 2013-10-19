#include <disruptorplus/ring_buffer.hpp>
#include <disruptorplus/single_threaded_claim_strategy.hpp>
#include <disruptorplus/blocking_wait_strategy.hpp>
#include <disruptorplus/sequence_barrier.hpp>

#include <iostream>
#include <thread>
#include <cassert>

using namespace disruptorplus;

struct message
{
    uint32_t m_type;
    uint8_t m_data[28];
};

int main(int argc, char* argv[])
{
    const int itemCount = 10000000;
    const size_t bufferSize = size_t(1) << 17;
    blocking_wait_strategy waitStrategy;
    single_threaded_claim_strategy<blocking_wait_strategy> claimStrategy(bufferSize, waitStrategy);
    sequence_barrier<blocking_wait_strategy> finishedReading(waitStrategy);
    claimStrategy.add_claim_barrier(finishedReading);
    ring_buffer<message> buffer(bufferSize);
    
    std::vector<size_t> readerBatchSizes(bufferSize, 0);
    
    auto start = std::chrono::high_resolution_clock::now();

    uint64_t result;
    std::thread reader([&]() {
        bool exit = false;
        uint64_t sum = 0;
        sequence_t nextToRead = 0;
        while (!exit)
        {
            sequence_t available = claimStrategy.wait_until_published(nextToRead);
            assert(difference(available, nextToRead) >= 0);
            ++readerBatchSizes[difference(available, nextToRead)];
            do
            {
                auto& message = buffer[nextToRead];
                if (message.m_type == 0xdead)
                {
                    exit = true;
                }
                else if (message.m_type == 0xadd)
                {
                    for (int i = 0; i < 28; ++i)
                    {
                        sum += message.m_data[i];
                    }
                }
                else if (message.m_type == 0xdec)
                {
                    for (int i = 0; i < 28; ++i)
                    {
                        sum -= message.m_data[i];
                    }
                }
            } while (nextToRead++ != available);
            finishedReading.publish(available);
        }
        result = sum;
    });
    
    std::thread writer([&]() {
        for (int i = 0; i < itemCount; ++i)
        {
            sequence_t seq = claimStrategy.claim_one();
            auto& item = buffer[seq];
            
            item.m_type = i % 5 == 0 ? 0xadd : 0xdec;
            for (int j = 0; j < 28; ++j)
            {
                item.m_data[j] = (i + j) % 60;
            }
            if (seq % 256 == 0)
            {
                claimStrategy.publish(seq);
            }
        }
        
        sequence_t seq = claimStrategy.claim_one();
        auto& item = buffer[seq];
        item.m_type = 0xdead;
        claimStrategy.publish(seq);
    });
    
    reader.join();
    writer.join();

    auto end = std::chrono::high_resolution_clock::now();
    auto dur = (end - start);
    auto durMS = std::chrono::duration_cast<std::chrono::milliseconds>(dur);
    auto durNS = std::chrono::duration_cast<std::chrono::nanoseconds>(dur);
    
    auto nsPerItem = durNS / itemCount;
    
    std::cout << result << "\n"
              << durMS.count() << "ms total time\n"
              << nsPerItem.count() << "ns per item (avg)\n"
              << (1000000000 / nsPerItem.count()) << " items per second (avg)\n"
              << std::flush;

    std::vector<std::pair<size_t, size_t>> sortedBatchSizes;
    for (size_t i = 0; i < readerBatchSizes.size(); ++i)
    {
        if (readerBatchSizes[i] != 0)
        {
            sortedBatchSizes.push_back(std::make_pair(readerBatchSizes[i] * (i + 1), (i + 1)));
        }
    }
    std::sort(sortedBatchSizes.rbegin(), sortedBatchSizes.rend());

    std::cout << "Reader batch sizes:\n";
    for (size_t i = 0; i < 20 && i < sortedBatchSizes.size(); ++i)
    {
        size_t batchSize = sortedBatchSizes[i].second;
        size_t batchItemCount = sortedBatchSizes[i].first;
        size_t percentage = (100 * batchItemCount) / itemCount;
        std::cout << "#" << (i + 1) << ": " << batchSize
                  << " item batch, " << percentage << "%, "
                  << (sortedBatchSizes[i].first / sortedBatchSizes[i].second) << " times\n";
    }
    std::cout << std::flush;
    
    return 0;
}
