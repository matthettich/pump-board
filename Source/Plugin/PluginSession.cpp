#include "Plugin/PluginSession.h"
#include "Effects/EffectRegistry.h"

namespace pb
{
namespace
{
// Two plugin instances share one process, and one registry: registering the pedals twice would assert on the second
// instance's load. Checking first is also what lets the test binary register them itself if it wants to.
void ensureBuiltInEffects()
{
    auto& registry = EffectRegistry::instance();

    if (! registry.contains ("pump.tube"))
        registerBuiltInEffects (registry);
}
} // namespace

PluginSession::PluginSession()
    : midiRouter (registry),
      keys (midiRouter),
      learn (registry, midiRouter, keys),
      macros (registry),
      processor (registry, midiRouter),
      model (processor, EffectRegistry::instance(), registry)
{
    ensureBuiltInEffects();
}

PluginSession::~PluginSession() = default;

void PluginSession::prepare (double sampleRate, int maximumBlockSize)
{
    // BoardProcessor reads AudioProcessor::getSampleRate() (the MIDI clock does), and only the host's player would
    // normally set it. Without this it would be zero inside a plugin.
    processor.setRateAndBufferSizeDetails (sampleRate, maximumBlockSize);
    processor.prepareToPlay (sampleRate, maximumBlockSize);
    model.prepareAllForHost (processor.getSampleRateHz(), processor.getMaxBlock());
}

void PluginSession::collectGarbage()
{
    processor.collectGarbage();
    midiRouter.collectGarbage();
}

void PluginSession::loadDefaultBoard()
{
    // The app's opening chain: drive, then delay, then space, inserted at the end so the order is the chain's order.
    const char* const chain[] = { "pump.tube", "pump.echo", "pump.spring" };

    for (const auto* typeId : chain)
        model.insert (model.size(), typeId);
}
} // namespace pb
