#include "Plugin/PluginProcessor.h"
#include "Effects/EffectRegistry.h"
#include <cmath>
#include <limits>

namespace
{
bool allFinite (const juce::AudioBuffer<float>& buffer)
{
    for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            if (! std::isfinite (buffer.getSample (ch, i)))
                return false;

    return true;
}

class PluginRobustnessTests final : public juce::UnitTest
{
public:
    PluginRobustnessTests() : juce::UnitTest ("PluginRobustness", "plugin") {}

    void runTest() override
    {
        ensureBuiltInEffects();

        beginTest ("every host rate and block size processes to finite output");
        {
            // Card 8.8: hosts are less polite than one CoreAudio device. The last combination wins, so each rate and
            // block also re-prepares a board that was prepared for the one before it.
            const double rates[] = { 44100.0, 48000.0, 88200.0, 96000.0 };
            const int blocks[] = { 0, 1, 7, 64, 128, 511, 1024 };

            pb::PluginProcessor processor;
            juce::MidiBuffer midi;

            for (auto rate : rates)
            {
                for (auto block : blocks)
                {
                    processor.prepareToPlay (rate, block);

                    juce::AudioBuffer<float> buffer (2, juce::jmax (1, block));
                    buffer.clear();
                    buffer.setSample (0, 0, 0.25f);

                    processor.processBlock (buffer, midi);

                    expect (allFinite (buffer));
                }
            }
        }

        beginTest ("a block larger than the prepared one passes audio instead of overrunning");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 64);
            processor.getSession().getModel().clear();      // an empty board, so the dry path is the only thing running

            juce::AudioBuffer<float> buffer (2, 4096);
            buffer.clear();
            buffer.setSample (0, 0, 0.1f);

            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);

            expect (allFinite (buffer));
            expectGreaterThan (buffer.getMagnitude (0, 0, 4096), 0.0f);
        }

        beginTest ("a zero-length block, or a buffer with no channels, is a no-op");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 128);

            juce::MidiBuffer midi;

            juce::AudioBuffer<float> empty (2, 0);
            processor.processBlock (empty, midi);
            expectEquals (empty.getNumSamples(), 0);

            juce::AudioBuffer<float> noChannels (0, 128);
            processor.processBlock (noChannels, midi);
            expectEquals (noChannels.getNumChannels(), 0);

            // The board is still alive afterwards: a normal block after the degenerate ones processes as before.
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            buffer.setSample (0, 0, 0.1f);
            processor.processBlock (buffer, midi);
            expect (allFinite (buffer));
        }

        beginTest ("mono in and stereo out is accepted; the reverse is not");
        {
            pb::PluginProcessor processor;

            const auto mono = juce::AudioChannelSet::mono();
            const auto stereo = juce::AudioChannelSet::stereo();

            expect (processor.isBusesLayoutSupported ({ mono, stereo }));
            expect (! processor.isBusesLayoutSupported ({ stereo, mono }));

            processor.prepareToPlay (48000.0, 128);
            processor.getSession().getModel().clear();

            juce::AudioBuffer<float> buffer (1, 128);
            buffer.clear();
            buffer.setSample (0, 0, 0.2f);

            juce::MidiBuffer midi;
            processor.processBlock (buffer, midi);

            expect (allFinite (buffer));
        }

        beginTest ("full-scale and non-finite input leave finite output");
        {
            pb::PluginProcessor processor;
            processor.prepareToPlay (48000.0, 128);

            juce::AudioBuffer<float> loud (2, 128);
            loud.clear();

            for (int ch = 0; ch < loud.getNumChannels(); ++ch)
                for (int i = 0; i < loud.getNumSamples(); ++i)
                    loud.setSample (ch, i, 1.0f);

            juce::MidiBuffer midi;
            processor.processBlock (loud, midi);

            expect (allFinite (loud));

            // A garbage first buffer from an interface is a real thing on macOS; the board scrubs it before any
            // pedal can carry it into a feedback path.
            juce::AudioBuffer<float> broken (2, 128);
            broken.clear();
            broken.setSample (0, 0, std::numeric_limits<float>::quiet_NaN());
            broken.setSample (1, 5, std::numeric_limits<float>::infinity());

            processor.processBlock (broken, midi);

            expect (allFinite (broken));
        }
    }

private:
    static void ensureBuiltInEffects()
    {
        auto& registry = pb::EffectRegistry::instance();

        if (! registry.contains ("pump.tube"))
            pb::registerBuiltInEffects (registry);
    }
};

PluginRobustnessTests pluginRobustnessTests;
} // namespace
