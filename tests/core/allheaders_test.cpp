// Includes the planned headers, so a change that breaks one fails here and not months later.
#include "testkit.h"

#include "common/audiodevice/audiodevice.h"
#include "common/core/audiodata.h"
#include "common/core/audioview.h"
#include "common/engine/audioengine.h"
#include "common/engine/enginecommands.h"
#include "common/engine/enginesnapshot.h"
#include "common/instruments/sampler/samplezone.h"
#include "common/midi/midievent.h"
#include "common/chordPlayer/rhythmpattern.h"
#include "common/processor/parameter.h"
#include "common/processor/processor.h"
#include "common/processor/smoother.h"
#include "common/rt/rtqueue.h"
#include "common/rt/rtsafe.h"
#include "common/rt/snapshotslot.h"
#include "common/session/project.h"
#include "common/transport/tempomap.h"

TEST(allheaders, compile_together)
{
    EngineSnapshot snapshot;
    SampleZone zone;
    chords::Pattern pattern;
    CHECK(snapshot.tracks.empty());
    CHECK(zone.audio == nullptr);
    CHECK(pattern.count == 0);
}
