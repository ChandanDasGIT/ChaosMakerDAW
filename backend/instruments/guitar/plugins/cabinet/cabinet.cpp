#include "cabinet.h"
#include <algorithm>
#include <cmath>
#include <stdexcept>

namespace {
using eqband::Shape; using eqband::Slope;
// Built-in cabinet responses, one row per model (index 0 = None has no bands).
struct Band { Shape shape; double hz, gainDb, q; Slope slope; };
constexpr Band none{Shape::Bell, 1000, 0, 1, Slope::Db12};   // unused slot: flat
constexpr std::array<std::array<Band, Cabinet::MaxModelBands>, Cabinet::ModelCount> models{{
    {none, none, none, none, none, none, none},
    // 1x12 Open back: an open back loses deep bass, so a gentler resonance; warmer mids.
    {{{Shape::LowCut, 100, 0, 0.7, Slope::Db12}, {Shape::LowShelf, 380, 6, 0.6, Slope::Db6},
      {Shape::Bell, 700, 2, 0.8, Slope::Db12}, {Shape::Bell, 2600, 6, 1.0, Slope::Db12},
      {Shape::HighShelf, 6500, -22, 1.0, Slope::Db24}, {Shape::HighCut, 12000, 0, eqband::DefaultQ, Slope::Db12}, none}},
    // 2x12: between the two.
    {{{Shape::LowCut, 80, 0, 0.8, Slope::Db24}, {Shape::LowShelf, 400, 10, 0.6, Slope::Db6},
      {Shape::Bell, 1500, 2, 1.2, Slope::Db12}, {Shape::Bell, 2900, 7, 1.0, Slope::Db12},
      {Shape::HighShelf, 7200, -24, 1.2, Slope::Db24}, {Shape::HighCut, 12500, 0, eqband::DefaultQ, Slope::Db12}, none}},
    // 4x12 Closed back: fitted to the measured boutique 4x12 IR (no window, third-octaves):
    // steep cut below ~80 Hz, broad resonance around 100-160 Hz, presence near 3.2 kHz.
    {{{Shape::LowCut, 86, 0, 0.9, Slope::Db24}, {Shape::LowShelf, 420, 14, 0.6, Slope::Db6},
      {Shape::Bell, 1500, 1.5, 1.4, Slope::Db12}, {Shape::Bell, 3200, 7.5, 0.9, Slope::Db12},
      {Shape::HighShelf, 7600, -24, 1.2, Slope::Db24}, {Shape::HighCut, 13000, 0, eqband::DefaultQ, Slope::Db12}, none}},
}};
// Level trims so switching models does not jump in loudness (measured on a high-gain amp).
constexpr std::array<float, Cabinet::ModelCount> modelTrimDb{0.0f, 3.6f, 2.1f, 0.0f};
}

Cabinet::Cabinet(CabinetImpulse value) : impulse(std::move(value)),controls{{
    &addParameter("mix","Mix",0,1,1), &addParameter("level","Level",-24,12,0,"dB"),
    &addParameter("model","Cab",0,ModelCount - 1,3,"cabmodel")
}} {}

void Cabinet::buildModel(int model) noexcept {
    builtModel = model;
    modelTrim = std::pow(10.0f, modelTrimDb[std::size_t(model)] / 20);
    for (std::size_t b = 0; b < modelBands.size(); ++b) {
        const auto& band = models[std::size_t(model)][b];
        modelBands[b].set({band.shape, band.hz, band.gainDb, band.q, band.slope}, rate);
        modelBands[b].reset();
    }
}

void Cabinet::prepare(double sampleRate,int) {
    rate = std::max(sampleRate, 1000.0);
    if(loaded()) {
        if(std::abs(impulse.sampleRate-sampleRate)>0.5)throw std::runtime_error("Cabinet IR needs resampling for this audio rate.");
        for(int c=0;c<2;++c)convolvers[c].prepare(impulse.channels[c]);
    }
    for(auto& smoother:smoothers)smoother.prepare(sampleRate,20);
    builtModel = -1;
    reset();
}

void Cabinet::reset() noexcept {
    for(auto& convolver:convolvers)convolver.reset();
    for(int i=0;i<2;++i)smoothers[i].reset(controls[i]->get());
    for(auto& band:modelBands)band.reset();
}

void Cabinet::process(ProcessContext& ctx) noexcept {
    const int model = std::clamp(int(std::lround(controls[2]->get())), 0, ModelCount - 1);
    if(!loaded() && model == 0) return; // No IR and no model: pass through, as before.
    if(!loaded() && model != builtModel) buildModel(model);
    for(int i=0;i<ctx.audio.frames;++i) {
        const float mix=smoothers[0].next(controls[0]->get());
        const float gain=std::pow(10.0f,smoothers[1].next(controls[1]->get())/20);
        for(int c=0;c<std::min(ctx.audio.channelCount,2);++c) {
            float& x=ctx.audio.channel(c)[i];
            float wet;
            if(loaded()) wet=convolvers[c].process(x);
            else { wet=x; for(auto& band:modelBands) wet=band.run(c,wet); wet*=modelTrim; }
            x=(x*(1-mix)+wet*mix)*gain;
        }
    }
}
