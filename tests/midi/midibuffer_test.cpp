#include "testkit.h"

#include "midi/midievent.h"

TEST(midibuffer, refuses_events_when_full)
{
    MidiBuffer buffer;
    int added = 0;
    while (buffer.add(MidiEvent{})) ++added;
    CHECK(added == 1024);
    CHECK(buffer.events().size() == 1024);

    buffer.clear();
    CHECK(buffer.events().empty());
}
