#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Effects/EffectRegistry.h"
#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"
#include "Engine/SafetyLimiter.h"
#include <cmath>
#include <vector>

namespace
{
constexpr double kRate = 48000.0;
const float kCeiling = juce::Decibels::decibelsToGain (-0.3f);

// A pedal with one job: +12 dB. Used for the card's "sixteen of them cannot push the board over" check.
class LoudEffect final : public pb::IEffect
{
public:
    juce::String typeId() const override { return "test.loud"; }
    juce::String displayName() const override { return "Test Loud"; }
    std::vector<pb::ParamInfo> params() const override { return {}; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.applyGain (3.98107f); }
};

float worstOver (const std::vector<float>& a, const std::vector<float>& b)
{
    float worst = 0.0f;

    for (size_t i = 0; i < a.size(); ++i)
    {
        worst = juce::jmax (worst, std::abs (a[i]));
        worst = juce::jmax (worst, std::abs (b[i]));
    }

    return worst;
}
} // namespace

class SafetyLimiterTests final : public juce::UnitTest
{
public:
    SafetyLimiterTests() : juce::UnitTest ("SafetyLimiter", "engine") {}

    void runTest() override
    {
        beginTest ("a signal under the ceiling passes untouched");
        {
            pb::SafetyLimiter limiter;
            limiter.prepare (kRate);

            std::vector<float> left (512), right (512), leftIn (512), rightIn (512);

            for (size_t i = 0; i < left.size(); ++i)
            {
                left[i] = leftIn[i] = 0.5f * std::sin (2.0f * juce::MathConstants<float>::pi * 440.0f * (float) i / (float) kRate);
                right[i] = rightIn[i] = 0.4f * leftIn[i];
            }

            limiter.process (left.data(), right.data(), (int) left.size());

            float worst = 0.0f;

            for (size_t i = 0; i < left.size(); ++i)
            {
                worst = juce::jmax (worst, std::abs (left[i] - leftIn[i]));
                worst = juce::jmax (worst, std::abs (right[i] - rightIn[i]));
            }

            expectWithinAbsoluteError (worst, 0.0f, 0.0f);            // bit-identical, not merely quiet
            expectWithinAbsoluteError (limiter.currentGain(), 1.0f, 1.0e-6f);
        }

        beginTest ("a signal over the ceiling comes out at the ceiling");
        {
            pb::SafetyLimiter limiter;
            limiter.prepare (kRate);

            std::vector<float> left (1024), right (1024);

            for (size_t i = 0; i < left.size(); ++i)
            {
                left[i] = 2.0f * std::sin (2.0f * juce::MathConstants<float>::pi * 100.0f * (float) i / (float) kRate);
                right[i] = -1.5f * left[i];
            }

            limiter.process (left.data(), right.data(), (int) left.size());

            expect (worstOver (left, right) <= kCeiling + 1.0e-6f);
        }

        beginTest ("the limiter lets go once the loud part is over");
        {
            pb::SafetyLimiter limiter;
            limiter.prepare (kRate);

            std::vector<float> left (1000, 4.0f), right (1000, 4.0f);
            limiter.process (left.data(), right.data(), 1000);
            expect (limiter.currentGain() < 0.5f);                    // ducked hard by the burst
            expect (limiter.currentGain() > 0.0f);

            // A second of quiet: the 50 ms release should have brought the gain back to unity.
            std::vector<float> quiet (48000, 0.1f), quietRight (48000, 0.1f);
            limiter.process (quiet.data(), quietRight.data(), 48000);

            expectWithinAbsoluteError (limiter.currentGain(), 1.0f, 1.0e-3f);
            expectWithinAbsoluteError (quiet.back(), 0.1f, 1.0e-3f);
        }

        beginTest ("a loud channel ducks the quiet one too (stereo link)");
        {
            pb::SafetyLimiter limiter;
            limiter.prepare (kRate);

            float left[1] = { 2.0f }, right[1] = { 0.1f };
            limiter.process (left, right, 1);

            expectWithinAbsoluteError (std::abs (left[0]), kCeiling, 1.0e-5f);
            expectWithinAbsoluteError (right[0], 0.1f * kCeiling / 2.0f, 1.0e-6f);
        }

        beginTest ("a NaN is stopped rather than spread through the board");
        {
            pb::SafetyLimiter limiter;
            limiter.prepare (kRate);

            float left[2] = { std::nanf (""), 0.5f }, right[2] = { 0.0f, 0.5f };
            limiter.process (left, right, 2);

            expectEquals (left[0], 0.0f);
            expectEquals (right[0], 0.0f);
            expect (std::isfinite (left[1]));
            expect (std::isfinite (right[1]));
        }

        beginTest ("mono works when the second channel is absent");
        {
            pb::SafetyLimiter limiter;
            limiter.prepare (kRate);

            std::vector<float> left (64, 2.0f);
            limiter.process (left.data(), nullptr, 64);

            for (const auto sample : left)
                expect (std::abs (sample) <= kCeiling + 1.0e-6f);
        }

        beginTest ("sixteen gain pedals at +12 dB cannot push the board over the ceiling");
        {
            pb::ParameterRegistry registry;
            pb::MidiRouter router (registry);
            pb::BoardProcessor processor (registry, router);
            processor.prepareToPlay (kRate, 64);

            pb::EffectRegistry effects;
            effects.add ({ "test.loud", "Test Loud", "utility", [] { return std::make_unique<LoudEffect>(); } });

            pb::BoardModel model (processor, effects, registry);

            for (int i = 0; i < pb::ChainSnapshot::kMaxSlots; ++i)
                expect (model.insert (i, "test.loud"));

            juce::AudioBuffer<float> buffer (2, 64);
            juce::MidiBuffer midi;

            for (int ch = 0; ch < 2; ++ch)
                for (int i = 0; i < 64; ++i)
                    buffer.setSample (ch, i, 0.25f);

            processor.processBlock (buffer, midi);

            float peak = 0.0f;

            for (int ch = 0; ch < 2; ++ch)
                peak = juce::jmax (peak, buffer.getMagnitude (ch, 0, 64));

            logMessage ("sixteen +12 dB pedals from 0.25 in: peak out "
                        + juce::String (juce::Decibels::gainToDecibels (peak), 2) + " dBFS");
            expect (peak <= kCeiling + 1.0e-6f);
        }
    }
};

static SafetyLimiterTests safetyLimiterTests;
