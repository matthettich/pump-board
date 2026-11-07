#include "Plugin/PluginSession.h"
#include "Effects/EffectRegistry.h"
#include <cmath>
#include <memory>

namespace
{
class PluginSessionTests final : public juce::UnitTest
{
public:
    PluginSessionTests() : juce::UnitTest ("PluginSession", "plugin") {}

    void runTest() override
    {
        ensureBuiltInEffects();

        beginTest ("the default board is the app's opening chain");
        {
            pb::PluginSession session;
            session.loadDefaultBoard();

            expectEquals (session.getModel().size(), 3);
        }

        beginTest ("a prepared session processes a block, with no allocation in the callback");
        {
            // In Debug the guard is inside BoardProcessor::processBlock: reaching the line after it without an
            // assertion is the proof that the plugin's path allocates nothing either.
            pb::PluginSession session;
            session.loadDefaultBoard();
            session.prepare (48000.0, 128);

            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            buffer.setSample (0, 0, 0.25f);

            juce::MidiBuffer midi;
            session.getProcessor().processBlock (buffer, midi);

            for (int ch = 0; ch < buffer.getNumChannels(); ++ch)
                for (int i = 0; i < buffer.getNumSamples(); ++i)
                    expect (std::isfinite (buffer.getSample (ch, i)));

            expectGreaterThan (buffer.getMagnitude (0, 0, 128), 0.0f);
        }

        beginTest ("mono input is folded up, as the app does");
        {
            pb::PluginSession session;
            session.getModel().clear();                 // an empty board, so the fold is the only thing that runs
            session.prepare (48000.0, 256);

            juce::AudioBuffer<float> buffer (2, 256);
            buffer.clear();

            for (int i = 0; i < buffer.getNumSamples(); ++i)
                buffer.setSample (0, i, 0.1f);

            juce::MidiBuffer midi;
            session.getProcessor().processBlock (buffer, midi);

            expectGreaterThan (buffer.getMagnitude (1, 0, 256), 0.0f);
            expectWithinAbsoluteError (buffer.getMagnitude (1, 0, 256),
                                       buffer.getMagnitude (0, 0, 256), 1.0e-6f);
        }

        beginTest ("a host prepare at a new rate and block re-prepares the slots");
        {
            pb::PluginSession session;
            session.loadDefaultBoard();

            session.prepare (44100.0, 64);
            session.prepare (96000.0, 1024);

            juce::AudioBuffer<float> buffer (2, 1024);
            buffer.clear();
            buffer.setSample (0, 0, 0.1f);

            juce::MidiBuffer midi;
            session.getProcessor().processBlock (buffer, midi);

            expect (std::isfinite (buffer.getSample (0, 0)));
            expectEquals (session.getProcessor().getMaxBlock(), 1024);
        }

        beginTest ("a session that is destroyed with a fresh insert pending does not crash");
        {
            // publish() retires the previous snapshot at N+2 blocks; with no blocks running, destruction has to
            // free both chains itself, and this is the case a host hits when a project is closed while loading.
            auto session = std::make_unique<pb::PluginSession>();
            session->prepare (48000.0, 128);
            session->loadDefaultBoard();
            session->getModel().insert (0, "pump.verb");
            session.reset();
            expect (true);
        }
    }

private:
    static void ensureBuiltInEffects()
    {
        // The test binary registers the pedals itself; the plugin's session only does so if nobody has.
        auto& registry = pb::EffectRegistry::instance();

        if (! registry.contains ("pump.tube"))
            pb::registerBuiltInEffects (registry);
    }
};

PluginSessionTests pluginSessionTests;
} // namespace
