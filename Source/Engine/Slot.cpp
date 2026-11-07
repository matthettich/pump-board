#include "Engine/Slot.h"
#include "Engine/LoadMeter.h"
#include <cmath>

namespace pb
{
namespace
{
constexpr double kCrossfadeSeconds = 0.010;      // the kit's 10 ms, equal power

ParameterRegistry::Entry entryFor (const juce::String& id, const ParamInfo& info)
{
    ParameterRegistry::Entry e;
    e.id = id;
    e.displayName = info.name;
    e.minValue = info.minValue;
    e.maxValue = info.maxValue;
    e.defaultValue = info.defaultValue;
    e.isToggle = info.isToggle;
    return e;
}
} // namespace

Slot::Slot (juce::String uidIn, ParameterRegistry& registryIn)
    : uid (std::move (uidIn)), registry (registryIn)
{
    // Registered here rather than in setEffect: the switch outlives any effect, so its id stays stable.
    ParameterRegistry::Entry on;
    on.id = "slot/" + uid + "/on";
    on.displayName = "On";
    on.minValue = 0.0f;
    on.maxValue = 1.0f;
    on.defaultValue = 1.0f;
    on.isToggle = true;
    onIndex = registry.add (on);

    // Card 5.24: the slot's own process time, published to a side table (the header is fixed).
    load::registerSlot (this);
}

Slot::~Slot()
{
    load::unregisterSlot (this);
    for (const int index : indices)
        if (index != ParameterRegistry::kInvalid)
            registry.remove (index);

    if (onIndex != ParameterRegistry::kInvalid)
        registry.remove (onIndex);
}

void Slot::setEffect (EffectPtr newEffect)
{
    for (const int index : indices)
        if (index != ParameterRegistry::kInvalid)
            registry.remove (index);

    indices.clear();
    effect = std::move (newEffect);

    if (effect == nullptr)
        return;

    const auto params = effect->params();
    indices.reserve (params.size());

    for (const auto& info : params)
        indices.push_back (registry.add (entryFor ("slot/" + uid + "/" + info.id, info)));
}

void Slot::prepare (double sampleRate, int maxBlockSize)
{
    dryBuffer.setSize (2, juce::jmax (1, maxBlockSize) + kMaxDelaySamples, false, true, false);
    dryBuffer.clear();
    dryWritePos = 0;

    wetGain.reset (sampleRate, kCrossfadeSeconds);

    const bool on = onIndex != ParameterRegistry::kInvalid && registry.get (onIndex) >= 0.5f;
    wetGain.setCurrentAndTargetValue (on ? 1.0f : 0.0f);

    load::setSampleRate (this, sampleRate);

    if (effect != nullptr)
        effect->prepare (sampleRate, juce::jmax (1, maxBlockSize), 2);
}

void Slot::process (juce::AudioBuffer<float>& buffer) noexcept
{
    if (effect == nullptr)
        return;                                   // an empty slot passes audio untouched

    const int numSamples = buffer.getNumSamples();
    const int numChannels = juce::jmin (2, buffer.getNumChannels());

    const int ringSize = dryBuffer.getNumSamples();

    if (numSamples <= 0 || numChannels <= 0 || numSamples > ringSize)
        return;

    // 1. push the targets. The effect smooths its own parameters.
    for (size_t i = 0; i < indices.size(); ++i)
        if (indices[i] != ParameterRegistry::kInvalid)
            effect->setParam ((int) i, registry.get (indices[i]));

    const bool wantOn = onIndex != ParameterRegistry::kInvalid && registry.get (onIndex) >= 0.5f;
    wetGain.setTargetValue (wantOn ? 1.0f : 0.0f);

    // 2. fully bypassed and settled: skip the effect entirely (card 2.1's v1 rule, tails stop with it)
    if (! wantOn && ! wetGain.isSmoothing() && wetGain.getCurrentValue() <= 0.0f)
        return;

    // 3. the dry signal, taken before the effect touches the buffer, into the delay ring. Card 5.4: it is read
    //    back `latency` frames later, so a pedal that reports latency (TUBE, CRUSH, SWIRL, COMP) does not comb
    //    the crossfade when its slot is switched off.
    const int latency = juce::jlimit (0, kMaxDelaySamples, effect->latencySamples());

    for (int ch = 0; ch < numChannels; ++ch)
    {
        auto* dst = dryBuffer.getWritePointer (ch);
        const int first = juce::jmin (numSamples, ringSize - dryWritePos);

        juce::FloatVectorOperations::copy (dst + dryWritePos, buffer.getReadPointer (ch), first);

        if (first < numSamples)
            juce::FloatVectorOperations::copy (dst, buffer.getReadPointer (ch) + first, numSamples - first);
    }

    dryWritePos = (dryWritePos + numSamples) % ringSize;

    // 4. the effect, in place — timed for the CPU bar (card 5.24). Two clock reads, no allocation.
    const auto startedTicks = juce::Time::getHighResolutionTicks();
    effect->process (buffer);
    load::publish (this, juce::Time::highResolutionTicksToSeconds (juce::Time::getHighResolutionTicks() - startedTicks),
                   numSamples);

    // 5. equal-power crossfade, 10 ms, against the delayed dry
    // (dryWritePos has already advanced past this block, so the block's own first sample sits at -numSamples)
    int readPos = ((dryWritePos - numSamples - latency) % ringSize + ringSize) % ringSize;

    for (int i = 0; i < numSamples; ++i)
    {
        const float g = wetGain.getNextValue();
        const float wet = std::sin (g * juce::MathConstants<float>::halfPi);
        const float dry = std::cos (g * juce::MathConstants<float>::halfPi);

        for (int ch = 0; ch < numChannels; ++ch)
        {
            auto* wetData = buffer.getWritePointer (ch);
            wetData[i] = wetData[i] * wet + dryBuffer.getReadPointer (ch)[readPos] * dry;
        }

        if (++readPos >= ringSize)
            readPos = 0;
    }
}
} // namespace pb
