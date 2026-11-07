#include "Plugin/PluginProcessor.h"
#include "Effects/EffectRegistry.h"
#include "Presets/BoardSerializer.h"
#include "Presets/BoardState.h"
#include <cstring>

namespace
{
// The board as it would be saved, so two sessions can be compared without knowing their uids.
juce::String boardJsonOf (pb::PluginSession& session)
{
    return pb::toJson (pb::capture (session.getModel(), session.getMacros(), session.getLearn(),
                                    session.getRegistry()));
}

float tempoOf (pb::PluginSession& session)
{
    const int index = session.getRegistry().find ("board/tempo");
    return index != pb::ParameterRegistry::kInvalid ? session.getRegistry().get (index) : -1.0f;
}

bool setTempo (pb::PluginSession& session, float bpm)
{
    const int index = session.getRegistry().find ("board/tempo");

    if (index == pb::ParameterRegistry::kInvalid)
        return false;

    session.getRegistry().set (index, bpm);
    return true;
}

juce::String envelope (const juce::String& format, const juce::var& version, const juce::String& board)
{
    juce::DynamicObject::Ptr object = new juce::DynamicObject();
    object->setProperty ("format", format);
    object->setProperty ("version", version);
    object->setProperty ("board", board);
    return juce::JSON::toString (juce::var (object.get()), false);
}

class PluginStateTests final : public juce::UnitTest
{
public:
    PluginStateTests() : juce::UnitTest ("PluginState", "plugin") {}

    void runTest() override
    {
        ensureBuiltInEffects();

        beginTest ("a board saved through the host restores into a fresh instance");
        {
            pb::PluginProcessor source;
            auto& session = source.getSession();

            // Something that is not the default in each of the places state lives: the tempo, a pedal value, and a
            // macro name.
            expect (setTempo (session, 137.0f));
            session.getMacros().setName (0, "VOLUME");

            if (auto* slot = session.getModel().at (0))
                if (const auto& indices = slot->paramIndices(); ! indices.empty())
                    if (indices[0] != pb::ParameterRegistry::kInvalid)
                        session.getRegistry().set (indices[0], 0.37f);

            juce::MemoryBlock saved;
            source.getStateInformation (saved);
            expectGreaterThan (saved.getSize(), (size_t) 0);

            pb::PluginProcessor restored;
            restored.setStateInformation (saved.getData(), (int) saved.getSize());

            expectEquals (boardJsonOf (restored.getSession()), boardJsonOf (session));
            expectWithinAbsoluteError (tempoOf (restored.getSession()), 137.0f, 1.0e-4f);
        }

        beginTest ("restoring the same state twice is the same as restoring it once");
        {
            pb::PluginProcessor source;
            source.getSession().getMacros().setName (1, "SPACE");

            juce::MemoryBlock saved;
            source.getStateInformation (saved);

            pb::PluginProcessor once, twice;
            once.setStateInformation (saved.getData(), (int) saved.getSize());
            twice.setStateInformation (saved.getData(), (int) saved.getSize());
            twice.setStateInformation (saved.getData(), (int) saved.getSize());

            expectEquals (boardJsonOf (twice.getSession()), boardJsonOf (once.getSession()));
        }

        beginTest ("state this build does not understand leaves the board alone");
        {
            pb::PluginProcessor processor;
            auto& session = processor.getSession();
            const auto before = boardJsonOf (session);

            auto offer = [&processor] (const juce::String& text)
            {
                processor.setStateInformation (text.toRawUTF8(), (int) text.getNumBytesAsUTF8());
            };

            processor.setStateInformation (nullptr, 0);                                     // nothing at all

            const char* const garbage = "\x01\x02 not json at all";
            processor.setStateInformation (garbage, (int) std::strlen (garbage));           // not JSON

            juce::MemoryBlock saved;
            processor.getStateInformation (saved);                                          // a good state,
            processor.setStateInformation (saved.getData(), (int) saved.getSize() / 2);     // cut in half

            offer (envelope ("pumpboard-plugin-state", 999, "{}"));                          // the future
            offer (envelope ("some-other-plugin", 1, "{}"));                                // another product
            offer (envelope ("pumpboard-plugin-state", 1, "not a board"));                  // our version, bad board

            expectEquals (boardJsonOf (session), before);
        }

        beginTest ("a restore during playback swaps behind the bypass and comes back");
        {
            pb::PluginProcessor source;
            expect (setTempo (source.getSession(), 90.0f));

            juce::MemoryBlock saved;
            source.getStateInformation (saved);

            pb::PluginProcessor live;
            live.prepareToPlay (48000.0, 128);                     // starts the fade timer

            live.setStateInformation (saved.getData(), (int) saved.getSize());

            // The swap waits for the fade, so the board is bypassed and still the old one right after the call.
            expect (live.getSession().isBoardBypassed());

            juce::Thread::sleep (40);                              // past the 25 ms fade, and past one timer tick
            juce::Timer::callPendingTimersSynchronously();

            expect (! live.getSession().isBoardBypassed());
            expectWithinAbsoluteError (tempoOf (live.getSession()), 90.0f, 1.0e-4f);

            // The board underneath the fade is a live one: it still processes.
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            buffer.setSample (0, 0, 0.1f);

            juce::MidiBuffer midi;
            live.getSession().getProcessor().processBlock (buffer, midi);

            expect (std::isfinite (buffer.getSample (0, 0)));
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

PluginStateTests pluginStateTests;
} // namespace
