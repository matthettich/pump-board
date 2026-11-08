// Open items 3, 7 and 8 from the 6 Nov review (docs/HANDOVER.md), each pinned by the scenario that used to go wrong.
#include <juce_core/juce_core.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include "Control/MidiRouter.h"
#include "Control/ParameterRegistry.h"
#include "Engine/BoardProcessor.h"
#include "Engine/Slot.h"
#include "Presets/PresetManager.h"
#include <memory>
#include <vector>

namespace
{
// Outputs silence and reports a latency, so a bypassed slot / board returns only its delayed dry path.
class LatentSilentFx final : public pb::IEffect
{
public:
    explicit LatentSilentFx (int latencyIn) : latency (latencyIn) {}

    juce::String typeId() const override { return "open.latent"; }
    juce::String displayName() const override { return "Open Latent"; }
    std::vector<pb::ParamInfo> params() const override { return {}; }
    void prepare (double, int, int) override {}
    void reset() override {}
    void setParam (int, float) noexcept override {}
    void process (juce::AudioBuffer<float>& buffer) noexcept override { buffer.clear(); }
    int latencySamples() const noexcept override { return latency; }

private:
    int latency;
};

// Feeds a single impulse through `run` block by block and returns the output frame where it came back (-1 = never).
template <typename RunBlock>
int impulseDelay (int blockSize, int totalFrames, RunBlock&& run)
{
    juce::AudioBuffer<float> buffer (2, blockSize);

    for (int done = 0; done < totalFrames; done += blockSize)
    {
        buffer.clear();

        if (done == 0)
        {
            buffer.setSample (0, 0, 0.5f);
            buffer.setSample (1, 0, 0.5f);
        }

        run (buffer);

        for (int i = 0; i < blockSize; ++i)
            if (std::abs (buffer.getSample (0, i)) > 0.25f)
                return done + i;
    }

    return -1;
}
} // namespace

class OpenItemsTests final : public juce::UnitTest
{
public:
    OpenItemsTests() : juce::UnitTest ("OpenItems", "engine") {}

    static juce::File tempDir (const juce::String& name)
    {
        auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("pb-open-" + name);
        dir.deleteRecursively();
        dir.createDirectory();
        return dir;
    }

    static pb::BoardData board (const juce::String& name)
    {
        pb::BoardData data;
        data.name = name;
        return data;
    }

    void runTest() override
    {
        // ---- item 3: dry rings sized from the sample rate ----------------------------------------------------
        beginTest ("the slot ring scales with the sample rate and never drops below the old fixed size");
        {
            expectEquals (pb::Slot::maxDelayFor (48000.0), 16800);
            expectEquals (pb::Slot::maxDelayFor (96000.0), 33600);
            expectEquals (pb::Slot::maxDelayFor (192000.0), 67200);
            expectEquals (pb::Slot::maxDelayFor (8000.0), 8192);       // the floor
            expectEquals (pb::Slot::maxDelayFor (0.0), 16800);         // a nonsense rate falls back to 48k

            pb::ParameterRegistry registry;
            pb::Slot slot { "ringsize", registry };
            slot.prepare (96000.0, 64);
            expectEquals (slot.getMaxDelay(), 33600);
        }

        beginTest ("a slot at 96 kHz compensates a 16384-frame latency instead of clamping it to 8192");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot { "latency96", registry };
            slot.setEffect (std::make_unique<LatentSilentFx> (16384));
            registry.set (slot.onParamIndex(), 0.0f);          // bypassed from the start: only the delayed dry comes out
            slot.prepare (96000.0, 64);

            const int delay = impulseDelay (64, 16384 + 256, [&] (juce::AudioBuffer<float>& b) { slot.process (b); });
            expectEquals (delay, 16384);
        }

