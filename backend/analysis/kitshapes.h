#pragma once

#include "analysis/spectrogram.h"

#include <array>
#include <vector>

// This recording's own drum sounds (restMap G20), learnt from it before the drums are told
// apart: every kit is tuned, miked and mixed differently, so one fixed "kick / snare / hi-hat"
// shape fits few of them.
//
//   1. The hits: peaks of the rise of the (compressed) drum part.
//   2. Each hit's spectrum as it rises, grouped by likeness (k-means).
//   3. A group that is two other groups sounding together (kick + hat, snare + hat) is left
//      out: what remains are the sounds heard alone, or never without the same partner.
//   4. Of those: the kick is the most played one with its level in the lows, the hi-hat the
//      brightest one with hardly any lows, the snare the most played of the rest that has a
//      body. The others (toms, a crash) are "other": known, and kept out of the drums.
//
// A drum that was not found has no group: the caller's generic shape is used for it.
namespace analysis {

// What was heard, for drumbench: every group with what it was taken for.
struct KitGroup {
    std::vector<float> centre;            // with KitContrast: what the groups are told apart by
    std::vector<float> plain;             // as the factorisation sees it
    int count = 0;                         // hits in it
    double level = 0.0;                    // their mean level
    bool mix = false;                      // two other groups together
    int role = -1;                         // 0 kick, 1 snare, 2 hat, 3 other, -1 none (a mix)
};

struct KitShapes {
    std::vector<KitGroup> groups;
    int hits = 0;                          // the hits it was learnt from

    bool has(int role) const noexcept
    {
        for (const auto& g : groups)
            if (g.role == role) return true;
        return false;
    }
};

constexpr int KitGroups = 10;               // k-means groups at most
constexpr int KitMinHits = 12;             // fewer hits than this: nothing is learnt
constexpr float KitHitAt = 0.2f;           // a hit to learn from reaches this part of a strong one
constexpr double KitSame = 0.97;           // groups this alike (cosine) are one sound
constexpr double KitPart = 0.25;           // a part of a mix weighs at least this
constexpr double KitPartHits = 0.04;       // ... and is a group of its own with this share of the hits (3 at least)
constexpr double KitMixError = 0.3;        // ... and the mix is made of its parts this closely
constexpr double KitKickLows = 0.3;        // a kick has more than this below 150 Hz
constexpr double KitHatHighs = 0.25;       // a hi-hat has more than this above 4 kHz
constexpr double KitHatLows = 0.15;        // ... and less than this below 200 Hz
constexpr double KitSnareBody = 0.35;      // a snare has this much from 150 Hz to 1.5 kHz
constexpr float KitContrast = 1.5f;        // the levels' power when groups are told apart

// `cells`: the drum part in the spectrogram's layout (frames * bands), already compressed.
KitShapes learnKit(const BandSpectrogram& spectrogram, const std::vector<float>& cells);

} // namespace analysis
