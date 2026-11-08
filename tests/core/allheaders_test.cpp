// Includes the planned headers, so a change that breaks one fails here and not months later.
#include "testkit.h"

#include "audiodevice/audiodevice.h"
#include "core/audiodata.h"
#include "core/audioview.h"
#include "engine/audioengine.h"
#include "engine/enginecommands.h"
#include "engine/enginesnapshot.h"
#include "instruments/sampler/samplezone.h"
#include "midi/midievent.h"
#include "plugins/chordplayer/rhythmpattern.h"
#include "processor/parameter.h"
#include "processor/processor.h"
#include "processor/smoother.h"
#include "rt/rtqueue.h"
#include "rt/rtsafe.h"
#include "rt/snapshotslot.h"
#include "session/project.h"
#include "transport/tempomap.h"

TEST(allheaders, compile_together)
{
    EngineSnapshot snapshot;
    SampleZone zone;
    chords::Pattern pattern;
    CHECK(snapshot.tracks.empty());
    CHECK(zone.audio == nullptr);
    CHECK(pattern.count == 0);
}