        beginTest ("a latency beyond the ring is clamped to it, not read out of bounds");
        {
            pb::ParameterRegistry registry;
            pb::Slot slot { "latencyHuge", registry };
            slot.setEffect (std::make_unique<LatentSilentFx> (1000000));
            registry.set (slot.onParamIndex(), 0.0f);
            slot.prepare (48000.0, 64);

            const int delay = impulseDelay (64, 16800 + 256, [&] (juce::AudioBuffer<float>& b) { slot.process (b); });
            expectEquals (delay, slot.getMaxDelay());
        }

        beginTest ("the board ring is four slots' worth and compensates a chain deeper than the old 16384");
        {
            expectEquals (pb::BoardProcessor::maxDryDelayFor (48000.0), 4 * 16800);
            expectEquals (pb::BoardProcessor::maxDryDelayFor (96000.0), 4 * 33600);
            expectEquals (pb::BoardProcessor::maxDryDelayFor (4000.0), 4 * 8192);

            pb::ParameterRegistry registry;
            pb::MidiRouter router { registry };
            pb::BoardProcessor processor { registry, router };
            processor.prepareToPlay (96000.0, 64);
            expectEquals (processor.getMaxDryDelay(), 4 * 33600);

            // Two latent slots in series: 2 x 20000 frames of chain latency, over the old 16384 cap. Board bypass
            // on, so what comes out is the board's own dry ring read that far back.
            auto chain = std::make_unique<pb::ChainSnapshot>();
            for (int i = 0; i < 2; ++i)
            {
                auto slot = std::make_shared<pb::Slot> ("deep" + juce::String (i), registry);
                slot->setEffect (std::make_unique<LatentSilentFx> (20000));
                slot->prepare (96000.0, 64);
                expect (chain->push (slot));
            }

            processor.publish (std::move (chain));

            const int bypassIndex = registry.find ("board/bypass");
            expect (bypassIndex != pb::ParameterRegistry::kInvalid);
            registry.set (bypassIndex, 1.0f);
            processor.prepareToPlay (96000.0, 64);         // settles the bypass fade at "bypassed"

            juce::MidiBuffer midi;
            const int delay = impulseDelay (64, 40000 + 512, [&] (juce::AudioBuffer<float>& b) { processor.processBlock (b, midi); });
            expectEquals (delay, 40000);
        }

        // ---- item 7: PresetManager edges ---------------------------------------------------------------------
        beginTest ("a name of only illegal characters becomes a visible default name, not a hidden .board");
        {
            const auto dir = tempDir ("illegal");
            const pb::PresetManager manager (dir);

            for (const auto* typed : { "///", "???", "...", " . . ", "<>:|" })
            {
                const auto file = manager.fileFor (typed);
                expect (! file.getFileName().startsWithChar ('.'), juce::String ("hidden file for ") + typed);
                expectEquals (file.getFileName(), juce::String ("Untitled.board"));
            }

            const auto saved = manager.saveAs ("***", board ("***"));
            expect (saved.existsAsFile());
            expectEquals (manager.list().size(), 1);
            expectEquals (manager.nameOf (manager.list()[0]), juce::String ("Untitled"));

            // leading dots are stripped from an otherwise good name, and ordinary names are untouched
            expectEquals (manager.fileFor (".secret").getFileName(), juce::String ("secret.board"));
            expectEquals (manager.fileFor ("Clean Tone").getFileName(), juce::String ("Clean Tone.board"));

            dir.deleteRecursively();
        }

        beginTest ("a case-only rename really renames, and leaves exactly one board");
        {
            const auto dir = tempDir ("case");
            const pb::PresetManager manager (dir);

            const auto original = manager.saveAs ("clean", board ("clean"));
            expect (original.existsAsFile());

            expect (manager.rename (original, "Clean"));

            const auto files = manager.list();
            expectEquals (files.size(), 1);
            expectEquals (files[0].getFileName(), juce::String ("Clean.board"));

            pb::BoardData loaded;
            pb::LoadResult result;
            expect (manager.read (files[0], loaded, result));
            expectEquals (loaded.name, juce::String ("Clean"));

            // no scratch file left behind
            expectEquals (dir.getNumberOfChildFiles (juce::File::findFiles, "*"), 1);

            // renaming to the very same name is still a quiet success
            expect (manager.rename (files[0], "Clean"));
            expectEquals (manager.list().size(), 1);

            dir.deleteRecursively();
        }

