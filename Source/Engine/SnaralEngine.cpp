#include "Engine/SnaralEngine.h"

#include "Dsp/Drive.h"
#include "Dsp/Fx/FormantFurnace.h"
#include "Dsp/Fx/HarmonicFracture.h"
#include "Dsp/Limiter.h"
#include "Dsp/Oversampler.h"
#include "Dsp/SettlingSmoother.h"
#include "Dsp/Width.h"
#include "Engine/Voice.h"
#include "Engine/VoiceAllocator.h"
#include "Parameters/MacroMappings.h"

#include <array>
#include <chrono>
#include <cstring>
#include <vector>

namespace snaral
{
static_assert (SnaralEngine::maxVoices == VoiceAllocator::maxVoices);

namespace
{
constexpr int N = engineControlBlock;

/// Integer delay for the sub layer with a click-free change of delay (cross-fade between the old and the new tap).
/// Delay 0 and no fade in progress is an exact pass-through.
class SubAlignDelay
{
public:
    static constexpr int fadeSamples = 128;

    void prepare (int maxDelay)
    {
        int size = 1;
        while (size < maxDelay + 2) size <<= 1;
        buf_.assign (static_cast<std::size_t> (size), 0.0f);
        mask_ = size - 1;
        maxDelay_ = maxDelay;
        reset();
    }
    void reset() noexcept
    {
        std::fill (buf_.begin(), buf_.end(), 0.0f);
        pos_ = 0;
        fadeLeft_ = 0;
        oldDelay_ = delay_;
    }
    void clearHistory() noexcept { std::fill (buf_.begin(), buf_.end(), 0.0f); }
    void setDelay (int d) noexcept
    {
        d = std::clamp (d, 0, maxDelay_);
        if (d == delay_)
            return;
        oldDelay_ = delay_;
        delay_ = d;
        fadeLeft_ = fadeSamples;
    }
    /// Start-up / reset: adopt the delay without a cross-fade.
    void snapDelay (int d) noexcept
    {
        delay_ = oldDelay_ = std::clamp (d, 0, maxDelay_);
        fadeLeft_ = 0;
    }
    int delay() const noexcept { return delay_; }
    float process (float x) noexcept
    {
        buf_[static_cast<std::size_t> (pos_)] = x;
        float y = buf_[static_cast<std::size_t> ((pos_ - delay_) & mask_)];
        if (fadeLeft_ > 0)
        {
            const float old = buf_[static_cast<std::size_t> ((pos_ - oldDelay_) & mask_)];
            y += (old - y) * (static_cast<float> (fadeLeft_) / static_cast<float> (fadeSamples));
            --fadeLeft_;
        }
        pos_ = (pos_ + 1) & mask_;
        return y;
    }

private:
    std::vector<float> buf_;
    int mask_ = 0, pos_ = 0, delay_ = 0, oldDelay_ = 0, maxDelay_ = 0, fadeLeft_ = 0;
};

int limiterLatencyFor (double sampleRate) noexcept
{
    const double sr = sampleRate >= 8000.0 ? sampleRate : 48000.0;
    return std::max (2, static_cast<int> (std::ceil (Limiter::lookaheadMs * 0.001 * sr))) + 2;
}

/// Mid-chain latency: HarmonicFracture's oversampled shaper and Drive's oversampled shaper are in series, so the mid path
/// carries two oversampler group delays when oversampling is on (and none when it is off).
int midLatencyFor (OversamplingMode mode) noexcept
{
    return mode == OversamplingMode::Off ? 0 : 2 * Oversampler::latencyFor (mode);
}
} // namespace

struct SnaralEngine::Impl
{
    // ---- configuration ----
    double sr = 48000.0;
    bool prepared = false;
    bool needSnap = true;
    ParamSnapshot snap;
    VoiceParams vp;
    VoiceAllocator::Config allocCfg;

    // ---- voices ----
    std::array<Voice, maxVoices> voices;
    VoiceAllocator alloc;
    VoiceCommandList cmds;
    SharedLfo sharedLfo;

