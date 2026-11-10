#include "testkit.h"
#include "plugins/effectchain.h"
#include "plugins/mixfx/mixfxfactory.h"
#include "plugins/mixfx/mixfxspec.h"

#include <cmath>
#include <limits>
#include <set>

TEST(mixfxspec, every_plugin_has_sane_controls_and_an_fx_list_entry)
{
    CHECK(!mixfx::all().empty());
    for (const auto& spec : mixfx::all()) {
        const auto* entry = effectchain::find(spec.type);
        CHECK(entry && entry->addable && entry->removable);
        CHECK(entry && entry->title == spec.title);
        std::set<std::string_view> ids;
        for (const auto& c : spec.controls) {
            CHECK(ids.insert(c.id).second);   // ids are unique
            CHECK(c.id != "bypass");          // the base's own parameter
            CHECK(c.min < c.max && c.def >= c.min && c.def <= c.max);
            CHECK(!c.card.empty() && !c.name.empty());
            if (c.kind == mixfx::ControlKind::Choice)
                CHECK(c.min == 0 && c.max == float(c.choices.size() - 1) && c.choices.size() >= 2);
            if (c.kind == mixfx::ControlKind::Switch) CHECK(c.min == 0 && c.max == 1);
        }
        // The processor's parameters follow the spec, plus bypass.
        auto processor = mixfx::create(spec.type);
        CHECK(processor != nullptr);
        CHECK(processor->parameters().size() == spec.controls.size() + 1);
        CHECK(processor->typeId() == "mix." + std::string(spec.type));
    }
    CHECK(mixfx::create("eq") == nullptr);
    CHECK(!mixfx::isMixEffect("chaoseq"));
}

TEST(mixfxspec, settings_are_sanitized_against_the_spec)
{
    const auto defaults = mixfx::defaults(mixfx::Compressor);
    const auto* spec = mixfx::find(mixfx::Compressor);
    CHECK(spec && defaults.values.size() == spec->controls.size() && !defaults.bypassed);
    CHECK(mixfx::defaults("nothing").values.empty());

    mixfx::Settings odd;
    odd.bypassed = true;
    odd.values["threshold"] = -500;                                      // clamped
    odd.values["ratio"] = std::numeric_limits<float>::quiet_NaN();       // default
    odd.values["style"] = 1.6f;                                          // a whole choice
    odd.values["autoMakeup"] = 0.7f;                                     // a switch
    odd.values["madeUp"] = 3;                                            // dropped
    const auto clean = mixfx::sanitize(mixfx::Compressor, odd);
    CHECK(clean.bypassed);
    CHECK(clean.values.at("threshold") == -60.0f);
    CHECK(clean.values.at("ratio") == 4.0f);
    CHECK(clean.values.at("style") == 2.0f);
    CHECK(clean.values.at("autoMakeup") == 1.0f);
    CHECK(clean.values.count("madeUp") == 0);
    CHECK(clean.values.at("mix") == 100.0f);                             // missing: default
    CHECK(mixfx::value(*spec, mixfx::Settings{}, "knee") == 6.0f);
}
