#include "pedalregistry.h"
#include "plugins/parametriceq/parametriceq.h"
#include "plugins/fuzz/fuzz.h"
#include "plugins/phaser/phaser.h"
#include "plugins/crunch/crunch.h"
#include "plugins/distortion/distortion.h"
#include "plugins/octaver/octaver.h"
#include "plugins/pitchshift/pitchshift.h"
#include "plugins/amp/amp.h"
#include "plugins/cabinet/cabinet.h"
#include "plugins/tuner/tuner.h"
#include "plugins/tremolo/tremolo.h"
#include "plugins/chorus/chorus.h"
#include "plugins/wah/wah.h"
#include "plugins/compressor/compressor.h"
#include "plugins/noisegate/noisegate.h"

#include "plugins/delay/delaypedal.h"
#include "plugins/overdrive/overdrive.h"
#include "plugins/reverb/reverbpedal.h"

QStringList PedalRegistry::availablePedals()
{
    return {QStringLiteral("pedal.noisegate"), QStringLiteral("pedal.overdrive"), QStringLiteral("pedal.crunch"), QStringLiteral("pedal.distortion"), QStringLiteral("pedal.delay"), QStringLiteral("pedal.compressor"), QStringLiteral("pedal.wah"), QStringLiteral("pedal.chorus"), QStringLiteral("pedal.tremolo"), QStringLiteral("pedal.tuner"), QStringLiteral("pedal.reverb"), QStringLiteral("pedal.amp"), QStringLiteral("pedal.cabinet"), QStringLiteral("pedal.octaver"), QStringLiteral("pedal.pitchshift"), QStringLiteral("pedal.parametriceq"), QStringLiteral("pedal.fuzz"), QStringLiteral("pedal.phaser")};
}

QString PedalRegistry::displayName(const QString& type)
{
    if (type == QStringLiteral("pedal.overdrive")) return QStringLiteral("Overdrive");
    if (type == QStringLiteral("pedal.delay")) return QStringLiteral("Delay");
    if (type == QStringLiteral("pedal.reverb")) return QStringLiteral("Reverb");
    if (type == QStringLiteral("pedal.noisegate")) return QStringLiteral("Noise Gate");
    if (type == QStringLiteral("pedal.compressor")) return QStringLiteral("Compressor");
    if (type == QStringLiteral("pedal.wah")) return QStringLiteral("Wah");
    if (type == QStringLiteral("pedal.chorus")) return QStringLiteral("Chorus");
    if (type == QStringLiteral("pedal.tremolo")) return QStringLiteral("Tremolo");
    if (type == QStringLiteral("pedal.tuner")) return QStringLiteral("Tuner");
    if(type == "pedal.amp") return QStringLiteral("Amp");
    if(type == "pedal.cabinet") return QStringLiteral("Cabinet IR");
    if(type == "pedal.octaver") return QStringLiteral("Octaver");
    if(type == "pedal.pitchshift") return QStringLiteral("Pitch Shifter");
    if(type == "pedal.crunch") return QStringLiteral("Crunch");
    if(type == "pedal.distortion") return QStringLiteral("Distortion");
    if(type == "pedal.parametriceq") return QStringLiteral("Parametric EQ");
    if(type == "pedal.fuzz") return QStringLiteral("Fuzz");
    if(type == "pedal.phaser") return QStringLiteral("Phaser");
    return type;
}

std::shared_ptr<Processor> PedalRegistry::create(const QString& type)
{
    if (type == QStringLiteral("pedal.overdrive") || type == QStringLiteral("Overdrive")) return std::make_shared<Overdrive>();
    if (type == QStringLiteral("pedal.delay") || type == QStringLiteral("Delay")) return std::make_shared<DelayPedal>();
    if (type == QStringLiteral("pedal.reverb") || type == QStringLiteral("Reverb")) return std::make_shared<ReverbPedal>();
    if (type == QStringLiteral("pedal.noisegate") || type == QStringLiteral("Noise Gate")) return std::make_shared<NoiseGate>();
    if (type == QStringLiteral("pedal.compressor") || type == QStringLiteral("Compressor")) return std::make_shared<Compressor>();
    if (type == QStringLiteral("pedal.wah") || type == QStringLiteral("Wah")) return std::make_shared<Wah>();
    if (type == QStringLiteral("pedal.chorus") || type == QStringLiteral("Chorus")) return std::make_shared<Chorus>();
    if (type == QStringLiteral("pedal.tremolo") || type == QStringLiteral("Tremolo")) return std::make_shared<Tremolo>();
    if (type == QStringLiteral("pedal.tuner") || type == QStringLiteral("Tuner")) return std::make_shared<Tuner>();
    if(type == "pedal.amp" || type == "Amp") return std::make_shared<Amp>();
    if(type == "pedal.cabinet" || type == "Cabinet IR") return std::make_shared<Cabinet>();
    if(type == "pedal.octaver" || type == "Octaver") return std::make_shared<Octaver>();
    if(type == "pedal.pitchshift" || type == "Pitch Shifter") return std::make_shared<PitchShift>();
    if(type == "pedal.crunch" || type == "Crunch") return std::make_shared<Crunch>();
    if(type == "pedal.distortion" || type == "Distortion") return std::make_shared<Distortion>();
    if(type == "pedal.parametriceq" || type == "Parametric EQ") return std::make_shared<ParametricEq>();
    if(type == "pedal.fuzz" || type == "Fuzz") return std::make_shared<Fuzz>();
    if(type == "pedal.phaser" || type == "Phaser") return std::make_shared<Phaser>();
    return {};
}
