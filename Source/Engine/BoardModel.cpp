#include "Engine/BoardModel.h"
#include "Engine/BoardProcessor.h"

namespace pb
{
BoardModel::BoardModel (BoardProcessor& processorIn, EffectRegistry& effectsIn, ParameterRegistry& registryIn)
    : processor (processorIn), effects (effectsIn), registry (registryIn)
{
}

Slot* BoardModel::at (int index) const
{
    if (index < 0 || index >= (int) entries.size())
        return nullptr;

    return entries[(size_t) index].slot.get();
}

juce::String BoardModel::categoryAt (int index) const
{
    if (index < 0 || index >= (int) entries.size())
        return {};

    return entries[(size_t) index].category;
}

int BoardModel::indexOfUid (const juce::String& uid) const
{
    for (int i = 0; i < (int) entries.size(); ++i)
        if (entries[(size_t) i].slot->getUid() == uid)
            return i;

    return -1;
}

bool BoardModel::insert (int index, const juce::String& typeId)
{
    if ((int) entries.size() >= ChainSnapshot::kMaxSlots)
        return false;

    auto effect = effects.create (typeId);

    if (effect == nullptr)
        return false;

    juce::String category;
    for (const auto& descriptor : effects.list())
        if (descriptor.typeId == typeId)
            category = descriptor.category;

    auto slot = std::make_shared<Slot> ("s" + juce::String (++uidCounter), registry);
    slot->setEffect (std::move (effect));
    slot->prepare (processor.getSampleRateHz(), processor.getMaxBlock());

    index = juce::jlimit (0, (int) entries.size(), index);
    entries.insert (entries.begin() + index, Entry { slot, category });

    publish();
    return true;
}

bool BoardModel::replace (int index, const juce::String& typeId)
{
    if (index < 0 || index >= (int) entries.size())
        return false;

    auto effect = effects.create (typeId);

    if (effect == nullptr)
        return false;

    juce::String category;
    for (const auto& descriptor : effects.list())
        if (descriptor.typeId == typeId)
            category = descriptor.category;

    // A successor, not the same Slot: the audio thread may be running the old one this very block, so it must not
    // be touched. The successor keeps the uid and the on switch; the old slot and its pedal are freed once the
    // last snapshot holding them is retired.
    auto& entry = entries[(size_t) index];
    auto next = entry.slot->makeSuccessor();
    next->setEffect (std::move (effect));
    next->prepare (processor.getSampleRateHz(), processor.getMaxBlock());
    entry.slot = std::move (next);
    entry.category = category;

    publish();
    return true;
}

bool BoardModel::clearSlot (int index)
{
    if (index < 0 || index >= (int) entries.size())
        return false;

    auto& entry = entries[(size_t) index];

    if (entry.slot->getEffect() == nullptr)
        return false;

    // A successor with no pedal passes audio untouched; see replace() for why the live slot is left alone.
    auto next = entry.slot->makeSuccessor();
    next->prepare (processor.getSampleRateHz(), processor.getMaxBlock());
    entry.slot = std::move (next);
    entry.category = {};

    publish();
    return true;
}

void BoardModel::remove (int index)
{
    if (index < 0 || index >= (int) entries.size())
        return;

    entries.erase (entries.begin() + index);
    publish();
}

bool BoardModel::move (int from, int to)
{
    if (from == to || from < 0 || from >= (int) entries.size())
        return false;

    to = juce::jlimit (0, (int) entries.size() - 1, to);

    auto entry = entries[(size_t) from];
    entries.erase (entries.begin() + from);
    entries.insert (entries.begin() + to, entry);

    publish();
    return true;
}

void BoardModel::clear()
{
    entries.clear();
    publish();
}

void BoardModel::prepareAll()
{
    // prepare() reallocates the slot's ring and the pedal's own buffers, so the audio thread must be off these
    // slots first: take the chain away, wait out the blocks that might still be running it, then put it back.
    // (Before Nov 2026 this re-prepared the live slots in place.) If the wait times out the device is not calling
    // back, which is just as safe.
    processor.detachChain();

    for (auto& entry : entries)
        entry.slot->prepare (processor.getSampleRateHz(), processor.getMaxBlock());

    publish();
}

bool BoardModel::insertEmpty (int index)
{
    if ((int) entries.size() >= ChainSnapshot::kMaxSlots)
        return false;

    auto slot = std::make_shared<Slot> ("s" + juce::String (++uidCounter), registry);
    slot->prepare (processor.getSampleRateHz(), processor.getMaxBlock());

    index = juce::jlimit (0, (int) entries.size(), index);
    entries.insert (entries.begin() + index, Entry { slot, {} });

    publish();
    return true;
}

void BoardModel::publish()
{
    auto snapshot = std::make_unique<ChainSnapshot>();

    for (auto& entry : entries)
        snapshot->push (entry.slot);      // shared_ptrs keep slots alive while a snapshot still points at them

    processor.publish (std::move (snapshot));
}
} // namespace pb
