#include "testkit.h"

#include "rt/rtqueue.h"

#include <atomic>
#include <cstdint>
#include <thread>

TEST(rtqueue, reports_empty_and_full)
{
    RtQueue<int, 4> queue;
    int value = 0;
    CHECK(!queue.pop(value));
    for (int i = 0; i < 4; ++i) CHECK(queue.push(i));
    CHECK(!queue.push(99));
    CHECK(queue.pop(value));
    CHECK(value == 0);
}

// Two real threads. Worth most under the tsan preset, which watches for data races.
TEST(rtqueue, keeps_order_across_threads)
{
    RtQueue<std::uint64_t, 1024> queue;
    const std::uint64_t count = 2'000'000;
    std::atomic<bool> outOfOrder{false};

    std::thread writer([&] {
        for (std::uint64_t i = 0; i < count;)
            if (queue.push(i)) ++i;
    });
    std::thread reader([&] {
        std::uint64_t expected = 0, value = 0;
        while (expected < count) {
            if (!queue.pop(value)) continue;
            if (value != expected) outOfOrder = true;
            ++expected;
        }
    });
    writer.join();
    reader.join();
    CHECK(!outOfOrder);
}
