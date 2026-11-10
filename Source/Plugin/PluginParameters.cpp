#include "Plugin/PluginParameters.h"
#include <cmath>

namespace pb
{
namespace
{
float normaliseValue (float value, float minValue, float maxValue) noexcept
{
    const float span = maxValue - minValue;
    return span > 1.0e-9f ? juce::jlimit (0.0f, 1.0f, (value - minValue) / span) : 0.0f;
}

float denormaliseValue (float normalised, float minValue, float maxValue) noexcept
{
    return minValue + juce::jlimit (0.0f, 1.0f, normalised) * (maxValue - minValue);
}

// The name a macro is listed under when the user has not renamed it. One place, because a rename that is cleared
// back to empty has to fall back to this again rather than leaving the host with a nameless parameter.
juce::String macroName (int macro)
{
    return "Macro " + juce::String (macro + 1);
}

// FNV-1a, the same shape the board uses to notice that its chain moved.
uint64_t hashStep (uint64_t hash, uint64_t value) noexcept
{
    return (hash ^ value) * UINT64_C (1099511628211);
}
} // namespace

PluginParameter::PluginParameter (ParameterRegistry& registryIn, juce::String paramId, juce::String paramName,
                                  Kind kindIn, float minValueIn, float maxValueIn, float defaultValueIn,
                                  int registryIndex, juce::String suffixIn)
    : juce::AudioProcessorParameterWithID (juce::ParameterID (paramId, 1), paramName),
      registry (registryIn),
      kind (kindIn),
      minValue (minValueIn),
      maxValue (maxValueIn),
      defaultValue (defaultValueIn),
      suffix (std::move (suffixIn)),
      currentName (paramName),
      index (registryIndex),
      lastPushed (normaliseValue (defaultValueIn, minValueIn, maxValueIn))
{
    addListener (this);      // the only way to see a gesture: see beginChangeGesture above
}

PluginParameter::~PluginParameter()
{
    removeListener (this);
}

float PluginParameter::getValue() const
{
    const int current = getRegistryIndex();

    if (current == ParameterRegistry::kInvalid)
        return normalised (defaultValue);

    return registry.getNormalised (current);
}

void PluginParameter::setValue (float newValue)
{
    const float clamped = juce::jlimit (0.0f, 1.0f, newValue);

    // Remembered first: the host's own move must never come back to it as ours.
    lastPushed.store (clamped, std::memory_order_relaxed);

    const int current = getRegistryIndex();

    if (current != ParameterRegistry::kInvalid)
        registry.setNormalised (current, clamped);      // any thread, and it clamps to the entry's own range
}

float PluginParameter::getDefaultValue() const
{
    return normalised (defaultValue);
}

int PluginParameter::getNumSteps() const
{
    // A switch is two values; anything else is continuous, so the host lets the user pick any point of it.
    return kind == Kind::toggle ? 2 : juce::AudioProcessorParameter::getDefaultNumParameterSteps();
}

juce::String PluginParameter::getText (float normalisedValue, int maximumStringLength) const
{
    const float value = actualValue (normalisedValue);

    juce::String text;

    if (kind == Kind::toggle)
        text = value >= 0.5f ? "On" : "Off";
    else if (minValue >= 0.0f && maxValue <= 1.0001f)
        text = juce::String (juce::roundToInt (value * 100.0f)) + "%";      // a macro: 0..1 reads as a percentage
    else
        text = juce::String (value, 2) + suffix;

    return maximumStringLength > 0 ? text.substring (0, maximumStringLength) : text;
}

float PluginParameter::getValueForText (const juce::String& text) const
{
    // The inverse of getText(), because a host's generic editor lets you type into a parameter. A switch has two
    // answers and accepts the words as well as 0 and 1; a percentage is read as one.
    if (kind == Kind::toggle)
    {
        const auto typed = text.trim().toLowerCase();
        return (typed == "on" || typed == "true" || typed == "1") ? 1.0f : 0.0f;
    }

    const auto typed = text.trim();
    float value = typed.getFloatValue();

    if (typed.containsChar ('%'))
        value /= 100.0f;

    return normalised (value);
}

juce::String PluginParameter::getName (int maximumStringLength) const
{
    // A host reads this from threads of its own while the message thread may be renaming a macro, so the string is
    // copied out under the lock and trimmed outside it. Never called from this plugin's audio thread.
    juce::String name;

    {
        const juce::SpinLock::ScopedLockType lock (nameLock);
        name = currentName;
    }

    return maximumStringLength > 0 ? name.substring (0, maximumStringLength) : name;
}

void PluginParameter::parameterGestureChanged (int, bool isStarting)
{
    hostGesture.store (isStarting, std::memory_order_relaxed);
}

void PluginParameter::setRegistryIndex (int newIndex) noexcept
{
    index.store (newIndex, std::memory_order_relaxed);
}

bool PluginParameter::setName (const juce::String& newName)
{
    const juce::SpinLock::ScopedLockType lock (nameLock);

    // The caller runs at 30 Hz and the name almost never changes: leave the string alone when it has not.
    if (currentName == newName)
        return false;

    currentName = newName;
    return true;
}

void PluginParameter::pollRegistry()
{
    const int current = getRegistryIndex();

    if (current == ParameterRegistry::kInvalid)
        return;

    const float now = registry.getNormalised (current);

    if (std::abs (now - lastPushed.load (std::memory_order_relaxed)) <= 1.0e-5f)
        return;

    lastPushed.store (now, std::memory_order_relaxed);

    // The host is holding this control: it is writing setValue() itself, and a gesture it started must not be
    // interrupted by setValueNotifyingHost() (card 8.4).
    if (hostGesture.load (std::memory_order_relaxed))
        return;

    setValueNotifyingHost (now);
}

void PluginParameter::resync() noexcept
{
    lastPushed.store (getValue(), std::memory_order_relaxed);
}

float PluginParameter::normalised (float actual) const noexcept
{
    return normaliseValue (actual, minValue, maxValue);
}

float PluginParameter::actualValue (float normalisedValue) const noexcept
{
    return denormaliseValue (normalisedValue, minValue, maxValue);
}

PluginParameters::PluginParameters (ParameterRegistry& registryIn, MacroBank& macrosIn, BoardModel& modelIn)
    : registry (registryIn),
      macros (macrosIn),
      model (modelIn)
{
    const auto add = [this] (juce::String paramId, juce::String paramName, PluginParameter::Kind kind,
                             float minValue, float maxValue, float defaultValue, int registryIndex,
                             juce::String suffix = {})
    {
        auto* parameter = new PluginParameter (registry, std::move (paramId), std::move (paramName), kind,
                                               minValue, maxValue, defaultValue, registryIndex, std::move (suffix));
        parameters.push_back (parameter);
        return parameter;
    };

    // A board parameter takes its range from the registry entry, so the host shows what the board shows.
    const auto addBoardParam = [&] (const juce::String& paramId, const juce::String& paramName,
                                    PluginParameter::Kind kind)
    {
        const int index = registry.find (paramId);
        float minValue = 0.0f, maxValue = 1.0f, defaultValue = 0.0f;
        juce::String suffix;

        if (index != ParameterRegistry::kInvalid)
        {
            const auto& entry = registry.info (index);
            minValue = entry.minValue;
            maxValue = entry.maxValue;
            defaultValue = entry.defaultValue;
            // By id: this used to compare the display name against a differently-cased string, which never matched,
            // and the suffix was computed and then never handed to the parameter, so a host showed a bare number.
            suffix = paramId == "board/inputTrim" || paramId == "board/outputTrim" ? " dB" : juce::String();
        }

        return add (paramId, paramName, kind, minValue, maxValue, defaultValue, index, suffix);
    };

    // The macros first, so a host's generic list opens on the eight knobs a player automates.
    for (int m = 0; m < MacroBank::kNumMacros; ++m)
    {
        const int index = macros.macroParamIndex (m);
        const auto stored = macros.getName (m);

        macroParams[(size_t) m] = add ("macro/" + juce::String (m),
                                       stored.isNotEmpty() ? stored : macroName (m),
                                       PluginParameter::Kind::continuous, 0.0f, 1.0f, 0.0f, index);
    }

    addBoardParam ("board/bypass", "Board Bypass", PluginParameter::Kind::toggle);
    addBoardParam ("board/inputTrim", "Input Trim", PluginParameter::Kind::continuous);
    addBoardParam ("board/outputTrim", "Output Trim", PluginParameter::Kind::continuous);

    // The sixteen switches, by position. The index behind each is re-pointed by refreshChain() as the chain moves.
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* slot = model.at (i);
        slotOn[(size_t) i] = add ("slot/" + juce::String (i) + "/on",
                                  "Slot " + juce::String (i + 1) + " On",
                                  PluginParameter::Kind::toggle, 0.0f, 1.0f, 1.0f,
                                  slot != nullptr ? slot->onParamIndex() : ParameterRegistry::kInvalid);
    }
}

