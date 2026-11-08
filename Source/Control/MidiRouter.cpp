#include "Control/MidiRouter.h"
#include <algorithm>
#include <cstdint>
#include <vector>

namespace pb
{
namespace
{
// MidiRouter.h is a fixed scaffold header: it has no room for the block counter or the retire list that
// publish / collectGarbage / handle need, and the house rules forbid adding members to it. The state lives
// here instead, one slot per live router, claimed on the message thread at construction and released at
// destruction. Lookups are a handful of atomic loads, never a lock or an allocation (see docs/NOTES.md).
struct RetiredTable
{
    MappingTable* table = nullptr;
    uint64_t retiredAtBlock = 0;
};

struct RouterState
{
    std::atomic<const MidiRouter*> owner { nullptr };
    std::atomic<uint64_t> blocksProcessed { 0 };
    std::vector<RetiredTable> retired;           // message thread only
    RouterState* next = nullptr;                 // fixed once the node is linked into the list
};

// Open item 8 (6 Nov review): the table used to be eight fixed slots, and a ninth router found none, so its
// publish() deleted the old mapping table at once while the audio thread could still be reading it. Nodes now
// form a push-only list: a router claims a free node or, on the message thread (construction), links a new one.
// The audio thread only walks the list with acquire loads - no lock, no allocation - and a node is never unlinked
// or freed while the process runs, so any pointer it has seen stays valid.
struct StatePool
{
    std::atomic<RouterState*> head { nullptr };