    // ---- smoothed global controls (10 ms unless noted) ----
    SettlingSmoother sBend, sSubGain, sMidGain, sCutoff, sContourSt, sLfoSt, sTrajSt, sPos, sContourPos, sLfoPos,
        sTrajPos, sRes;
    // bypass: linear ramp, exactly 10 ms from full level to silence (and back)
    float bypassGain = 1.0f, bypassTarget = 1.0f, bypassStep = 0.001f;
    float aBend[N] {}, aSubGain[N] {}, aMidGain[N] {}, aCutoff[N] {}, aContourSt[N] {}, aLfoSt[N] {}, aTrajSt[N] {},
        aPos[N] {}, aContourPos[N] {}, aLfoPos[N] {}, aTrajPos[N] {}, aShared[N] {};

    // ---- mid chain ----
    FormantFurnace furnace;
    HarmonicFracture fracture;
    Drive drive;
    Width width;
    Oversampler os;                 // shared by Drive
    std::array<Oversampler, 3> osFr; // one per Fracture band (filter state must not be shared)
    OversamplingMode osMode = OversamplingMode::Off;
    int midLatency = 0;

    // ---- output ----
    SubAlignDelay subDelay;
    OutputTrim trim;
    Limiter limiter;
    float maxCutoffHz = 20000.0f;

    // ---- panic ----
    int panicLeft = 0, panicTotal = 1;
    bool flushPending = false;

    // ---- host timing ----
    double lastBpm = 120.0;
    bool haveBpm = false, tempoNowValid = false;
    HostTiming lastHost;
    bool lfoSync = false, trajSync = false;

    // ---- diagnostics ----
    EngineDiagnostics diag;
    int activeVoices = 0;
    double winEnergyL = 0.0, winEnergyR = 0.0;
    float winPeakL = 0.0f, winPeakR = 0.0f;
    int winSamples = 0;
    double cpuAccumSec = 0.0;
    std::int64_t cpuAccumSamples = 0;
    float cpuSmoothed = 0.0f;

    // =====================================================================================================
    void prepare (double sampleRate)
    {
        sr = sampleRate >= 8000.0 && std::isfinite (sampleRate) ? sampleRate : 48000.0;
        maxCutoffHz = std::min (20000.0f, static_cast<float> (0.45 * sr));
        for (int v = 0; v < maxVoices; ++v)
            voices[static_cast<std::size_t> (v)].prepare (sr, 0x2545F491u + 7919u * static_cast<std::uint32_t> (v));
        sharedLfo.prepare (sr);

        for (auto* s : { &sSubGain, &sMidGain, &sCutoff, &sContourSt, &sLfoSt, &sTrajSt, &sPos, &sContourPos, &sLfoPos,
                         &sTrajPos, &sRes })
            s->prepare (sr, 10.0f);
        bypassStep = static_cast<float> (1.0 / (0.010 * sr));
        sBend.prepare (sr, bendSmoothMs);

        furnace.prepare (sr, N);
        fracture.prepare (sr, N);
        drive.prepare (sr, N);
        width.prepare (sr, N);
        os = Oversampler(); // fresh instances: no stale mode / hold values survive a re-prepare
        for (auto& o : osFr) o = Oversampler();
        os.prepare (sr, N);
        for (auto& o : osFr) o.prepare (sr, N);
        subDelay.prepare (2 * Oversampler::latencyFor (OversamplingMode::X4) + 8);
        trim.prepare (sr);
        limiter.prepare (sr, N);

        panicTotal = std::max (1, VoiceAllocator::declickSamples (sr));
        prepared = true;
        osMode = OversamplingMode::Off;
        applyOversampling (OversamplingMode::Off, true);
        setParameters (snap);
        resetAll();
    }

    void resetAll()
    {
        for (auto& v : voices) v.reset();
        alloc = VoiceAllocator();
        cmds.clear();
        alloc.setConfig (allocCfg, cmds);
        cmds.clear();
        sharedLfo.reset();
        furnace.reset();
        fracture.reset();
        drive.reset();
        width.reset();
        os.reset();
        for (auto& o : osFr) o.reset();
        subDelay.reset();
        trim.reset();
        limiter.reset();
        panicLeft = 0;
        flushPending = false;
        activeVoices = 0;
        needSnap = true; // next setParameters() snaps every smoother
        snapSmoothers();
        diag.publishActiveVoices (0);
        diag.clearScope();
        winEnergyL = winEnergyR = 0.0;
        winPeakL = winPeakR = 0.0f;
        winSamples = 0;
    }