void PluginParameters::addTo (juce::AudioProcessor& owner)
{
    for (auto* parameter : parameters)
        owner.addParameter (parameter);
}

void PluginParameters::poll()
{
    for (auto* parameter : parameters)
        parameter->pollRegistry();
}

void PluginParameters::resync() noexcept
{
    for (auto* parameter : parameters)
        parameter->resync();
}

bool PluginParameters::refreshChain()
{
    for (int i = 0; i < kNumSlots; ++i)
    {
        auto* slot = model.at (i);
        slotOn[(size_t) i]->setRegistryIndex (slot != nullptr ? slot->onParamIndex() : ParameterRegistry::kInvalid);
    }

    uint64_t signature = hashStep (UINT64_C (1469598103934665603), (uint64_t) model.size());

    for (int i = 0; i < model.size(); ++i)
    {
        auto* slot = model.at (i);
        const auto* effect = slot != nullptr ? slot->getEffect() : nullptr;
        signature = hashStep (signature, (uint64_t) (uintptr_t) effect);
    }

    // A renamed macro changes a name the host draws, not a value, so the parameter has to carry the new name and the
    // signature has to move with it, which is what gets updateHostDisplay called.
    for (int m = 0; m < MacroBank::kNumMacros; ++m)
    {
        const auto stored = macros.getName (m);
        const auto name = stored.isNotEmpty() ? stored : macroName (m);

        macroParams[(size_t) m]->setName (name);

        for (int c = 0; c < name.length(); ++c)
            signature = hashStep (signature, (uint64_t) name[c]);
    }

    if (chainValid && signature == lastSignature)
        return false;

    chainValid = true;
    lastSignature = signature;
    return true;
}

PluginParameter* PluginParameters::find (const juce::String& paramId) const
{
    for (auto* parameter : parameters)
        if (parameter->getParameterID() == paramId)
            return parameter;

    return nullptr;
}
} // namespace pb
