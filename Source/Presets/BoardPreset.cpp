#include "Presets/BoardPreset.h"

namespace pb
{
namespace
{
// Same folder as the audio device settings (audio.xml), so the app keeps all of its state in one place.
// Built by hand for the same reason AudioEngine::getSetupFile does: userApplicationDataDirectory is ~/Library on
// macOS, so using it would file the board beside the framework folders instead of in Application Support.
juce::File boardDirectory()
{
    return juce::File::getSpecialLocation (juce::File::userHomeDirectory)
             .getChildFile ("Library")
             .getChildFile ("Application Support")
             .getChildFile ("pump BOARD");
}
} // namespace

juce::File boardFile()
{
    return boardDirectory().getChildFile ("board.xml");
}

juce::XmlElement* boardToXml (const BoardModel& model, const ParameterRegistry& registry)
{
    auto* root = new juce::XmlElement ("PUMPBOARD");
    root->setAttribute ("version", 1);

    // The tempo is board state, not a pedal's: without it a saved synced delay comes back at 120 BPM.
    const int tempoIndex = registry.find ("board/tempo");

    if (tempoIndex != ParameterRegistry::kInvalid)
        root->setAttribute ("tempo", (double) registry.get (tempoIndex));

    for (int i = 0; i < model.size(); ++i)
    {
        auto* slot = model.at (i);
        auto* effect = slot != nullptr ? slot->getEffect() : nullptr;

        if (effect == nullptr)
            continue;

        auto* slotXml = root->createNewChildElement ("slot");
        slotXml->setAttribute ("effect", effect->typeId());

        const auto params = effect->params();
        const auto& indices = slot->paramIndices();

        for (size_t p = 0; p < params.size() && p < indices.size(); ++p)
        {
            if (indices[p] == ParameterRegistry::kInvalid)
                continue;

            auto* paramXml = slotXml->createNewChildElement ("param");
            paramXml->setAttribute ("id", params[p].id);
            paramXml->setAttribute ("value", (double) registry.get (indices[p]));
        }

        if (slot->onParamIndex() != ParameterRegistry::kInvalid)
            slotXml->setAttribute ("on", (double) registry.get (slot->onParamIndex()));
    }

    return root;
}

bool boardFromXml (const juce::XmlElement& xml, BoardModel& model, ParameterRegistry& registry)
{
    if (! xml.hasTagName ("PUMPBOARD"))
        return false;

    // A file written before the board had a tempo leaves the current one alone rather than resetting it.
    const int tempoIndex = registry.find ("board/tempo");

    if (tempoIndex != ParameterRegistry::kInvalid && xml.hasAttribute ("tempo"))
        registry.set (tempoIndex, (float) xml.getDoubleAttribute ("tempo", 120.0));

    model.clear();

    for (int i = 0; i < xml.getNumChildElements(); ++i)
    {
        auto* slotXml = xml.getChildElement (i);

        if (slotXml == nullptr || ! slotXml->hasTagName ("slot"))
            continue;

        const auto typeId = slotXml->getStringAttribute ("effect");

        if (typeId.isEmpty() || ! model.insert (model.size(), typeId))
            continue;

        auto* slot = model.at (model.size() - 1);
        auto* effect = slot != nullptr ? slot->getEffect() : nullptr;

        if (effect == nullptr)
            continue;

        const auto params = effect->params();
        const auto& indices = slot->paramIndices();

        for (int c = 0; c < slotXml->getNumChildElements(); ++c)
        {
            auto* paramXml = slotXml->getChildElement (c);

            if (paramXml == nullptr || ! paramXml->hasTagName ("param"))
                continue;

            const auto id = paramXml->getStringAttribute ("id");
            const auto value = (float) paramXml->getDoubleAttribute ("value");

            for (size_t p = 0; p < params.size() && p < indices.size(); ++p)
                if (params[p].id == id && indices[p] != ParameterRegistry::kInvalid)
                    registry.set (indices[p], value);
        }

        // The switch is the board's bypass, so it lives beside the pedal rather than among its params.
        if (slotXml->hasAttribute ("on") && slot->onParamIndex() != ParameterRegistry::kInvalid)
            registry.set (slot->onParamIndex(), (float) slotXml->getDoubleAttribute ("on", 1.0));
    }

    return true;
}

bool saveBoard (const juce::File& file, const BoardModel& model, const ParameterRegistry& registry)
{
    file.getParentDirectory().createDirectory();

    const std::unique_ptr<juce::XmlElement> xml (boardToXml (model, registry));
    return xml != nullptr && xml->writeTo (file);
}

bool loadBoard (const juce::File& file, BoardModel& model, ParameterRegistry& registry)
{
    if (! file.existsAsFile())
        return false;

    const auto xml = juce::XmlDocument::parse (file);

    if (xml == nullptr)
        return false;

    return boardFromXml (*xml, model, registry);
}
} // namespace pb
