#include "testkit.h"
#include "plugins/chordplayer/chordsymbol.h"
#include "plugins/chordplayer/chordvoicer.h"

#include <algorithm>
#include <cstdlib>
#include <set>

using namespace chords;

TEST(chordvoicer, the_right_hand_moves_little_between_chords_and_the_bass_stays_low)
{
    const Chord c = *parse("C"), g = *parse("G"), am = *parse("Am"), f = *parse("F"), e = *parse("E");
    const auto first = voice(c, {});
    CHECK(first.size() == 3);
    CHECK(first.front() >= RightHand.low && first.back() <= RightHand.high);
    CHECK(std::is_sorted(first.begin(), first.end()));
    CHECK(first.back() - first.front() < 12);                              // close position
    std::set<int> classes; for (int n : first) classes.insert(n % 12);
    CHECK(classes == (std::set<int>{0, 4, 7}));

    // C -> G: a pianist keeps the shared G and moves the rest a step (B D G over C E G), not a
    // jump of the whole hand up a fifth.
    const auto toG = voice(g, first);
    std::set<int> gClasses; for (int n : toG) gClasses.insert(n % 12);
    CHECK(gClasses == (std::set<int>{7, 11, 2}));
    CHECK(movement(first, toG) <= 6);
    const auto naive = std::vector<int>{first[0] + 7, first[1] + 7, first[2] + 7};
    CHECK(movement(first, toG) < movement(first, naive));

    // Am G F E steps like a pianist's hand, never jumping.
    std::vector<int> previous = voice(am, {});
    for (const Chord* next : {&g, &f, &e, &am}) {
        const auto v = voice(*next, previous);
        CHECK(v.size() == 3 && v.front() >= RightHand.low && v.back() <= RightHand.high);
        // Every voice moves at most a whole tone to its nearest neighbour, both ways.
        const auto nearest = [](int n, const std::vector<int>& others) {
            int best = 99; for (int o : others) best = std::min(best, std::abs(n - o)); return best;
        };
        for (int n : v) CHECK(nearest(n, previous) <= 2);
        for (int n : previous) CHECK(nearest(n, v) <= 2);
        previous = v;
    }

    // Four notes at most: a ninth chord drops its fifth.
    const auto c9 = voice(*parse("C9"), {});
    CHECK(c9.size() == 4);
    std::set<int> nine; for (int n : c9) nine.insert(n % 12);
    CHECK(nine == (std::set<int>{0, 4, 10, 2}));

    // The bass: the root low in the left hand, or the slash bass; near the bass before.
    const int bassC = bassNote(c);
    CHECK(bassC % 12 == 0 && bassC >= BassRange.low && bassC <= BassRange.high);
    CHECK(bassNote(*parse("G/B")) % 12 == 11);
    const int bassG = bassNote(g, bassC);
    CHECK(bassG % 12 == 7 && std::abs(bassG - bassC) <= 6);
    CHECK(bassNote(f, 40) == 41);
}