        beginTest ("rename does not leave two copies when the Bin refuses the old file");
        {
            const auto dir = tempDir ("trashfail");
            pb::PresetManager manager (dir);
            manager.setTrashFunction ([] (const juce::File&) { return false; });

            const auto first = manager.saveAs ("Clean", board ("Clean"));
            expect (first.existsAsFile());

            expect (! manager.rename (first, "Dirty"));
            expect (first.existsAsFile(), "the original is untouched");
            expect (! manager.fileFor ("Dirty").existsAsFile(), "the new copy was rolled back");
            expectEquals (manager.list().size(), 1);
            expect (! manager.remove (first), "remove reports the Bin's refusal");

            manager.setTrashFunction ([] (const juce::File& f) { return f.deleteFile(); });
            expect (manager.rename (first, "Dirty"));
            expectEquals (manager.list().size(), 1);
            expectEquals (manager.nameOf (manager.list()[0]), juce::String ("Dirty"));

            dir.deleteRecursively();
        }

        // ---- item 8: any number of routers ------------------------------------------------------------------
        beginTest ("twenty routers alive at once each keep a working table and retire old ones safely");
        {
            struct One
            {
                pb::ParameterRegistry registry;
                std::unique_ptr<pb::MidiRouter> router;
                int toggle = pb::ParameterRegistry::kInvalid;
            };

            std::vector<std::unique_ptr<One>> all;

            for (int i = 0; i < 20; ++i)
            {
                auto one = std::make_unique<One>();
                one->router = std::make_unique<pb::MidiRouter> (one->registry);

                pb::ParameterRegistry::Entry entry;
                entry.id = "t/toggle";
                entry.displayName = "toggle";
                entry.minValue = 0.0f;
                entry.maxValue = 1.0f;
                entry.defaultValue = 0.0f;
                entry.isToggle = true;
                one->toggle = one->registry.add (entry);
                all.push_back (std::move (one));
            }

            auto tableFor = [] (int param, int note)
            {
                auto table = std::make_unique<pb::MappingTable>();
                pb::Mapping m;
                m.source = { pb::SourceType::Note, 0, note };
                m.action = pb::ActionType::Toggle;
                m.paramIndex = param;
                table->mappings.push_back (m);
                return table;
            };

            auto noteOn = [] (int note)
            {
                juce::MidiBuffer midi;
                midi.addEvent (juce::MidiMessage::noteOn (1, note, (juce::uint8) 100), 0);
                return midi;
            };

            for (auto& one : all)
            {
                one->router->publish (tableFor (one->toggle, 60));
                one->router->publish (tableFor (one->toggle, 61));   // retires the first: it must outlive this call
                one->router->publish (tableFor (one->toggle, 62));   // and the second

                one->router->handle (noteOn (62));                   // block 1 reads the live table
                one->router->handle (noteOn (60));                   // an old mapping no longer fires
                one->router->handle (juce::MidiBuffer());
                one->router->collectGarbage();
                one->router->handle (juce::MidiBuffer());
                one->router->collectGarbage();                       // two more blocks: the retired tables go now
            }

            for (auto& one : all)
                expectEquals (one->registry.get (one->toggle), 1.0f);   // flipped once by note 62, never by 60

            // Destroy out of order: a freed router's slot is reused cleanly.
            all.erase (all.begin() + 3);
            all.erase (all.begin() + 9);

            pb::ParameterRegistry registry;
            pb::MidiRouter again { registry };
            again.publish (tableFor (pb::ParameterRegistry::kInvalid, 1));
            again.publish (tableFor (pb::ParameterRegistry::kInvalid, 2));
            again.handle (juce::MidiBuffer());
            again.handle (juce::MidiBuffer());
            again.collectGarbage();
            expect (true);
        }
    }
};

static OpenItemsTests openItemsTests;