    void snapSmoothers() noexcept
    {
        for (auto* s : { &sBend, &sSubGain, &sMidGain, &sCutoff, &sContourSt, &sLfoSt, &sTrajSt, &sPos, &sContourPos,
                         &sLfoPos, &sTrajPos, &sRes })
            s->reset (s->target());
        bypassGain = bypassTarget;
    }

    // ---- oversampling / latency ----
    void applyOversampling (OversamplingMode m, bool force = false)
    {
        if (m == osMode && ! force)
            return;
        osMode = m;
        os.setMode (m);
        for (auto& o : osFr) o.setMode (m);
        const int lat = m == OversamplingMode::Off ? 0 : Oversampler::latencyFor (m);
        fracture.setShaperLatency (lat);
        midLatency = midLatencyFor (m);
        subDelay.setDelay (midLatency);
        diag.publishLatency (limiter.latencySamples() + midLatency);
    }

    // ---- parameters ----
    void setParameters (const ParamSnapshot& in)
    {
        for (int i = 0; i < parameterCount; ++i)
            snap.v[static_cast<std::size_t> (i)] = parameterTable[static_cast<std::size_t> (i)].clampToDomain (in.v[static_cast<std::size_t> (i)]);
        if (! prepared)
            return;

        allocCfg = { snap.choice (P::voice_mode) == 1, snap.flag (P::legato) };
        cmds.clear();
        alloc.setConfig (allocCfg, cmds);
        applyCommands();

        vp = VoiceParams::from (snap);
        for (auto& v : voices) v.setParams (vp);
        sharedLfo.setParams (vp.lfo);
        lfoSync = vp.lfo.sync;
        trajSync = snap.flag (P::traj_enabled) && snap.choice (P::traj_time_mode) == 1;

        // ---- macros and layer audibility ----
        const bool soloSub = snap.choice (P::layer_solo) == 1, soloMid = snap.choice (P::layer_solo) == 2;
        const bool subOn = soloSub || (! soloMid && ! snap.flag (P::sub_mute));
        const bool midOn = soloMid || (! soloSub && ! snap.flag (P::mid_mute));
        const float weightMul = weightSubMultiplier (snap.percent (P::weight));
        const float growlMul = growlMidMultiplier (snap.percent (P::growl));
        sSubGain.setTarget (subOn ? weightMul * snap.percent (P::sub_level) : 0.0f);
        sMidGain.setTarget (midOn ? growlMul * snap.percent (P::mid_level) : 0.0f);
        bypassTarget = snap.flag (P::bypass) ? 0.0f : 1.0f;

        updateBendTarget();

        // ---- filter / motion routing. Bite's filter offset does NOT depend on Motion; Motion scales only the
        // contour, LFO and trajectory depths (and the Furnace modulation amounts below). ----
        const float motion = motionDepthMultiplier (snap.percent (P::motion));
        const float bitePeak = biteFilterPeakOffsetSemitones (snap.percent (P::bite));
        const float contourDepth = clampf (snap.percent (P::contour_depth), 0.0f, 1.0f);
        const float lfoDepth = clampf (snap.percent (P::lfo_depth), 0.0f, 1.0f);
        const float trajDepth = clampf (snap.percent (P::traj_depth), 0.0f, 1.0f);
        const bool trajOn = snap.flag (P::traj_enabled);
        const int dest = snap.choice (P::traj_dest);
        const bool trajCutoff = trajOn && (dest == 0 || dest == 2);
        const bool trajPosition = trajOn && (dest == 1 || dest == 2);

        sCutoff.setTarget (std::log2 (clampf (snap[P::filter_cutoff_hz], 20.0f, 20000.0f)));
        sRes.setTarget (clampf (snap[P::filter_resonance], 0.0f, SvfFilter::maxResonancePercent));
        sContourSt.setTarget (trajCutoff ? 0.0f : motion * contourDepth * contourCutoffRangeSemitones + bitePeak);
        sTrajSt.setTarget (trajCutoff ? motion * trajDepth * trajCutoffRangeSemitones + bitePeak : 0.0f);
        sLfoSt.setTarget (motion * lfoDepth * lfoCutoffRangeSemitones);
        sPos.setTarget (clampf (snap.percent (P::mid_table_position), 0.0f, 1.0f));
        sContourPos.setTarget (trajPosition ? 0.0f : motion * contourDepth * contourPositionRange);
        sTrajPos.setTarget (trajPosition ? motion * trajDepth * trajPositionRange : 0.0f);
        sLfoPos.setTarget (motion * lfoDepth * lfoPositionRange);

        // ---- mid-bus effects ----
        // FormantFurnaceParams::from already applies the Motion multiplier to the modulation depths.
        furnace.setParams (FormantFurnaceParams::from (snap));
        fracture.setParams (HarmonicFractureParams::from (snap));
        drive.setParams (DriveParams::from (snap));
        width.setParams (WidthParams::from (snap));
        trim.setTrimDb (snap[P::output_trim_db]);
        limiter.setParams (LimiterParams::from (snap));
        applyOversampling (static_cast<OversamplingMode> (std::clamp (snap.choice (P::oversampling_mode), 0, 2)));

        if (needSnap)
        {
            snapSmoothers();
            furnace.reset();
            fracture.reset();
            drive.reset();
            width.reset();
            trim.reset();
            subDelay.snapDelay (midLatency);
            needSnap = false;
        }
        publishTempo();
    }

