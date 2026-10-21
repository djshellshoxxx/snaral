#pragma once

// Adapts the lock-free EngineDiagnostics of the processor to the UI kit's read-only display sources.
// The engine publishes a post-limiter mono output tap plus pre-trim Sub and Mid layer taps for the scope's diagnosis views.

#include "Engine/EngineDiagnostics.h"
#include "UI/UiSources.h"

namespace snaral::ui
{
class DiagnosticsSources final : public ScopeSource, public MeterSource, public CpuSource
{
public:
    DiagnosticsSources (const EngineDiagnostics& d, std::function<double()> sampleRate)
        : diag (d), sampleRateFn (std::move (sampleRate)) {}

    // ScopeSource
    bool hasTap (ScopeTap) const noexcept override { return true; }
    int pullLatest (ScopeTap tap, int, float* dest, int numSamples) noexcept override
    {
        const auto n = static_cast<std::size_t> (juce::jmax (0, numSamples));
        switch (tap)
        {
            case ScopeTap::Sub: return static_cast<int> (diag.copyLatestLayer (0, dest, n));
            case ScopeTap::Mid: return static_cast<int> (diag.copyLatestLayer (1, dest, n));
            case ScopeTap::Output: break;
        }
        return static_cast<int> (diag.copyLatest (dest, n));
    }
    double getSampleRate() const noexcept override
    {
        const double sr = sampleRateFn ? sampleRateFn() : 0.0;
        return sr > 1.0 ? sr : 48000.0;
    }

    // MeterSource: the engine publishes per-window peaks; the kit meter adds ballistics and the clip latch.
    MeterReading readAndReset() noexcept override
    {
        MeterReading m;
        m.valid = diag.scopeWritten() > 0;
        for (int c = 0; c < 2; ++c)
        {
            m.peak[c] = diag.peak (c);
            m.rms[c] = diag.rms (c);
        }
        return m;
    }

    // CpuSource: render time / audio time of the Snaral engine only (never host-wide CPU).
    CpuReading read() noexcept override
    {
        CpuReading r;
        r.valid = diag.scopeWritten() > 0;
        r.load = diag.cpuLoad();
        const auto count = diag.overruns();   // a measured over-deadline render call, not the smoothed load
        r.overrunSinceLastRead = count != seenOverruns;
        seenOverruns = count;
        return r;
    }

private:
    const EngineDiagnostics& diag;
    std::function<double()> sampleRateFn;
    std::uint32_t seenOverruns = 0;
};
} // namespace snaral::ui
