#pragma once

// SnaralEngine: the complete JUCE-free synth engine. Owner: engine-integration.
//
// Signal flow (docs/IMPLEMENTATION_PLAN.md, macro-mapping.md, sub-mid-engine.md):
//
//   per voice  sub:  SubOscillator -> sub Adsr -> Sub Anchor gain --------------------------------.
//   per voice  mid:  MidOscillator A/B blend (+ Noise/Metal) -> SvfFilter (Contour/LFO/Trajectory,   |
//                    Bite offset independent of Motion) -> mid Adsr                                  |
//   mid bus (mono):  FormantFurnace -> HarmonicFracture -> Drive (inside the shared Oversampler       |
//                    when oversampling_mode != off) -> Width (Space)  -> L, R                        |
//   sub bus  (mono): aligned with the mid path (delay = mid-chain latency) -- added identically to L, R <'
//   L, R -> output trim -> limiter -> bypass fade -> output.
//
// Threading: every method except diagnostics() accessors is called from ONE thread (the audio thread; prepare() / reset()
// from the thread that owns the engine while audio is stopped). Diagnostics are lock-free for another reader thread.
//
// Block handling: render() accepts any n >= 0. Internally the engine works in control blocks of at most
// engineControlBlock (32) samples, so its output does not depend on how the caller slices the audio, and all internal
// buffers are sized in prepare() (maxBlockSize is only advisory). Real-time safe: no allocation, locks or I/O in
// render()/handleMidi()/setParameters()/panic().
//
// Sample-accurate MIDI: the caller renders up to an event's offset, calls handleMidi(), and renders on.
// handleMidi() ignores EngineMidiEvent::sampleOffset.

#include "Engine/EngineDiagnostics.h"
#include "Engine/EngineTypes.h"
#include "Parameters/ParameterTable.h"

#include <memory>

namespace snaral
{
enum class OversamplingMode : int;

class SnaralEngine
{
public:
    static constexpr int maxVoices = 8;

    SnaralEngine();
    ~SnaralEngine();
    SnaralEngine (const SnaralEngine&) = delete;
    SnaralEngine& operator= (const SnaralEngine&) = delete;

    /// Allocates everything (voices, oversamplers, wavetables). Clears all voices and effect state. Parameters are kept.
    void prepare (double sampleRate, int maxBlockSize);

    /// Clears voices, held notes, pedal, effect tails and the limiter. Keeps the current parameters (no ramp afterwards).
    void reset();

    /// Once per host block, before render(). Values are sanitised and clamped to the registry. The first call after
    /// prepare()/reset() snaps all smoothers to the new values (no start-up ramp).
    void setParameters (const ParamSnapshot&);

    /// Once per host block. Missing/invalid tempo keeps the last valid tempo for sync timing, raises
    /// diagnostics().tempoUnavailable() and, if no tempo has ever been seen, falls back to the free LFO rate / ms length.
    void setHostTiming (const HostTiming&);

    /// Applies one pre-sorted event at the current position.
    void handleMidi (const EngineMidiEvent&);

    /// Renders n samples. right may be nullptr or alias left (mono output: (L+R)/2 is written to left).
    void render (float* left, float* right, int numSamples);

    /// All Sound Off + clear sustain; voices ramp to silence in 5 ms, mid-bus effect tails are ducked and flushed.
    void panic();

    /// Limiter look-ahead + oversampling latency of the mid chain (the sub layer is delayed to match).
    /// Reflects the oversampling mode requested by the last setParameters().
    int latencySamples() const noexcept;

    /// Latency for a mode at a sample rate without needing an engine instance (host-thread safe, pure).
    static int latencyFor (OversamplingMode mode, double sampleRate) noexcept;
    static int latencyForModeIndex (int oversamplingModeChoice, double sampleRate) noexcept;

    int activeVoiceCount() const noexcept;
    double sampleRate() const noexcept;

    /// Lock-free diagnostics (peak/RMS, scope tap, voices, CPU load, tempo flags, counters).
    const EngineDiagnostics& diagnostics() const noexcept;
    EngineDiagnostics& diagnostics() noexcept;

private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
} // namespace snaral