    void updateBendTarget() noexcept
    {
        sBend.setTarget (static_cast<float> (bendSemitones (alloc.bend14(), snap[P::pitch_bend_range])));
    }

    // ---- host timing ----
    void setHostTiming (const HostTiming& t)
    {
        tempoNowValid = t.bpmValid && std::isfinite (t.bpm) && t.bpm >= 20.0 && t.bpm <= 999.0;
        if (tempoNowValid)
        {
            lastBpm = t.bpm;
            haveBpm = true;
        }
        HostTiming eff;
        eff.bpm = lastBpm;
        eff.bpmValid = haveBpm;
        eff.playing = t.playing;
        eff.ppqValid = t.ppqValid;
        eff.ppqPosition = t.ppqPosition;
        lastHost = eff;
        for (auto& v : voices) v.setHostTiming (eff);
        sharedLfo.setTiming (lastBpm, haveBpm, tempoNowValid && t.playing, t.ppqValid, t.ppqPosition);
        publishTempo();
    }

    void publishTempo() noexcept
    {
        diag.publishTempo (! tempoNowValid, (lfoSync || trajSync) && ! haveBpm);
    }

    // ---- MIDI / voice commands ----
    void applyCommands() noexcept
    {
        for (const auto& c : cmds)
        {
            auto& v = voices[c.voice];
            switch (c.type)
            {
                case VoiceCommand::Type::Start: v.start (c); break;
                case VoiceCommand::Type::Retarget: v.retarget (c); break;
                case VoiceCommand::Type::Release: v.release(); break;
                case VoiceCommand::Type::Kill: v.kill (panicTotal); break;
            }
        }
        if (cmds.dropped > 0)
        {
            diag.addDroppedCommands (static_cast<std::uint32_t> (cmds.dropped));
            cmds.dropped = 0;
        }
        if (cmds.declick)
            startPanicDuck();
        cmds.clear();
    }

    void startPanicDuck() noexcept
    {
        if (panicLeft == 0)
            panicLeft = panicTotal; // a second panic during the ramp keeps the running ramp (gain never jumps back up)
        flushPending = true;
    }

    void handleMidi (const EngineMidiEvent& e)
    {
        if (! prepared)
            return;
        cmds.clear();
        alloc.handle (e, cmds);
        applyCommands();
        if (e.type == EngineMidiEvent::Type::PitchBend)
            updateBendTarget();
    }

    void panic()
    {
        if (! prepared)
            return;
        cmds.clear();
        alloc.panic (cmds);
        cmds.declick = true; // always duck + flush: panic also clears effect tails
        applyCommands();
    }

    // ---- rendering ----
    void flushEffects() noexcept
    {
        furnace.reset();
        fracture.reset();
        drive.reset();
        width.reset();
        os.reset();
        for (auto& o : osFr) o.reset();
        subDelay.clearHistory();
    }

