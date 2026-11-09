#include "Plugin/PluginSession.h"
#include "Effects/EffectRegistry.h"
#include "Presets/BoardSerializer.h"
#include "Presets/BoardState.h"
#include <mutex>

namespace pb
{
namespace
{
// The envelope around the board document. These keys are the contract with a saved host project, so they are named
// once here rather than spelled as literals at each use.
constexpr const char* kStateFormatKey = "format";
constexpr const char* kStateFormat = "pumpboard-plugin-state";
constexpr const char* kStateVersionKey = "version";
constexpr const char* kStateBoardKey = "board";
constexpr const char* kStateTempoKey = "tempo";
constexpr const char* kStateTempoId = "board/tempo";
constexpr int kStateVersion = 1;
// Two plugin instances share one process, and one registry: registering the pedals twice would assert on the second
// instance's load, and two instances can be created at once on different threads. call_once makes that one decision
// however many hosts race to load the plugin; the contains check stays inside it so a test binary that registered
// the pedals itself is still respected (CodeRabbit, PR #2).
void ensureBuiltInEffects()
{
    static std::once_flag once;

    std::call_once (once, []
    {
        auto& registry = EffectRegistry::instance();

        if (! registry.contains ("pump.tube"))
            registerBuiltInEffects (registry);
    });
}

// Why a state would be refused, or an empty string when it reads. Nothing is applied: this is the whole of the
// checking, shared by restoreState() and by canRestoreState(), so the two cannot disagree about what is acceptable.
juce::String readState (const juce::var& state, BoardData& data, juce::var& tempo)
{
    auto* object = state.getDynamicObject();

    if (object == nullptr)
        return "the state is not an object";

    if (object->getProperty (kStateFormatKey).toString() != kStateFormat)
        return "the state belongs to something else: '" + object->getProperty (kStateFormatKey).toString() + "'";

    const auto version = object->getProperty (kStateVersionKey);

    if (! (version.isInt() || version.isInt64() || version.isDouble()))
        return "the state has no version";

    // Exactly this build's version. An older one has no reader yet, and a newer one was written by a build that knew
    // more than this one: half-applying it would keep the pedals and lose whatever it added. Compared as a double:
    // a version written as 1e300 must not be cast to an int on its way to being refused.
    if ((double) version != (double) kStateVersion)
        return "the state is version " + juce::String ((double) version) + ", and this build writes "
               + juce::String (kStateVersion);

    const auto boardText = object->getProperty (kStateBoardKey).toString();

    if (boardText.isEmpty())
        return "the state holds no board";

    const auto parsed = fromJson (boardText, data);

    if (! parsed.ok)
        return "the board in the state did not read: " + parsed.error;

    tempo = object->getProperty (kStateTempoKey);
    return {};
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
    tempoIndex = registry.find (kStateTempoId);
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

void PluginSession::setEditorAttached (bool attached)
{
    editorAttached = attached;
}

void PluginSession::collectGarbage()
{
    processor.collectGarbage();
    midiRouter.collectGarbage();

    // Card 8.5: with no editor, nobody drains the router's events, so a learn result or a program change would sit in
    // the FIFO for the life of the plugin. Dropping them is the honest answer for now: card 8.7 gives program changes
    // a home that does not need a window.
    if (! editorAttached)
        midiRouter.drainEvents ([] (const ControlEvent&) {});
}

void PluginSession::loadDefaultBoard()
{
    // The app's opening chain: drive, then delay, then space, inserted at the end so the order is the chain's order.
    const char* const chain[] = { "pump.tube", "pump.echo", "pump.spring" };

    for (const auto* typeId : chain)
        model.insert (model.size(), typeId);
}

juce::var PluginSession::captureState() const
{
    const int tempoIndex = registry.find (kStateTempoId);

    auto* state = new juce::DynamicObject();
    state->setProperty (kStateFormatKey, kStateFormat);
    state->setProperty (kStateVersionKey, kStateVersion);
    state->setProperty (kStateBoardKey, toJson (capture (model, macros, learn, registry)));
    state->setProperty (kStateTempoKey,
                        tempoIndex != ParameterRegistry::kInvalid ? registry.get (tempoIndex) : 120.0f);

    return juce::var (state);
}

bool PluginSession::canRestoreState (const juce::var& state)
{
    BoardData data;
    juce::var tempo;

    lastRestoreError = readState (state, data, tempo);
    return lastRestoreError.isEmpty();
}

bool PluginSession::restoreState (const juce::var& state)
{
    // Card 8.3: a host only ever sees the bool, which says nothing about why its project's state was dropped. The
    // reason is kept here instead, for the host's log and for the tests.
    const auto refuse = [this] (const juce::String& reason)
    {
        lastRestoreError = reason;
        return false;
    };

    // Parse before touching anything: a board that will not read leaves the current one exactly as it was.
    BoardData data;
    juce::var tempo;

    if (const auto reason = readState (state, data, tempo); reason.isNotEmpty())
        return refuse (reason);

    lastRestoreError.clear();

    const auto applied = apply (data, model, macros, learn, registry);

    if (! applied.ok)
        return refuse ("the board read but could not be applied: " + applied.error);

    const int tempoIndex = registry.find (kStateTempoId);

    // The registry clamps to the parameter's own range, so a nonsense BPM in a project file cannot reach the clock.
    if (tempoIndex != ParameterRegistry::kInvalid && (tempo.isInt() || tempo.isDouble()))
        registry.set (tempoIndex, (float) (double) tempo);

    // Card 8.5: the slots the editor may be holding are gone with the old chain. Tell it before returning, so its
    // panel is re-pointed before its own timer can run again.
    if (onBoardReplaced != nullptr)
        onBoardReplaced();

    return true;
}

bool PluginSession::isBoardBypassed() const
{
    const int index = registry.find ("board/bypass");
    return index != ParameterRegistry::kInvalid && registry.get (index) >= 0.5f;
}

void PluginSession::setBoardBypassed (bool bypassed)
{
    const int index = registry.find ("board/bypass");

    if (index != ParameterRegistry::kInvalid)
        registry.set (index, bypassed ? 1.0f : 0.0f);
}
} // namespace pb
