#include "Effects/EffectRegistry.h"
#include "Effects/adapters/PumpCompAdapter.h"
#include "Effects/adapters/PumpCrushAdapter.h"
#include "Effects/adapters/PumpEchoAdapter.h"
#include "Effects/adapters/PumpHeatAdapter.h"
#include "Effects/adapters/PumpSpringAdapter.h"
#include "Effects/adapters/PumpSwirlAdapter.h"
#include "Effects/adapters/PumpToneAdapter.h"
#include "Effects/adapters/PumpVerbAdapter.h"
#include "Effects/adapters/PumpLimitAdapter.h"
#include "Effects/adapters/PumpSnapAdapter.h"
#include "Effects/adapters/PumpStripAdapter.h"
#include "Effects/adapters/PumpCalmAdapter.h"
#include "Effects/adapters/PumpGateAdapter.h"
#include "Effects/adapters/PumpGrainAdapter.h"
#include "Effects/adapters/PumpStutterAdapter.h"
#include "Effects/adapters/PumpChopAdapter.h"
#include "Effects/adapters/PumpWideAdapter.h"
#include "Effects/adapters/PumpBandAdapter.h"
#include "Effects/adapters/PumpEqAdapter.h"
#include "Effects/adapters/PumpTwangAdapter.h"
#include "Effects/adapters/PumpTubeAdapter.h"

namespace pb
{
void registerBuiltInEffects (EffectRegistry& registry)
{
    EffectDescriptor spring;
    spring.typeId = PumpSpringAdapter().typeId();
    spring.displayName = PumpSpringAdapter().displayName();
    spring.category = "space";
    spring.create = [] { return std::make_unique<PumpSpringAdapter>(); };

    registry.add (std::move (spring));

    EffectDescriptor echo;
    echo.typeId = PumpEchoAdapter().typeId();
    echo.displayName = PumpEchoAdapter().displayName();
    echo.category = "time";
    echo.create = [] { return std::make_unique<PumpEchoAdapter>(); };

    registry.add (std::move (echo));

    EffectDescriptor tube;
    tube.typeId = PumpTubeAdapter().typeId();
    tube.displayName = PumpTubeAdapter().displayName();
    tube.category = "drive";
    tube.create = [] { return std::make_unique<PumpTubeAdapter>(); };

    registry.add (std::move (tube));

    EffectDescriptor crush;
    crush.typeId = PumpCrushAdapter().typeId();
    crush.displayName = PumpCrushAdapter().displayName();
    crush.category = "drive";
    crush.create = [] { return std::make_unique<PumpCrushAdapter>(); };

    registry.add (std::move (crush));

    EffectDescriptor swirl;
    swirl.typeId = PumpSwirlAdapter().typeId();
    swirl.displayName = PumpSwirlAdapter().displayName();
    swirl.category = "modulation";
    swirl.create = [] { return std::make_unique<PumpSwirlAdapter>(); };

    registry.add (std::move (swirl));

    EffectDescriptor comp;
    comp.typeId = PumpCompAdapter().typeId();
    comp.displayName = PumpCompAdapter().displayName();
    comp.category = "dynamics";
    comp.create = [] { return std::make_unique<PumpCompAdapter>(); };

    registry.add (std::move (comp));

    EffectDescriptor heat;
    heat.typeId = PumpHeatAdapter().typeId();
    heat.displayName = PumpHeatAdapter().displayName();
    heat.category = "drive";
    heat.create = [] { return std::make_unique<PumpHeatAdapter>(); };

    registry.add (std::move (heat));

    EffectDescriptor tone;
    tone.typeId = PumpToneAdapter().typeId();
    tone.displayName = PumpToneAdapter().displayName();
    tone.category = "filter";
    tone.create = [] { return std::make_unique<PumpToneAdapter>(); };

    registry.add (std::move (tone));

    EffectDescriptor verb;
    verb.typeId = PumpVerbAdapter().typeId();
    verb.displayName = PumpVerbAdapter().displayName();
    verb.category = "space";
    verb.create = [] { return std::make_unique<PumpVerbAdapter>(); };

    registry.add (std::move (verb));

    EffectDescriptor limit;
    limit.typeId = PumpLimitAdapter().typeId();
    limit.displayName = PumpLimitAdapter().displayName();
    limit.category = "dynamics";
    limit.create = [] { return std::make_unique<PumpLimitAdapter>(); };

    registry.add (std::move (limit));

    EffectDescriptor snap;
    snap.typeId = PumpSnapAdapter().typeId();
    snap.displayName = PumpSnapAdapter().displayName();
    snap.category = "dynamics";
    snap.create = [] { return std::make_unique<PumpSnapAdapter>(); };

    registry.add (std::move (snap));

    EffectDescriptor strip;
    strip.typeId = PumpStripAdapter().typeId();
    strip.displayName = PumpStripAdapter().displayName();
    strip.category = "drive";
    strip.create = [] { return std::make_unique<PumpStripAdapter>(); };

    registry.add (std::move (strip));

    EffectDescriptor calm;
    calm.typeId = PumpCalmAdapter().typeId();
    calm.displayName = PumpCalmAdapter().displayName();
    calm.category = "filter";
    calm.create = [] { return std::make_unique<PumpCalmAdapter>(); };

    registry.add (std::move (calm));

    EffectDescriptor gate;
    gate.typeId = PumpGateAdapter().typeId();
    gate.displayName = PumpGateAdapter().displayName();
    gate.category = "dynamics";
    gate.create = [] { return std::make_unique<PumpGateAdapter>(); };

    registry.add (std::move (gate));

    EffectDescriptor grain;
    grain.typeId = PumpGrainAdapter().typeId();
    grain.displayName = PumpGrainAdapter().displayName();
    grain.category = "space";
    grain.create = [] { return std::make_unique<PumpGrainAdapter>(); };

    registry.add (std::move (grain));

    EffectDescriptor stutter;
    stutter.typeId = PumpStutterAdapter().typeId();
    stutter.displayName = PumpStutterAdapter().displayName();
    stutter.category = "time";
    stutter.create = [] { return std::make_unique<PumpStutterAdapter>(); };

    registry.add (std::move (stutter));

    EffectDescriptor chop;
    chop.typeId = PumpChopAdapter().typeId();
    chop.displayName = PumpChopAdapter().displayName();
    chop.category = "time";
    chop.create = [] { return std::make_unique<PumpChopAdapter>(); };

    registry.add (std::move (chop));

    EffectDescriptor wide;
    wide.typeId = PumpWideAdapter().typeId();
    wide.displayName = PumpWideAdapter().displayName();
    wide.category = "space";
    wide.create = [] { return std::make_unique<PumpWideAdapter>(); };

    registry.add (std::move (wide));

    EffectDescriptor band;
    band.typeId = PumpBandAdapter().typeId();
    band.displayName = PumpBandAdapter().displayName();
    band.category = "dynamics";
    band.create = [] { return std::make_unique<PumpBandAdapter>(); };

    registry.add (std::move (band));

    EffectDescriptor eq;
    eq.typeId = PumpEqAdapter().typeId();
    eq.displayName = PumpEqAdapter().displayName();
    eq.category = "filter";
    eq.create = [] { return std::make_unique<PumpEqAdapter>(); };

    registry.add (std::move (eq));

    EffectDescriptor twang;
    twang.typeId = PumpTwangAdapter().typeId();
    twang.displayName = PumpTwangAdapter().displayName();
    twang.category = "space";
    twang.create = [] { return std::make_unique<PumpTwangAdapter>(); };

    registry.add (std::move (twang));
}
} // namespace pb