    void renderBlock (int m, float* L, float* R) noexcept
    {
        for (int i = 0; i < m; ++i)
        {
            aBend[i] = sBend.next();
            aSubGain[i] = sSubGain.next();
            aMidGain[i] = sMidGain.next();
            aCutoff[i] = sCutoff.next();
            aContourSt[i] = sContourSt.next();
            aLfoSt[i] = sLfoSt.next();
            aTrajSt[i] = sTrajSt.next();
            aPos[i] = sPos.next();
            aContourPos[i] = sContourPos.next();
            aLfoPos[i] = sLfoPos.next();
            aTrajPos[i] = sTrajPos.next();
            aShared[i] = sharedLfo.process();
        }
        const float res = sRes.next();

        float midBus[N] = {}, subBus[N] = {};
        VoiceBlockInput in;
        in.n = m;
        in.bendSt = aBend;
        in.subGain = aSubGain;
        in.midGain = aMidGain;
        in.cutoffLog2 = aCutoff;
        in.contourSt = aContourSt;
        in.lfoSt = aLfoSt;
        in.trajSt = aTrajSt;
        in.position = aPos;
        in.contourPos = aContourPos;
        in.lfoPos = aLfoPos;
        in.trajPos = aTrajPos;
        in.sharedLfo = aShared;
        in.resonancePercent = res;
        in.maxCutoffHz = maxCutoffHz;

        VoiceModOut mod;
        int active = 0;
        for (int v = 0; v < maxVoices; ++v)
        {
            auto& voice = voices[static_cast<std::size_t> (v)];
            if (! voice.active())
                continue;
            ++active;
            if (voice.render (in, midBus, subBus, mod))
                alloc.voiceFinished (v);
        }
        activeVoices = active;

        // ---- mid chain: Furnace -> Fracture -> Drive (oversampled when selected) -> Width ----
        MidModulation mm;
        if (mod.count > 0)
        {
            mm.contour = mod.contourSum / static_cast<float> (mod.count);
            mm.lfo = mod.lfoSum / static_cast<float> (mod.count);
        }
        furnace.process (midBus, m, mm);
        fracture.processWith (midBus, m, mm,
                              [this, m] (int band, const float* src, float* dst, int count, const FractureShapeControl& c)
                              {
                                  std::memcpy (dst, src, static_cast<std::size_t> (count) * sizeof (float));
                                  osFr[static_cast<std::size_t> (band)].process (dst, count,
                                                                                 [&c, band, count] (float* buf, int hiCount)
                                                                                 {
                                                                                     const int f = hiCount / count;
                                                                                     int k = 0;
                                                                                     for (int i = 0; i < count; ++i)
                                                                                         for (int j = 0; j < f; ++j, ++k)
                                                                                             buf[k] = HarmonicFracture::shapeSample (band, buf[k], c.gain[i], c.amount[i], c.bias[i]);
                                                                                 });
                              });
        drive.process (midBus, m, &os);
        width.process (midBus, L, R, m);

        // ---- sub: aligned with the mid chain, then added identically to L and R (centred, after Width) ----
        for (int i = 0; i < m; ++i)
        {
            const float s = subDelay.process (subBus[i]);
            L[i] += s;
            R[i] += s;
        }

        // ---- panic duck (effect tails), output trim, limiter, bypass fade ----
        if (panicLeft > 0 || flushPending)
        {
            for (int i = 0; i < m; ++i)
            {
                float g = 0.0f; // after the ramp the output stays muted until the block ends and the effects are flushed
                if (panicLeft > 0)
                {
                    g = static_cast<float> (panicLeft - 1) / static_cast<float> (panicTotal);
                    --panicLeft;
                }
                L[i] *= g;
                R[i] *= g;
            }
        }
        if (flushPending && panicLeft == 0)
        {
            flushPending = false;
            flushEffects();
        }
        trim.process (L, R, m);
        limiter.process (L, R, m);
        for (int i = 0; i < m; ++i)
        {
            if (bypassGain != bypassTarget)
            {
                const float d = bypassTarget - bypassGain;
                bypassGain = std::fabs (d) <= bypassStep ? bypassTarget : bypassGain + (d > 0.0f ? bypassStep : -bypassStep);
            }
            const float b = bypassGain;
            L[i] = sanitize (L[i] * b);
            R[i] = sanitize (R[i] * b);
        }
    }

