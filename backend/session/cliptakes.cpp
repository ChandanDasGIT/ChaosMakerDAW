#include "session/cliptakes.h"
#include "session/project.h"

#include <cmath>

namespace cliptakes {

namespace {

double safeRatio(const RatioOf& ratioOf, const std::string& poolId, bool reversed)
{
    const double ratio = ratioOf ? ratioOf(poolId, reversed) : 1.0;
    return std::isfinite(ratio) && ratio > 0 ? ratio : 1.0;
}

double originOf(const AudioClip& clip, double ratio)
{
    return double(clip.start) - double(clip.sourceOffset) / ratio;
}

} // namespace

int count(const AudioClip& clip) noexcept
{
    return clip.takes.list.empty() ? 1 : int(clip.takes.list.size());
}

int active(const AudioClip& clip) noexcept
{
    return clip.takes.list.empty() ? 0 : clip.takes.active;
}

void set(AudioClip& clip, std::vector<ClipTake> takes, int activeIndex)
{
    if (takes.size() < 2 || activeIndex < 0 || activeIndex >= int(takes.size())) {
        clip.takes = {};
        return;
    }
    takes[std::size_t(activeIndex)] = ClipTake{};
    clip.takes = {std::move(takes), activeIndex};
}

bool activate(AudioClip& clip, int index, const RatioOf& ratioOf)
{
    auto& takes = clip.takes;
    if (takes.list.empty() || index < 0 || index >= int(takes.list.size())) return false;
    if (index == takes.active) return true;
    const double oldRatio = safeRatio(ratioOf, clip.poolId, clip.reversed);
    const double oldOrigin = originOf(clip, oldRatio);
    auto& next = takes.list[std::size_t(index)];
    const double newRatio = safeRatio(ratioOf, next.poolId, next.reversed);
    const double wanted = oldOrigin + next.lead;
    const double offset = (double(clip.start) - wanted) * newRatio;
    const auto newOffset = std::llround(offset);
    if (!std::isfinite(offset) || newOffset < 0) return false;

    // The clip now plays `next`; the old active take goes back into the list.
    ClipTake previous{clip.poolId, clip.reversed, 0.0, std::move(clip.notes)};
    clip.poolId = std::move(next.poolId);
    clip.reversed = next.reversed;
    clip.notes = std::move(next.notes);
    clip.sourceOffset = newOffset;
    next = ClipTake{};
    // Re-base every lead on the new active origin (its real one, after rounding).
    const double delta = originOf(clip, newRatio) - oldOrigin;
    previous.lead = -delta;
    takes.list[std::size_t(takes.active)] = std::move(previous);
    for (std::size_t i = 0; i < takes.list.size(); ++i)
        if (int(i) != index && int(i) != takes.active) takes.list[i].lead -= delta;
    takes.active = index;
    return true;
}

bool remove(AudioClip& clip, int index, const RatioOf& ratioOf)
{
    auto& takes = clip.takes;
    if (takes.list.empty() || index < 0 || index >= int(takes.list.size())) return false;
    if (index == takes.active) {
        // The next take that can play here takes over (after the last: from the first).
        const int size = int(takes.list.size());
        bool replaced = false;
        for (int k = 1; k < size && !replaced; ++k) replaced = activate(clip, (index + k) % size, ratioOf);
        if (!replaced) return false;
    }
    takes.list.erase(takes.list.begin() + index);
    if (takes.active > index) --takes.active;
    if (takes.list.size() < 2) takes = {};
    return true;
}

void cropToActive(AudioClip& clip) noexcept
{
    clip.takes = {};
}

void activeOriginMoved(AudioClip& clip, double delta) noexcept
{
    for (std::size_t i = 0; i < clip.takes.list.size(); ++i)
        if (int(i) != clip.takes.active) clip.takes.list[i].lead -= delta;
}

} // namespace cliptakes
