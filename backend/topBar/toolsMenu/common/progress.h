#pragma once

#include <algorithm>
#include <atomic>
#include <functional>

// Progress and cancel for the long analyses (restMap G20-G22), on any thread. A stage reports
// 0..1 of its own work; sub() maps that into a part [from, to) of the whole.
namespace analysis {

struct Progress {
    const std::atomic<bool>* cancel = nullptr;
    std::function<void(double)> report;    // 0..1 of the whole analysis; may be empty
    double from = 0.0;
    double to = 1.0;

    bool cancelled() const noexcept { return cancel && cancel->load(std::memory_order_relaxed); }
    void at(double fraction) const
    {
        if (report) report(from + (to - from) * std::clamp(fraction, 0.0, 1.0));
    }
    Progress sub(double partFrom, double partTo) const
    {
        Progress p = *this;
        p.from = from + (to - from) * partFrom;
        p.to = from + (to - from) * partTo;
        return p;
    }
};

} // namespace analysis