    ~StatePool()
    {
        auto* node = head.load (std::memory_order_acquire);

        while (node != nullptr)
        {
            auto* following = node->next;
            delete node;
            node = following;
        }
    }
};

StatePool& statePool()
{
    static StatePool pool;
    return pool;
}

RouterState* claimState (const MidiRouter* router)
{
    auto& pool = statePool();

    for (auto* state = pool.head.load (std::memory_order_acquire); state != nullptr; state = state->next)
    {
        const MidiRouter* unclaimed = nullptr;

        if (state->owner.compare_exchange_strong (unclaimed, router, std::memory_order_acq_rel))
        {
            state->blocksProcessed.store (0, std::memory_order_relaxed);
            state->retired.clear();
            return state;
        }
    }

    // None free: grow the list (message thread, construction time).
    auto* fresh = new RouterState();
    fresh->owner.store (router, std::memory_order_relaxed);
    fresh->next = pool.head.load (std::memory_order_relaxed);

    while (! pool.head.compare_exchange_weak (fresh->next, fresh, std::memory_order_acq_rel, std::memory_order_relaxed))
    {
    }

    return fresh;
}

RouterState* stateFor (const MidiRouter* router) noexcept
{
    for (auto* state = statePool().head.load (std::memory_order_acquire); state != nullptr; state = state->next)
        if (state->owner.load (std::memory_order_acquire) == router)
            return state;

    return nullptr;
}
} // namespace

MidiRouter::MidiRouter (ParameterRegistry& registryIn)
    : registry (registryIn)
{
    claimState (this);
}

MidiRouter::~MidiRouter()
{
    delete active.exchange (nullptr, std::memory_order_acquire);

    if (auto* state = stateFor (this))
    {
        for (auto& retired : state->retired)
            delete retired.table;

        state->retired.clear();
        state->owner.store (nullptr, std::memory_order_release);
    }
}

void MidiRouter::publish (std::unique_ptr<MappingTable> next)
{
    auto* incoming = next.release();
    auto* previous = active.exchange (incoming, std::memory_order_acq_rel);

    if (previous == nullptr)
        return;

    if (auto* state = stateFor (this))
        state->retired.push_back ({ previous, state->blocksProcessed.load (std::memory_order_acquire) });
    else
        delete previous;
}

void MidiRouter::collectGarbage()
{
    auto* state = stateFor (this);

    if (state == nullptr)
        return;

    // A table retired during block N may still be read until block N+1 ends, so it goes at N+2.
    const auto now = state->blocksProcessed.load (std::memory_order_acquire);

    state->retired.erase (std::remove_if (state->retired.begin(), state->retired.end(),
                                          [now] (const RetiredTable& retired)
                                          {
                                              if (retired.retiredAtBlock + 2 <= now)
                                              {
                                                  delete retired.table;
                                                  return true;
                                              }

                                              return false;
                                          }),
                          state->retired.end());
}

void MidiRouter::beginLearn()
{
    learning.store (true, std::memory_order_relaxed);
}

void MidiRouter::cancelLearn()
{
    learning.store (false, std::memory_order_relaxed);
}

bool MidiRouter::handleKey (int keyCode, bool isDown, ControlEvent& presetEvent)
{
    const auto* table = active.load (std::memory_order_acquire);

    if (table == nullptr)
        return false;

    // Keys carry no channel: a Key mapping's channel field is ignored, and its `number` is the key code.
    const MappingSource src { SourceType::Key, 0, keyCode };
    bool havePreset = false;

    for (const auto& mapping : table->mappings)
    {
        if (mapping.source.type != SourceType::Key || mapping.source.number != keyCode)
            continue;

        const bool hasTarget = mapping.paramIndex != ParameterRegistry::kInvalid;

        switch (mapping.action)
        {
            case ActionType::Toggle:
                if (isDown && hasTarget)
                    registry.toggle (mapping.paramIndex);
                break;

            case ActionType::Momentary:
                if (hasTarget)
                    registry.set (mapping.paramIndex, isDown ? mapping.maxValue : mapping.minValue);
                break;

            case ActionType::PresetSelect:
            case ActionType::PresetNext:
            case ActionType::PresetPrev:
                if (isDown)
                {
                    // Returned, not queued: the FIFO has exactly one producer, the audio thread.
                    presetEvent.kind = mapping.action == ActionType::PresetSelect ? ControlEvent::Kind::PresetSelect
                                 : mapping.action == ActionType::PresetNext   ? ControlEvent::Kind::PresetNext
                                                                               : ControlEvent::Kind::PresetPrev;
                    presetEvent.source = src;
                    presetEvent.value = 0;   // a key carries no program number; the handler picks the first board
                    havePreset = true;
                }
                break;

            case ActionType::CCThreshold:
            case ActionType::SetValue:
            default:
                break;                   // both read a CC value, and a key has none
        }
    }

    return havePreset;
}

void MidiRouter::handle (const juce::MidiBuffer& midi) noexcept
{
    if (auto* state = stateFor (this))
        state->blocksProcessed.fetch_add (1, std::memory_order_acq_rel);   // releases this block's table reads (retire GC)

    if (! learning.load (std::memory_order_relaxed) && active.load (std::memory_order_acquire) == nullptr)
        return;                                  // nothing mapped and nothing to learn: the common case, free

    for (const auto metadata : midi)
    {
        const auto message = metadata.getMessage();

        MappingSource src;
        int value = 0;
        bool isOn = false;

        if (message.isNoteOn())                  // isNoteOn() is false for velocity 0
        {
            src = { SourceType::Note, (uint8_t) message.getChannel(), message.getNoteNumber() };
            value = message.getVelocity();
            isOn = true;
        }
        else if (message.isNoteOff())            // includes a note-on with velocity 0
        {
            src = { SourceType::Note, (uint8_t) message.getChannel(), message.getNoteNumber() };
            value = 0;
            isOn = false;
        }
        else if (message.isController())
        {
            src = { SourceType::CC, (uint8_t) message.getChannel(), message.getControllerNumber() };
            value = message.getControllerValue();
            isOn = value >= 64;
        }
        else if (message.isProgramChange())
        {
            src = { SourceType::ProgramChange, (uint8_t) message.getChannel(), message.getProgramChangeNumber() };
            value = src.number;
            isOn = true;
        }
        else
        {
            continue;                            // pitch bend, aftertouch and clock are not mappable
        }

        // A note-off can never be a mapping's source, so learn keeps waiting through one.
        const bool capturable = src.type != SourceType::Note || isOn;

        // Re-read every message: the first capture clears `learning`, and everything after it in the same block
        // must act normally rather than be captured too (a cached copy swallowed the rest of the block).
        if (learning.load (std::memory_order_relaxed))
        {
            if (! capturable)
                continue;                        // keep waiting, and act on nothing while learning

            learning.store (false, std::memory_order_relaxed);

            ControlEvent captured;
            captured.kind = ControlEvent::Kind::LearnCaptured;
            captured.source = src;
            captured.value = value;
            pushEvent (captured);

            continue;                            // the learned message must not also act on the old table
        }

        apply (src, isOn, value);
    }
}

void MidiRouter::apply (const MappingSource& src, bool isOn, int value) noexcept
{
    const auto* table = active.load (std::memory_order_acquire);

    if (table == nullptr)
        return;

    // Linear scan: a board has tens of mappings, not thousands, and this runs once per message.
    for (const auto& mapping : table->mappings)
    {
        if (mapping.source.type != src.type || mapping.source.number != src.number)
            continue;

        // Channel 0 in a mapping means any channel.
        if (mapping.source.channel != 0 && mapping.source.channel != src.channel)
            continue;

        const bool hasTarget = mapping.paramIndex != ParameterRegistry::kInvalid;

        switch (mapping.action)
        {
            case ActionType::Toggle:
                // Note-on or key press only: a note-off would flip it straight back.
                if (isOn && hasTarget && (src.type == SourceType::Note || src.type == SourceType::Key))
                    registry.toggle (mapping.paramIndex);
                break;

            case ActionType::Momentary:
                if (hasTarget)
                    registry.set (mapping.paramIndex, isOn ? mapping.maxValue : mapping.minValue);
                break;

            case ActionType::CCThreshold:
                if (hasTarget && src.type == SourceType::CC)
                    registry.set (mapping.paramIndex, value >= 64 ? mapping.maxValue : mapping.minValue);
                break;

            case ActionType::SetValue:
                if (hasTarget && src.type == SourceType::CC)
                {
                    const float t = (float) juce::jlimit (0, 127, value) / 127.0f;
                    registry.set (mapping.paramIndex, mapping.minValue + t * (mapping.maxValue - mapping.minValue));
                }
                break;

            case ActionType::PresetSelect:
                if (isOn)
                {
                    ControlEvent event;
                    event.kind = ControlEvent::Kind::PresetSelect;
                    event.source = src;
                    event.value = value;
                    pushEvent (event);
                }
                break;

            case ActionType::PresetNext:
                if (isOn)
                {
                    ControlEvent event;
                    event.kind = ControlEvent::Kind::PresetNext;
                    event.source = src;
                    pushEvent (event);
                }
                break;

            case ActionType::PresetPrev:
                if (isOn)
                {
                    ControlEvent event;
                    event.kind = ControlEvent::Kind::PresetPrev;
                    event.source = src;
                    pushEvent (event);
                }
                break;

            default:
                break;
        }
    }
}

void MidiRouter::pushEvent (const ControlEvent& event) noexcept
{
    const auto scope = fifo.write (1);

    if (scope.blockSize1 > 0)
        events[(size_t) scope.startIndex1] = event;
    else if (scope.blockSize2 > 0)
        events[(size_t) scope.startIndex2] = event;

    // Else the FIFO is full and the event is dropped. The audio thread never waits for the message thread.
}
} // namespace pb
