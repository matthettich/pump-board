#pragma once
// TEMPLATE — copy this file to make each pump adapter (e.g. PumpSpringAdapter.h/.cpp).
// Rules:
//  - Include pump's DSP header from external/pump. Never copy pump code here.
//  - Keep the same param ids, ranges and defaults pump's plugin uses, so presets
//    and null tests line up.
//  - setParam() only stores the value; smoothing happens in process().
#include "Effects/IEffect.h"
// #include "<path inside external/pump to the effect's DSP class>"

namespace pb
{
class PumpExampleAdapter final : public IEffect
{
public:
    juce::String typeId() const override { return "pump.example"; }
    juce::String displayName() const override { return "pump EXAMPLE"; }

    std::vector<ParamInfo> params() const override
    {
        return {
            { "drive", "Drive", 0.0f, 1.0f, 0.3f, "" },
            { "mix",   "Mix",   0.0f, 1.0f, 1.0f, "%" },
        };
    }

    void prepare (double sampleRate, int maxBlockSize, int numChannels) override
    {
        juce::ignoreUnused (maxBlockSize, numChannels);
        for (auto& s : smoothed)
            s.reset (sampleRate, 0.02); // 20 ms
        // dsp.prepare (sampleRate, maxBlockSize, numChannels);
    }

    void reset() override
    {
        // dsp.reset();
    }

    void setParam (int index, float value) noexcept override
    {
        if (index >= 0 && index < (int) smoothed.size())
            smoothed[(size_t) index].setTargetValue (value);
    }

    void process (juce::AudioBuffer<float>& buffer) noexcept override
    {
        // Per-sample or per-block: advance smoothers and hand values to pump's DSP.
        // dsp.setDrive (smoothed[0].skip (buffer.getNumSamples()));
        // dsp.process (buffer);
        juce::ignoreUnused (buffer);
    }

private:
    std::array<juce::SmoothedValue<float>, 2> smoothed;
    // pump::ExampleDsp dsp;
};
} // namespace pb