    void render (float* left, float* right, int n)
    {
        if (left == nullptr || n <= 0)
            return;
        const bool mono = right == nullptr || right == left;
        if (! prepared)
        {
            std::fill (left, left + n, 0.0f);
            if (! mono) std::fill (right, right + n, 0.0f);
            return;
        }
        const auto t0 = std::chrono::steady_clock::now();
        int done = 0;
        while (done < n)
        {
            const int m = std::min (N, n - done);
            float L[N], R[N];
            renderBlock (m, L, R);
            for (int i = 0; i < m; ++i)
            {
                const float l = L[i], r = R[i];
                winPeakL = std::max (winPeakL, std::fabs (l));
                winPeakR = std::max (winPeakR, std::fabs (r));
                winEnergyL += static_cast<double> (l) * l;
                winEnergyR += static_cast<double> (r) * r;
                diag.pushScope (0.5f * (l + r));
                if (mono) left[done + i] = 0.5f * (l + r);
                else { left[done + i] = l; right[done + i] = r; }
            }
            winSamples += m;
            done += m;
            if (winSamples >= 512)
            {
                const double inv = 1.0 / winSamples;
                diag.publishLevels (winPeakL, winPeakR, static_cast<float> (std::sqrt (winEnergyL * inv)),
                                    static_cast<float> (std::sqrt (winEnergyR * inv)));
                winPeakL = winPeakR = 0.0f;
                winEnergyL = winEnergyR = 0.0;
                winSamples = 0;
            }
        }
        diag.publishActiveVoices (activeVoices);
        diag.publishLimiterReductionDb (limiter.currentReductionDb());

        const auto t1 = std::chrono::steady_clock::now();
        cpuAccumSec += std::chrono::duration<double> (t1 - t0).count();
        cpuAccumSamples += n;
        if (cpuAccumSamples >= 2048)
        {
            const float load = static_cast<float> (cpuAccumSec / (static_cast<double> (cpuAccumSamples) / sr));
            cpuSmoothed = cpuSmoothed == 0.0f ? load : cpuSmoothed + 0.25f * (load - cpuSmoothed);
            diag.publishCpuLoad (cpuSmoothed);
            cpuAccumSec = 0.0;
            cpuAccumSamples = 0;
        }
    }
};

// =========================================================================================================
SnaralEngine::SnaralEngine() : impl_ (std::make_unique<Impl>()) {}
SnaralEngine::~SnaralEngine() = default;

void SnaralEngine::prepare (double sampleRate, int /*maxBlockSize*/) { impl_->prepare (sampleRate); }
void SnaralEngine::reset() { if (impl_->prepared) impl_->resetAll(); }
void SnaralEngine::setParameters (const ParamSnapshot& s) { impl_->setParameters (s); }
void SnaralEngine::setHostTiming (const HostTiming& t) { impl_->setHostTiming (t); }
void SnaralEngine::handleMidi (const EngineMidiEvent& e) { impl_->handleMidi (e); }
void SnaralEngine::render (float* left, float* right, int numSamples) { impl_->render (left, right, numSamples); }
void SnaralEngine::panic() { impl_->panic(); }

int SnaralEngine::latencySamples() const noexcept
{
    return impl_->prepared ? impl_->limiter.latencySamples() + impl_->midLatency : 0;
}

int SnaralEngine::latencyFor (OversamplingMode mode, double sampleRate) noexcept
{
    return limiterLatencyFor (sampleRate) + midLatencyFor (mode);
}

int SnaralEngine::latencyForModeIndex (int choice, double sampleRate) noexcept
{
    return latencyFor (static_cast<OversamplingMode> (std::clamp (choice, 0, 2)), sampleRate);
}

int SnaralEngine::activeVoiceCount() const noexcept { return impl_->activeVoices; }
double SnaralEngine::sampleRate() const noexcept { return impl_->sr; }
const EngineDiagnostics& SnaralEngine::diagnostics() const noexcept { return impl_->diag; }
EngineDiagnostics& SnaralEngine::diagnostics() noexcept { return impl_->diag; }
} // namespace snaral
