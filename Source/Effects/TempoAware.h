#pragma once
// Tempo is not part of IEffect (a scaffold interface, not ours to change): only time-based pedals care, so
// they opt in here instead. The board hands its tempo to whichever pedals implement this.

namespace pb
{
class ITempoAware
{
public:
    virtual ~ITempoAware() = default;

    // AUDIO THREAD. Called before the pedal processes the block, and only when the tempo has changed.
    virtual void setTempo (double beatsPerMinute) noexcept = 0;
};
} // namespace pb
