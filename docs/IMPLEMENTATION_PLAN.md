# Snaral Beta Implementation Plan (v0.2 beta)

**Status:** Active. Authoritative for module ownership, interfaces and the release procedure of the beta.
Product requirements remain in [Initial Developer Specification](SNARAL_INITIAL_DEVELOPER_SPEC.md) and the per-feature specs in `docs/specs/`.

## Scope decisions (beta)

Everything in the initial spec and the CDL baseline is core. In addition, the beta ships these chosen extensions
(each has a full spec in `docs/specs/`):

| Category | Feature | Spec |
|---|---|---|
| Effect 1 | Formant Furnace (mid-only moving formant bank) | expansion-effect-formant-furnace |
| Effect 2 | Harmonic Fracture (mid-only multiband waveshaper) | expansion-effect-harmonic-fracture |
| More usable | Undo/Redo history (100 steps, coalesced) + extended A/B | expansion-undo-ab |
| More musical | Snarl Trajectory editor (drawn body > peak > return contour, tempo-synced) | expansion-snarl-trajectory |
| More valuable | Nonlinear oversampling (Off/2x/4x) with latency reporting + expanded factory library (24+ presets) | expansion-oversampling, factory-presets |
| Easier to use | Preset browser (search, tags, favourites, audition) | expansion-preset-browser |
| Bonus (cheap, specced) | Sub Anchor range compensation | expansion-sub-anchor |

Still post-beta: MPE, Variation Morph, Motion Snapshot, Readability Monitor, Preset Exchange bundles, Spectral Shard, Clockwork Gate, Inharmonic Striker.
Performance Gesture Map stays excluded.

Formats: VST3 + Standalone (all OS), CLAP (all OS), AU (macOS), LV2 (Linux, best-effort).
Beta builds: Windows x64, macOS arm64, macOS x86_64 (Intel), Linux x64.

## Architecture contract

```
Source/Parameters/  ParameterTable.h (+ParameterList.def, Groups/*.def)  single source of truth, JUCE-free
Source/Dsp/         JUCE-free header-only DSP blocks (Adsr, Oscillators, SvfFilter, Lfo, Contour, Trajectory, Drive,
                    Texture, Width, Limiter, Oversampler, Fx/FormantFurnace, Fx/HarmonicFracture, DspUtil)
Source/Engine/      JUCE-free engine: EngineTypes.h, VoiceAllocator, Voice, SnaralEngine (+ SubAnchor)
Source/Core/        JUCE-free services (midi learn mapping logic, randomize, offline render driver, option models)
Source/State/       JUCE-core based: preset JSON, project state, variations, A/B, undo history, preset manager
Source/UI/          JUCE GUI: LookAndFeel, themes, widgets, editor views
Source/Plugin*.cpp  Processor/Editor glue (integration worker only)
tests/*Tests.cpp    JUCE-free tests (auto-registered by CMake; link Source/Core|Engine|Dsp *.cpp)
tests/juce/*Tests.cpp  JUCE-dependent tests (auto-registered; link Source/Core|State|Parameters *.cpp + juce_audio_utils)
```

Rules for every module:
- Namespace `snaral`. C++20. No allocation, locks, I/O, exceptions or `std::string` in any `process`/`render` path.
- DSP/Engine never include JUCE. Parameters reach DSP only as `ParamSnapshot` (domain units, see ParameterTable.h).
- Parameters are indexed with `snaral::P::<id>`; to add a parameter append to YOUR group `.def` file only (never edit another group; never reorder or reuse IDs).
- Every module ships boundary tests in `tests/` that run in < 10 s and assert the acceptance criteria of its spec. No test claims without running them.
- Mid-only effects must not touch the sub layer. The sub path is centered at every stage.

### Engine interface (implemented by the integration worker from the delivered DSP blocks)

```cpp
class SnaralEngine {
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParameters (const ParamSnapshot&);          // once per block, before render
    void setHostTiming (const HostTiming&);
    void handleMidi (const EngineMidiEvent&);           // pre-sorted; sample-accurate splitting is done by the caller
    void render (float* left, float* right, int numSamples);   // right may alias left for mono
    void panic();                                       // All Sound Off + clear sustain, 5 ms de-click
    int  latencySamples() const;                        // limiter + oversampling latency, host-reported
    // read-only diagnostics, lock-free: peak/RMS, scope tap (ring buffer), active voice count, cpu load
};
```

### Bus-level mid effect interface (Formant Furnace, Harmonic Fracture)

```cpp
struct XParams { static XParams from (const ParamSnapshot&); /* domain values already scaled */ };
class X {
public:
    void prepare (double sampleRate, int maxBlockSize);
    void reset();
    void setParams (const XParams&);
    void process (float* io, int numSamples, const MidModulation&);   // mono mid bus, in place
    int  latencySamples() const;
};
```

Mid chain per spec: per voice `source A/B blend + noise + metal -> SVF filter (contour/LFO/trajectory) -> mid amp ADSR`, summed to the
mid bus, then `Furnace -> Fracture -> Drive (oversampled when selected) -> Width`, then summed with the centered sub
(`sub osc -> sub amp ADSR -> sub anchor gain`), `output trim -> limiter`.

## Waves and ownership

| Wave | Worker | Owns (exclusive) |
|---|---|---|
| 1 | engine-voice | Source/Dsp/Adsr.h, Source/Engine/VoiceAllocator.*, tuning/MIDI logic + tests |
| 1 | engine-synth | Source/Dsp/Oscillators.h, SvfFilter.h, Lfo.h, Contour.h, Trajectory.h + tests |
| 1 | engine-output | Source/Dsp/Drive.h, Texture.h, Width.h, Limiter.h, Oversampler.h + tests |
| 1 | engine-fx | Source/Dsp/Fx/* + tests |
| 1 | state | Source/State/*, Resources/presets/*.json, tests/juce/* for state |
| 1 | ui-kit | Source/UI/* (LookAndFeel, themes, widgets) |
| 1 | specs | docs/** only |
| 1 | release-eng | .github/workflows/*, tools/package*, docs/release/* |
| 2 | engine-integration | Source/Engine/SnaralEngine.*, Voice, SubAnchor, PluginProcessor.* |
| 2 | cdl-services | Source/Core/*, WAV export, MIDI learn, randomize, standalone reference tone |
| 3 | editor | Source/PluginEditor.*, Source/UI/Views/* |
| 4 | audit, qa, release | verification evidence |

## Release procedure (beta)

1. Merge all branches into `work/beta`; CI must be green on Linux, macOS (arm64 + Intel cross-build) and Windows.
2. Full audit (spec-by-spec wiring checklist in `docs/release/AUDIT.md`), light QA (`docs/release/QA_REPORT.md`), fix bugs.
3. Tag `v0.2.0-beta.N`; the release workflow publishes a GitHub **pre-release** with per-platform zips.
4. Record only measurements that were actually run.
