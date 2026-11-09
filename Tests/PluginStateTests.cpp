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

// One line naming where two board documents part company. Comparing two long JSON documents says nothing readable
// in a CI annotation, and the annotation is the only part of a failed run that can be read without a token.
juce::String firstDifference (const juce::String& before, const juce::String& after)
{
    const int limit = juce::jmin (before.length(), after.length());
    int i = 0;

    while (i < limit && before[i] == after[i])
        ++i;

    const int from = juce::jmax (0, i - 25);

    return "the board changed at character " + juce::String (i) + ": now '" + after.substring (from, i + 35)
           + "', was '" + before.substring (from, i + 35) + "'";
}

// The fade is a 30 Hz timer behind a 25 ms deadline, so a fixed sleep assumes an idle machine: on a loaded runner the
// callback has not been posted when the sleep ends, and the test then fails for a reason that has nothing to do with
// the plugin. Pump the pending timers until the condition holds, with a deadline, so a slow machine waits instead.
template <typename Predicate>
bool pumpTimersUntil (Predicate&& condition, int timeoutMs = 2000)
{
    const auto deadline = juce::Time::currentTimeMillis() + timeoutMs;

    for (;;)
    {
        juce::Timer::callPendingTimersSynchronously();

        if (condition())
            return true;

        if (juce::Time::currentTimeMillis() >= deadline)
            return false;

        juce::Thread::sleep (5);
    }
}

// Counts what the host would be told about one parameter.
class ChangeCounter final : public juce::AudioProcessorParameter::Listener
{
public:
    void parameterValueChanged (int, float) override { ++changes; }
    void parameterGestureChanged (int, bool) override {}
    int changes = 0;
};

// The board document inside a host state, which is what two states are compared on: the envelope's text can differ in
// formatting without the board differing.
juce::String boardTextIn (const juce::MemoryBlock& state)
{
    const auto parsed = juce::JSON::parse (juce::String::fromUTF8 ((const char*) state.getData(), (int) state.getSize()));
    return parsed.getProperty ("board", juce::var()).toString();
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

            const auto after = boardJsonOf (session);
            expect (after == before, firstDifference (before, after));
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

            // Nothing pumps timers while the test thread sleeps, so wait on the timer itself: it runs at 30 Hz and the
            // swap lands one fade (25 ms) after the state arrived.
            expect (pumpTimersUntil ([&live] { return ! live.getSession().isBoardBypassed(); }),
                    "the deferred restore did not come back within two seconds of the fade deadline");

            // A refused restore is the failure worth reading: "the tempo is 120" only says so indirectly.
            expect (live.getSession().getLastRestoreError().isEmpty(), live.getSession().getLastRestoreError());
            expectWithinAbsoluteError (tempoOf (live.getSession()), 90.0f, 1.0e-4f);

            // The board underneath the fade is a live one: it still processes.
            juce::AudioBuffer<float> buffer (2, 128);
            buffer.clear();
            buffer.setSample (0, 0, 0.1f);

            juce::MidiBuffer midi;
            live.getSession().getProcessor().processBlock (buffer, midi);

            expect (std::isfinite (buffer.getSample (0, 0)));
        }

        beginTest ("a state that will not read is refused before the board is bypassed for it");
        {
            pb::PluginProcessor live;
            live.prepareToPlay (48000.0, 128);                     // the fade timer is running: a restore would wait

            const auto before = boardJsonOf (live.getSession());
            const auto bad = envelope ("pumpboard-plugin-state", 1, "not a board");

            live.setStateInformation (bad.toRawUTF8(), (int) bad.getNumBytesAsUTF8());

            // It used to bypass the board, wait out the fade, find the board unreadable and bring it back: an audible
            // dip for a project file that was never going to change anything.
            expect (! live.getSession().isBoardBypassed());
            expect (live.getSession().getLastRestoreError().isNotEmpty());
            expect (boardJsonOf (live.getSession()) == before);
        }

        beginTest ("a host that asks for state while a restore waits is given the state it just set");
        {
            pb::PluginProcessor source;
            source.getSession().getMacros().setName (2, "WAITING");

            juce::MemoryBlock saved;
            source.getStateInformation (saved);

            pb::PluginProcessor live;
            live.prepareToPlay (48000.0, 128);
            live.setStateInformation (saved.getData(), (int) saved.getSize());
            expect (live.getSession().isBoardBypassed());          // still waiting behind the fade

            juce::MemoryBlock asked;
            live.getStateInformation (asked);

            // A host that saves straight after loading (an undo point, a preset store) would otherwise write the
            // board that was just replaced.
            expect (boardTextIn (asked) == boardTextIn (saved), "the state handed back was the old board");
        }

        beginTest ("deactivating the plugin lands a restore that was still waiting");
        {
            pb::PluginProcessor source;
            source.getSession().getMacros().setName (3, "LANDED");

            juce::MemoryBlock saved;
            source.getStateInformation (saved);

            pb::PluginProcessor live;
            live.prepareToPlay (48000.0, 128);
            live.setStateInformation (saved.getData(), (int) saved.getSize());

            // releaseResources stops the timer that would have landed it: the board stayed bypassed and the restore
            // was never applied, until some later prepareToPlay.
            live.releaseResources();

            expect (! live.getSession().isBoardBypassed());
            expectEquals (live.getSession().getMacros().getName (3), juce::String ("LANDED"));
        }

        beginTest ("the bypass behind a restore is not reported to the host as automation");
        {
            pb::PluginProcessor source;
            source.getSession().getMacros().setName (4, "QUIET");

            juce::MemoryBlock saved;
            source.getStateInformation (saved);

            pb::PluginProcessor live;
            live.prepareToPlay (48000.0, 128);

            auto* bypass = live.getAutomation().find ("board/bypass");
            ChangeCounter counter;
            bypass->addListener (&counter);

            live.setStateInformation (saved.getData(), (int) saved.getSize());

            expect (pumpTimersUntil ([&live] { return ! live.getSession().isBoardBypassed(); }));
            live.getAutomation().poll();

            // The fade is the plugin's own doing. A host in a write mode would have recorded it as a move of the
            // Board Bypass lane, once to 1 and once back to 0.
            expectEquals (counter.changes, 0);

            bypass->removeListener (&counter);
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
