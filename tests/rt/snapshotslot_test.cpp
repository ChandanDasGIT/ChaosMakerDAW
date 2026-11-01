#include "testkit.h"

#include "rt/snapshotslot.h"

#include <atomic>
#include <thread>

namespace {
struct Snapshot {
    int value = 0;
};
}

TEST(snapshotslot, gives_the_newest_and_returns_the_old_one)
{
    SnapshotSlot<Snapshot> slot;
    CHECK(slot.acquire() == nullptr);

    auto* first = new Snapshot{1};
    slot.publish(first);
    CHECK(slot.acquire() == first);
    CHECK(slot.collect() == nullptr);                      // the first one is still in use

    auto* second = new Snapshot{2};
    slot.publish(second);
    CHECK(slot.acquire() == second);
    CHECK(slot.collect() == first);

    delete first;
    delete second;
}

// One thread plays the UI and one plays audio. Every snapshot must be freed once, by the UI thread.
TEST(snapshotslot, frees_every_snapshot_once)
{
    SnapshotSlot<Snapshot> slot;
    std::atomic<bool> stop{false};
    std::atomic<bool> wentBackwards{false};

    std::thread audio([&] {
        int last = -1;
        while (!stop) {
            const Snapshot* snapshot = slot.acquire();
            if (!snapshot) continue;
            if (snapshot->value < last) wentBackwards = true;
            last = snapshot->value;
        }
    });

    const int count = 100'000;
    int freed = 0;
    auto collectAll = [&] {
        while (Snapshot* old = slot.collect()) {
            delete old;
            ++freed;
        }
    };
    for (int i = 0; i < count; ++i) {
        while (!slot.pendingEmpty()) collectAll();
        slot.publish(new Snapshot{i});
        collectAll();
    }
    while (!slot.pendingEmpty()) collectAll();

    stop = true;
    audio.join();
    collectAll();
    delete slot.acquire();                                 // the one still current
    CHECK(freed == count - 1);
    CHECK(!wentBackwards);
}
