#include "TestSupport.h"

#include <cmath>
#include <cstring>
#include <fstream>
#include <iostream>
#include <vector>

#include "Core/ExportPhrases.h"
#include "Core/OfflineRenderer.h"
#include "Core/WavWriter.h"

using namespace snaral;
using namespace snaral::render;
using namespace snaral::testing;
namespace fs = std::filesystem;

namespace
{
// ---------------------------------------------------------------- stub engine (follows the SnaralEngine interface)
class StubEngine
{
public:
    int prepareCalls = 0;
    int latency = 64;
    bool throwOnRender = false;
    int renderCalls = 0;
    int maxRenderSize = 0;
    int midiEventsSeen = 0;
    float lastWeight = -1.0f;

    void prepare (double sr, int maxBlock) { sr_ = sr; maxBlock_ = maxBlock; ++prepareCalls; }
    void reset() { for (auto& v : on_) v = 0; delay_.assign (static_cast<std::size_t> (latency), 0.0f); delayPos_ = 0; t_ = 0; }
    void setParameters (const ParamSnapshot& p) { lastWeight = p[P::weight]; gain_ = 0.5f * p[P::weight] / 100.0f; }
    void setHostTiming (const HostTiming& h) { timing_ = h; }
    void handleMidi (const EngineMidiEvent& e)
    {
        ++midiEventsSeen;
        if (e.sampleOffset != 0) std::abort(); // the renderer splits blocks; offsets are always 0
        if (e.type == EngineMidiEvent::Type::NoteOn) { on_[e.data1] = e.data2; }
        else if (e.type == EngineMidiEvent::Type::NoteOff) { on_[e.data1] = 0; }
    }
    void render (float* l, float* r, int n)
    {
        if (n > maxBlock_) std::abort();
        ++renderCalls; maxRenderSize = std::max (maxRenderSize, n);
        if (throwOnRender && renderCalls > 3) throw std::runtime_error ("boom");
        for (int i = 0; i < n; ++i, ++t_)
        {
            float s = 0.0f;
            for (int k = 0; k < 128; ++k)
                if (on_[k])
                {
                    const double f = 440.0 * std::pow (2.0, (k - 69) / 12.0);
                    s += gain_ * static_cast<float> (on_[k]) / 127.0f * static_cast<float> (std::cos (2.0 * 3.14159265358979 * f * static_cast<double> (t_) / sr_));
                }
            float out = s;
            if (latency > 0) { out = delay_[delayPos_]; delay_[delayPos_] = s; delayPos_ = (delayPos_ + 1) % delay_.size(); }
            l[i] = out;
            r[i] = out * 0.5f + 0.01f * out * out; // different from l so stereo/mono are distinguishable
        }
    }
    void panic() { for (auto& v : on_) v = 0; }
    int latencySamples() const { return latency; }
    HostTiming timing_;

private:
    double sr_ = 48000; int maxBlock_ = 512; float gain_ = 0.0f;
    int on_[128] = {};
    std::vector<float> delay_; std::size_t delayPos_ = 0; long long t_ = 0;
};

// ---------------------------------------------------------------- independent minimal WAV reader (test only)
struct WavFile
{
    bool ok = false;
    std::string error;
    int tag = 0, channels = 0, rate = 0, bits = 0, blockAlign = 0;
    std::uint32_t riffSize = 0, dataSize = 0, factFrames = 0;
    bool hasFact = false;
    std::uint64_t fileSize = 0;
    std::vector<std::string> chunkOrder;
    std::string info;
    std::vector<std::vector<double>> ch; // normalised: int / 2^(b-1), float as is
    std::vector<std::vector<std::int32_t>> raw;
    std::uint64_t frames() const { return ch.empty() ? 0 : ch[0].size(); }
};

std::uint32_t rd32 (const unsigned char* p) { return p[0] | (p[1] << 8) | (p[2] << 16) | (static_cast<std::uint32_t> (p[3]) << 24); }
std::uint32_t rd16 (const unsigned char* p) { return p[0] | (p[1] << 8); }

WavFile readWav (const fs::path& path)
{
    WavFile w;
    std::ifstream f (path, std::ios::binary);
    if (! f) { w.error = "cannot open"; return w; }
    std::vector<unsigned char> b ((std::istreambuf_iterator<char> (f)), std::istreambuf_iterator<char>());
    w.fileSize = b.size();
    if (b.size() < 12 || std::memcmp (b.data(), "RIFF", 4) != 0 || std::memcmp (b.data() + 8, "WAVE", 4) != 0) { w.error = "no RIFF/WAVE"; return w; }
    w.riffSize = rd32 (b.data() + 4);
    if (w.riffSize + 8 != b.size()) { w.error = "RIFF size mismatch"; return w; }
    std::size_t pos = 12;
    const unsigned char* data = nullptr;
    while (pos + 8 <= b.size())
    {
        const std::string id (reinterpret_cast<const char*> (b.data() + pos), 4);
        const std::uint32_t len = rd32 (b.data() + pos + 4);
        if (pos + 8 + len > b.size()) { w.error = "chunk overruns file"; return w; }
        const unsigned char* body = b.data() + pos + 8;
        w.chunkOrder.push_back (id);
        if (id == "fmt ")
        {
            if (len != 16) { w.error = "fmt not 16 bytes"; return w; }
            w.tag = static_cast<int> (rd16 (body)); w.channels = static_cast<int> (rd16 (body + 2)); w.rate = static_cast<int> (rd32 (body + 4));
            w.blockAlign = static_cast<int> (rd16 (body + 12)); w.bits = static_cast<int> (rd16 (body + 14));
            if (rd32 (body + 8) != static_cast<std::uint32_t> (w.rate * w.blockAlign)) { w.error = "byte rate"; return w; }
            if (w.blockAlign != w.channels * w.bits / 8) { w.error = "block align"; return w; }
        }
        else if (id == "fact") { w.hasFact = true; w.factFrames = rd32 (body); }
        else if (id == "LIST") w.info.assign (reinterpret_cast<const char*> (body), len);
        else if (id == "data") { data = body; w.dataSize = len; }
        pos += 8 + len + (len & 1);
    }
    if (pos != b.size()) { w.error = "trailing garbage"; return w; }
    if (! data || w.channels == 0) { w.error = "missing chunk"; return w; }
    const std::uint64_t frames = w.dataSize / static_cast<std::uint32_t> (w.blockAlign);
    if (frames * static_cast<std::uint32_t> (w.blockAlign) != w.dataSize) { w.error = "partial frame"; return w; }
    w.ch.assign (static_cast<std::size_t> (w.channels), std::vector<double> (frames));
    w.raw.assign (static_cast<std::size_t> (w.channels), std::vector<std::int32_t> (frames));
    for (std::uint64_t i = 0; i < frames; ++i)
        for (int c = 0; c < w.channels; ++c)
        {
            const unsigned char* p = data + (i * static_cast<std::uint64_t> (w.channels) + static_cast<std::uint64_t> (c)) * static_cast<std::uint64_t> (w.bits / 8);
            double v; std::int32_t iv = 0;
            if (w.tag == 3) { std::uint32_t u = rd32 (p); float fl; std::memcpy (&fl, &u, 4); v = fl; }
            else if (w.bits == 16) { iv = static_cast<std::int16_t> (rd16 (p)); v = iv / 32768.0; }
            else { std::int32_t u = static_cast<std::int32_t> (p[0] | (p[1] << 8) | (p[2] << 16)); if (u & 0x800000) u -= 0x1000000; iv = u; v = u / 8388608.0; }
            w.ch[static_cast<std::size_t> (c)][i] = v; w.raw[static_cast<std::size_t> (c)][i] = iv;
        }
    w.ok = true;
    return w;
}

bool noTempFiles (const fs::path& dir)
{
    for (auto& e : fs::directory_iterator (dir))
        if (e.path().extension() == ".tmp") return false;
    return true;
}

std::string slurp (const fs::path& p) { std::ifstream f (p, std::ios::binary); return { std::istreambuf_iterator<char> (f), std::istreambuf_iterator<char>() }; }

ParamSnapshot patch() { ParamSnapshot p; p[P::weight] = 80.0f; return p; }

ExportSettings baseSettings()
{
    ExportSettings s;
    s.bpm = 140; s.sampleRate = 48000; s.channels = 2; s.tailSeconds = 0.5; s.progressIntervalMs = 0;
    s.metadata = { "Snaral 0.2.0", "Test Preset", "2026-10-20" };
    return s;
}

// ---------------------------------------------------------------- tests
void testConversionAndNaming()
{
    using namespace wav;
    bool clip;
    CHECK (toInt (0.0f, 24, clip) == 0 && ! clip);
    CHECK (toInt (1.0f, 24, clip) == 8388607 && clip);
    CHECK (toInt (-1.0f, 24, clip) == -8388608 && ! clip);
    CHECK (toInt (-1.5f, 24, clip) == -8388608 && clip);
    CHECK (toInt (0.5f, 24, clip) == 4194304);
    CHECK (toInt (1.0f, 16, clip) == 32767 && clip);
    CHECK (toInt (-1.0f, 16, clip) == -32768 && ! clip);
    CHECK (toInt (1.0f / 65536.0f, 16, clip) == 1);  // 0.5 LSB rounds away from zero
    CHECK (toInt (-1.0f / 65536.0f, 16, clip) == -1);

    CHECK (presetSlug ("Foundation") == "foundation");
    CHECK (presetSlug ("  Dark -- Reese!! v2  ") == "dark-reese-v2");
    CHECK (presetSlug ("") == "init" && presetSlug ("!!!") == "init" && presetSlug ("Ünï") == "n");
    CHECK (presetSlug (std::string (100, 'a')).size() == 48);
    CHECK (presetSlug (std::string (47, 'a') + "-bbbb") == std::string (47, 'a'));  // trimmed at 48 then trailing '-' removed
    CHECK (defaultExportFileName ("Foundation", 48000, SampleFormat::Pcm24) == "snaral_foundation_48000_24.wav");
    CHECK (defaultExportFileName ("", 44100, SampleFormat::Pcm16) == "snaral_init_44100_16.wav");
    CHECK (defaultExportFileName ("X", 96000, SampleFormat::Float32) == "snaral_x_96000_32f.wav");

    TempDir d;
    const auto p = d.path() / "a.wav";
    CHECK (uniqueFilePath (p) == p);
    std::ofstream (p) << "x";
    CHECK (uniqueFilePath (p) == d.path() / "a_2.wav");
    std::ofstream (d.path() / "a_2.wav") << "x";
    CHECK (uniqueFilePath (p) == d.path() / "a_3.wav");
}

void testPhrases()
{
    const auto p = builtinPhrase();
    CHECK (p.notes.size() == 8);
    const int steps[8] = { 0, 2, 3, 5, 7, 8, 10, 12 };
    for (int i = 0; i < 8; ++i)
        CHECK (p.notes[static_cast<std::size_t> (i)].note == 36 + steps[i] && p.notes[static_cast<std::size_t> (i)].startBeats == i
               && p.notes[static_cast<std::size_t> (i)].lengthBeats == 0.9 && p.notes[static_cast<std::size_t> (i)].velocity == 100);
    CHECK (std::abs (p.computedEndBeats() - 7.9) < 1e-12);
    CHECK (builtinPhrase (10).notes[0].note == 24 && builtinPhrase (200).notes[0].note == 84 && builtinPhrase (36, 0).notes[0].velocity == 1);
    const auto riff = fourBarRiffPhrase();
    CHECK (riff.endBeats == 16.0 && riff.notes.size() == 20);
    for (auto& n : riff.notes) CHECK (n.note == 36 || n.note == 38 || n.note == 41);
    CHECK (exportPhrasePresets().size() >= 3);
}

void testValidation()
{
    ExportStatus st;
    auto s = baseSettings();
    CHECK (validateExport (s, builtinPhrase(), st).empty());
    auto bad = [&] (ExportSettings x, MidiPhrase ph, ExportStatus want) { const auto m = validateExport (x, ph, st); CHECK (! m.empty() && st == want); };
    auto x = s; x.sampleRate = 22050; bad (x, builtinPhrase(), ExportStatus::InvalidSettings);
    x = s; x.channels = 3; bad (x, builtinPhrase(), ExportStatus::InvalidSettings);
    x = s; x.bpm = 5; bad (x, builtinPhrase(), ExportStatus::InvalidSettings);
    x = s; x.tailSeconds = 31; bad (x, builtinPhrase(), ExportStatus::InvalidSettings);
    x = s; x.tailSeconds = -1; bad (x, builtinPhrase(), ExportStatus::InvalidSettings);
    bad (s, MidiPhrase {}, ExportStatus::InvalidPhrase);
    auto ph = builtinPhrase(); ph.notes[0].velocity = 0; bad (s, ph, ExportStatus::InvalidPhrase);
    ph = builtinPhrase(); ph.notes[0].note = 200; bad (s, ph, ExportStatus::InvalidPhrase);
    ph = builtinPhrase(); ph.endBeats = 100000.0; bad (s, ph, ExportStatus::TooLong);   // 100000 beats at 140 bpm > 600 s
    ph = builtinPhrase(); ph.endBeats = 1400.0; x = s; x.tailSeconds = 30; bad (x, ph, ExportStatus::TooLong); // 600 s + 30 s
    ph = builtinPhrase(); ph.endBeats = 1400.0; x = s; x.tailSeconds = 0; CHECK (validateExport (x, ph, st).empty()); // exactly 600 s
    // frames arithmetic
    CHECK (exportFrames (s, builtinPhrase()) == std::llround ((7.9 * 60.0 / 140.0 + 0.5) * 48000.0));
    CHECK (beatsToSample (1.0, 120.0, 48000) == 24000);
}

void testTimeline()
{
    auto s = baseSettings();
    MidiPhrase p;
    p.notes = { { 0.0, 1.0, 36, 100, 0 }, { 1.0, 1.0, 36, 100, 0 } };  // retrigger at the same sample as note-off
    p.ccs = { { 0.5, 64, 127, 0 }, { 0.75, 74, 20, 0 } };
    p.endBeats = 3.0;
    const auto tl = buildTimeline (s, p);
    std::int64_t prev = -1;
    for (auto& it : tl.items) { CHECK (it.sample >= prev); prev = it.sample; }
    // at sample of beat 1: note-off precedes note-on
    const auto b1 = beatsToSample (1.0, s.bpm, s.sampleRate);
    int offIdx = -1, onIdx = -1;
    for (std::size_t i = 0; i < tl.items.size(); ++i)
        if (tl.items[i].sample == b1 && ! tl.items[i].isCc)
            (tl.items[i].ev.type == EngineMidiEvent::Type::NoteOff ? offIdx : onIdx) = static_cast<int> (i);
    CHECK (offIdx >= 0 && onIdx > offIdx);
    // sustain pedal released at phrase end
    CHECK (tl.items.back().ev.type == EngineMidiEvent::Type::ControlChange && tl.items.back().ev.data1 == 64 && tl.items.back().ev.data2 == 0);
    CHECK (tl.items.back().sample == beatsToSample (3.0, s.bpm, s.sampleRate));
    CHECK (tl.totalSamples == exportFrames (s, p));
}

void testFileFormatsAndRenderMatch()
{
    TempDir dir;
    StubEngine eng;
    OfflineRenderer<StubEngine> r (eng);
    const auto ph = builtinPhrase();
    auto s = baseSettings();

    const auto reference = r.renderToMemory (patch(), ph, s);
    CHECK (reference.status == ExportStatus::Ok);
    CHECK (reference.latencyTrimmed == 64);
    const auto expectFrames = static_cast<std::uint64_t> (exportFrames (s, ph));
    CHECK (reference.left.size() == expectFrames && reference.right.size() == expectFrames);
    CHECK (eng.lastWeight == 80.0f && eng.maxRenderSize <= 512);
    CHECK (eng.timing_.playing && eng.timing_.bpmValid && eng.timing_.bpm == 140.0);
    // onset: first non-zero sample is exactly at the note start (latency trimmed)
    {
        std::size_t first = 0;
        while (first < reference.left.size() && reference.left[first] == 0.0f) ++first;
        CHECK (first == 0);   // note 1 starts at beat 0 -> sample 0
    }

    struct Case { wav::SampleFormat f; int tag, bits; const char* name; };
    for (Case c : { Case { wav::SampleFormat::Pcm24, 1, 24, "p24" }, Case { wav::SampleFormat::Float32, 3, 32, "f32" },
                    Case { wav::SampleFormat::Pcm16, 1, 16, "p16" } })
        for (int rate : { 44100, 48000, 96000 })
            for (int chans : { 1, 2 })
            {
                s.sample = c.f; s.sampleRate = rate; s.channels = chans;
                const auto path = dir.path() / (std::string (c.name) + "_" + std::to_string (rate) + "_" + std::to_string (chans) + ".wav");
                const auto res = r.renderToFile (patch(), ph, s, path);
                CHECK_MSG (res.status == ExportStatus::Ok, res.message);
                const auto w = readWav (path);
                CHECK_MSG (w.ok, w.error);
                CHECK (w.tag == c.tag && w.bits == c.bits && w.channels == chans && w.rate == rate);
                CHECK (w.hasFact == (c.tag == 3));
                if (w.hasFact) CHECK (w.factFrames == w.frames());
                CHECK (w.frames() == static_cast<std::uint64_t> (exportFrames (s, ph)));
                CHECK (res.frames == w.frames());
                CHECK (w.chunkOrder.front() == "fmt " && w.chunkOrder.back() == "data");
                CHECK (w.info.find ("Snaral 0.2.0") != std::string::npos && w.info.find ("Test Preset") != std::string::npos
                       && w.info.find ("2026-10-20") != std::string::npos);
                if (rate == 48000)
                {
                    // compare with the in-memory reference rendered at the same rate (24-bit: within 1 LSB, float exact)
                    s.channels = 2;
                    const auto ref = r.renderToMemory (patch(), ph, s);
                    for (std::uint64_t i = 0; i < w.frames(); ++i)
                    {
                        const double L = ref.left[i], R = ref.right[i];
                        const double want0 = chans == 1 ? 0.5 * (static_cast<float> (L) + static_cast<float> (R)) : L;
                        if (c.tag == 3)
                        {
                            CHECK (w.ch[0][i] == static_cast<double> (static_cast<float> (want0)));
                            if (chans == 2) CHECK (w.ch[1][i] == R);
                        }
                        else
                        {
                            const double lsb = 1.0 / (c.bits == 24 ? 8388608.0 : 32768.0);
                            const double tol = c.bits == 24 ? lsb : 1.5 * lsb; // 16-bit carries +-1 LSB TPDF dither
                            CHECK (std::abs (w.ch[0][i] - want0) <= tol);
                            if (chans == 2) CHECK (std::abs (w.ch[1][i] - R) <= tol);
                        }
                    }
                }
            }
    CHECK (noTempFiles (dir.path()));
}

void testOnsetsAndLatency()
{
    TempDir dir;
    StubEngine eng;
    OfflineRenderer<StubEngine> r (eng);
    auto s = baseSettings();
    s.sample = wav::SampleFormat::Float32; s.sampleRate = 44100; s.bpm = 173;
    MidiPhrase p;
    p.notes = { { 1.0, 0.5, 36, 127, 0 }, { 2.33, 0.25, 40, 127, 0 } };
    const auto res = r.renderToFile (patch(), p, s, dir.path() / "o.wav");
    CHECK (res.status == ExportStatus::Ok);
    const auto w = readWav (dir.path() / "o.wav");
    std::size_t first = 0;
    while (first < w.ch[0].size() && w.ch[0][first] == 0.0) ++first;
    const auto expect = beatsToSample (1.0, 173, 44100);
    CHECK (static_cast<std::int64_t> (first) >= expect - 1 && static_cast<std::int64_t> (first) <= expect + 1);
    CHECK (std::abs (static_cast<double> (expect) - 1.0 * 60.0 / 173.0 * 44100.0) <= 0.5);
    // latency 0 engine gives the same file
    StubEngine e0; e0.latency = 0;
    OfflineRenderer<StubEngine> r0 (e0);
    CHECK (r0.renderToFile (patch(), p, s, dir.path() / "o0.wav").status == ExportStatus::Ok);
    CHECK (slurp (dir.path() / "o.wav") == slurp (dir.path() / "o0.wav"));
}

void testDeterminismAndDither()
{
    TempDir dir;
    StubEngine a, b;
    OfflineRenderer<StubEngine> ra (a), rb (b);
    auto s = baseSettings();
    s.sample = wav::SampleFormat::Pcm16;
    CHECK (ra.renderToFile (patch(), builtinPhrase(), s, dir.path() / "1.wav").status == ExportStatus::Ok);
    s.progressIntervalMs = 100; s.blockSize = 512;
    int reports = 0; float lastFrac = -1; bool mono = true;
    Callbacks cb;
    cb.progress = [&] (const Progress& p) { ++reports; mono &= p.fraction() >= lastFrac; lastFrac = p.fraction(); };
    CHECK (rb.renderToFile (patch(), builtinPhrase(), s, dir.path() / "2.wav", cb).status == ExportStatus::Ok);
    CHECK (slurp (dir.path() / "1.wav") == slurp (dir.path() / "2.wav"));
    CHECK (reports >= 1 && mono && lastFrac == 1.0f);

    // dither: silence stays within +-1 LSB, not all zero, repeatable
    std::vector<float> zeros (48000, 0.0f);
    const float* ptr[1] = { zeros.data() };
    wav::Format f { 48000, 1, wav::SampleFormat::Pcm16 };
    CHECK (wav::writeWavFile (dir.path() / "z1.wav", f, ptr, zeros.size()).status == wav::Status::Ok);
    CHECK (wav::writeWavFile (dir.path() / "z2.wav", f, ptr, zeros.size()).status == wav::Status::Ok);
    CHECK (slurp (dir.path() / "z1.wav") == slurp (dir.path() / "z2.wav"));
    const auto z = readWav (dir.path() / "z1.wav");
    int nonzero = 0, sum = 0;
    for (auto v : z.raw[0]) { CHECK (v >= -1 && v <= 1); nonzero += v != 0; sum += v; }
    CHECK (nonzero > 10000 && std::abs (sum) < 1500);
    // 24-bit and float: no dither
    f.sample = wav::SampleFormat::Pcm24;
    CHECK (wav::writeWavFile (dir.path() / "z3.wav", f, ptr, zeros.size()).status == wav::Status::Ok);
    const auto z3 = readWav (dir.path() / "z3.wav");
    for (auto v : z3.raw[0]) CHECK (v == 0);
}

void testClipCountAndStereoSub()
{
    TempDir dir;
    std::vector<float> l (1000), rr (1000);
    for (int i = 0; i < 1000; ++i) { l[static_cast<std::size_t> (i)] = (i % 100 == 0) ? 1.5f : 0.1f; rr[static_cast<std::size_t> (i)] = l[static_cast<std::size_t> (i)]; }
    l[7] = std::nanf (""); rr[7] = INFINITY;
    const float* ptr[2] = { l.data(), rr.data() };
    wav::WriteStats st;
    CHECK (wav::writeWavFile (dir.path() / "c.wav", { 48000, 2, wav::SampleFormat::Pcm24 }, ptr, 1000, {}, &st).status == wav::Status::Ok);
    CHECK (st.clippedSamples == 20 && st.nonFiniteSamples == 2 && st.peak == 1.5f);
    const auto w = readWav (dir.path() / "c.wav");
    CHECK (w.ok && w.ch[0][0] == 8388607 / 8388608.0 && w.ch[0][7] == 0.0 && w.ch[1][7] == 0.0);
    CHECK (w.chunkOrder == std::vector<std::string> ({ "fmt ", "data" })); // no metadata -> no LIST
    // float output is unclipped
    CHECK (wav::writeWavFile (dir.path() / "cf.wav", { 48000, 2, wav::SampleFormat::Float32 }, ptr, 1000).status == wav::Status::Ok);
    CHECK (readWav (dir.path() / "cf.wav").ch[0][0] == 1.5);
    // odd data size gets a pad byte and still parses (mono 24-bit, 1 frame = 3 bytes)
    CHECK (wav::writeWavFile (dir.path() / "odd.wav", { 44100, 1, wav::SampleFormat::Pcm24 }, ptr, 1).status == wav::Status::Ok);
    const auto odd = readWav (dir.path() / "odd.wav");
    CHECK_MSG (odd.ok, odd.error);
    // invalid formats
    CHECK (wav::writeWavFile (dir.path() / "bad.wav", { 0, 2, wav::SampleFormat::Pcm24 }, ptr, 1).status == wav::Status::InvalidFormat);
    CHECK (wav::writeWavFile (dir.path() / "bad.wav", { 48000, 0, wav::SampleFormat::Pcm24 }, ptr, 1).status == wav::Status::InvalidFormat);
    CHECK (! fs::exists (dir.path() / "bad.wav") && noTempFiles (dir.path()));
}

void testNormaliseAndMono()
{
    TempDir dir;
    StubEngine eng;
    OfflineRenderer<StubEngine> r (eng);
    auto s = baseSettings();
    s.sample = wav::SampleFormat::Float32;
    const auto ph = builtinPhrase();
    CHECK (r.renderToFile (patch(), ph, s, dir.path() / "st.wav").status == ExportStatus::Ok);
    s.channels = 1;
    CHECK (r.renderToFile (patch(), ph, s, dir.path() / "mo.wav").status == ExportStatus::Ok);
    const auto st = readWav (dir.path() / "st.wav"), mo = readWav (dir.path() / "mo.wav");
    double peakSt = 0, peakMo = 0;
    for (auto v : st.ch[0]) peakSt = std::max (peakSt, std::abs (v));
    for (auto v : st.ch[1]) peakSt = std::max (peakSt, std::abs (v));
    for (auto v : mo.ch[0]) { CHECK (std::isfinite (v)); peakMo = std::max (peakMo, std::abs (v)); }
    CHECK (peakMo <= peakSt + 1e-9 && peakMo > 0.01);
    for (std::size_t i = 0; i < st.ch[0].size(); i += 37)
        CHECK (std::abs (mo.ch[0][i] - 0.5 * (st.ch[0][i] + st.ch[1][i])) < 1e-6);

    // normalise: -1.0 dBFS within 0.01 dB, no temp files, for all formats
    for (auto f : { wav::SampleFormat::Float32, wav::SampleFormat::Pcm24, wav::SampleFormat::Pcm16 })
        for (int chans : { 1, 2 })
        {
            s.sample = f; s.channels = chans; s.normalise = true;
            const auto path = dir.path() / ("n" + std::to_string (static_cast<int> (f)) + std::to_string (chans) + ".wav");
            const auto res = r.renderToFile (patch(), ph, s, path);
            CHECK_MSG (static_cast<bool> (res), res.message);
            const auto w = readWav (path);
            double pk = 0;
            for (auto& c : w.ch) for (auto v : c) pk = std::max (pk, std::abs (v));
            const double db = 20.0 * std::log10 (pk);
            CHECK_MSG (std::abs (db + 1.0) < 0.01, std::to_string (db));
            CHECK (std::abs (res.peakDbfs + 1.0) < 0.01);
            CHECK (noTempFiles (dir.path()));
            CHECK (w.frames() == static_cast<std::uint64_t> (exportFrames (s, ph)));
        }
}

void testFailuresLeaveDestinationUntouched()
{
    TempDir dir;
    StubEngine eng;
    OfflineRenderer<StubEngine> r (eng);
    auto s = baseSettings();
    const auto dest = dir.path() / "keep.wav";
    { std::ofstream f (dest, std::ios::binary); f << "ORIGINAL-CONTENT"; }
    const auto original = slurp (dest);

    // cancel (after a few blocks)
    {
        int polls = 0;
        Callbacks cb; cb.cancelled = [&] { return ++polls > 5; };
        const auto res = r.renderToFile (patch(), builtinPhrase(), s, dest, cb);
        CHECK (res.status == ExportStatus::Cancelled && slurp (dest) == original && noTempFiles (dir.path()));
        CHECK (polls <= 7);   // honoured within one block
    }
    // cancel during normalise read-back
    {
        auto sn = s; sn.normalise = true;
        int polls = 0;
        Callbacks cb; cb.cancelled = [&] { return ++polls > 400; };
        const auto res = r.renderToFile (patch(), builtinPhrase(), sn, dest, cb);
        CHECK ((res.status == ExportStatus::Cancelled || res.status == ExportStatus::Ok));
        if (res.status == ExportStatus::Cancelled) CHECK (slurp (dest) == original);
        { std::ofstream f (dest, std::ios::binary); f << "ORIGINAL-CONTENT"; }
        CHECK (noTempFiles (dir.path()) && ! fs::exists (dest.string() + ".snaral-render.tmp"));
    }
    // simulated disk full part-way
    {
        wav::WriterHooks hooks;
        hooks.beforeWrite = [] (std::uint64_t written, std::size_t) { return written < 100000; };
        auto sf = s; sf.writerHooks = &hooks;
        const auto res = r.renderToFile (patch(), builtinPhrase(), sf, dest);
        CHECK (res.status == ExportStatus::DiskFull && ! res.message.empty());
        CHECK (slurp (dest) == original && noTempFiles (dir.path()));
    }
    // failure right before commit
    {
        wav::WriterHooks hooks;
        hooks.beforeCommit = [] { return false; };
        auto sf = s; sf.writerHooks = &hooks;
        const auto res = r.renderToFile (patch(), builtinPhrase(), sf, dest);
        CHECK (res.status == ExportStatus::WriteError && slurp (dest) == original && noTempFiles (dir.path()));
    }
    // unwritable destination: parent "folder" is a regular file
    {
        const auto res = r.renderToFile (patch(), builtinPhrase(), s, dest / "sub" / "x.wav");
        CHECK (res.status != ExportStatus::Ok && ! res.message.empty());
        CHECK (slurp (dest) == original && noTempFiles (dir.path()));
    }
    // destination is a directory
    {
        fs::create_directories (dir.path() / "adir");
        const auto res = r.renderToFile (patch(), builtinPhrase(), s, dir.path() / "adir");
        CHECK (res.status != ExportStatus::Ok && fs::is_directory (dir.path() / "adir") && noTempFiles (dir.path()));
    }
    // engine throws
    {
        StubEngine bad; bad.throwOnRender = true;
        OfflineRenderer<StubEngine> rb (bad);
        const auto res = rb.renderToFile (patch(), builtinPhrase(), s, dest);
        CHECK (res.status == ExportStatus::EngineError && res.message.find ("boom") != std::string::npos);
        CHECK (slurp (dest) == original && noTempFiles (dir.path()));
    }
    // invalid phrase / too long: rejected before the engine is touched
    {
        StubEngine untouched;
        OfflineRenderer<StubEngine> ru (untouched);
        CHECK (ru.renderToFile (patch(), MidiPhrase {}, s, dest).status == ExportStatus::InvalidPhrase);
        auto longPhrase = builtinPhrase(); longPhrase.endBeats = 1.0e6;
        CHECK (ru.renderToFile (patch(), longPhrase, s, dest).status == ExportStatus::TooLong);
        CHECK (untouched.prepareCalls == 0 && slurp (dest) == original && noTempFiles (dir.path()));
    }
    // success replaces atomically
    CHECK (r.renderToFile (patch(), builtinPhrase(), s, dest).status == ExportStatus::Ok);
    CHECK (readWav (dest).ok && noTempFiles (dir.path()));
}

void testMidiLearnInRender()
{
    // CC 20 mapped to "weight": the stub's gain follows weight, so CC 0 silences the first note, CC 127 restores the second.
    StubEngine eng;
    OfflineRenderer<StubEngine> r (eng);
    midi::MidiMapping m; m.target = P::weight; m.cc = 20;
    const auto table = midi::MidiLearnTable::build ({ m });
    auto s = baseSettings();
    s.learnTable = table.get();
    s.tailSeconds = 0.2;
    MidiPhrase p;
    p.ccs = { { 0.0, 20, 0, 0 }, { 2.0, 20, 127, 0 }, { 2.5, 21, 5, 0 } };
    p.notes = { { 0.5, 1.0, 36, 127, 0 }, { 3.0, 1.0, 36, 127, 0 } };
    const auto out = r.renderToMemory (patch(), p, s);
    CHECK (out.status == ExportStatus::Ok);
    double firstPeak = 0, secondPeak = 0;
    const auto split = static_cast<std::size_t> (beatsToSample (2.0, s.bpm, s.sampleRate));
    for (std::size_t i = 0; i < out.left.size(); ++i)
        (i < split ? firstPeak : secondPeak) = std::max (i < split ? firstPeak : secondPeak, static_cast<double> (std::abs (out.left[i])));
    CHECK (firstPeak == 0.0 && secondPeak > 0.2);
    CHECK (eng.midiEventsSeen >= 4 + 1);   // 4 note events + the unmapped CC 21; mapped CC 20 not forwarded
    StubEngine eng2;
    OfflineRenderer<StubEngine> r2 (eng2);
    s.learnTable = nullptr;
    const auto plain = r2.renderToMemory (patch(), p, s);
    double plainFirst = 0;
    for (std::size_t i = 0; i < split; ++i) plainFirst = std::max (plainFirst, static_cast<double> (std::abs (plain.left[i])));
    CHECK (plainFirst > 0.2);              // without a map the CC is just MIDI
    CHECK (eng2.midiEventsSeen == eng.midiEventsSeen + 2);
}

// BUG-009: a mapped CC took effect at the start of the block containing it (up to blockSize-1 samples early).
void testMappedCcIsSampleAccurate()
{
    StubEngine eng;
    eng.latency = 0;
    OfflineRenderer<StubEngine> r (eng);
    midi::MidiMapping m; m.target = P::weight; m.cc = 20;
    const auto table = midi::MidiLearnTable::build ({ m });
    auto s = baseSettings();
    s.learnTable = table.get();
    s.tailSeconds = 0.1;
    MidiPhrase p;
    p.notes = { { 0.0, 4.0, 36, 127, 0 } };
    p.ccs = { { 2.0, 20, 0, 0 } };
    const auto out = r.renderToMemory (patch(), p, s);
    CHECK (out.status == ExportStatus::Ok);
    const auto split = static_cast<std::size_t> (beatsToSample (2.0, s.bpm, s.sampleRate));
    CHECK (split % static_cast<std::size_t> (s.blockSize) != 0);
    double before = 0.0, after = 0.0;
    for (std::size_t i = split - 20; i < split; ++i) before = std::max (before, static_cast<double> (std::abs (out.left[i])));
    for (std::size_t i = split; i < split + 200; ++i) after = std::max (after, static_cast<double> (std::abs (out.left[i])));
    CHECK (before > 0.05);
    CHECK (after == 0.0);
}

// Second-application interoperability: Python's stdlib `wave` (PCM) and a small stdlib RIFF reader
// (IEEE float) decode files written by us and compare against the analytic signal. SciPy adds an
// optional independent check for PCM when installed, but is not a test dependency.
void testPythonInterop()
{
    const char* script = R"PY(
import sys, math, wave
path, kind, rate, ch, frames = sys.argv[1], sys.argv[2], int(sys.argv[3]), int(sys.argv[4]), int(sys.argv[5])
def sig(i, c):
    return (0.5 if c == 0 else 0.25) * math.sin(2*math.pi*440.0*i/rate + 0.3*c)
tol = {'p16': 2.0/32768, 'p24': 2.0/8388608, 'f32': 1e-6}[kind]
if kind == 'f32':
    import struct
    raw = open(path, 'rb').read()
    assert raw[:4] == b'RIFF' and raw[8:12] == b'WAVE', 'float: RIFF/WAVE signature'
    pos, fmt, payload = 12, None, None
    while pos + 8 <= len(raw):
        name, size = struct.unpack_from('<4sI', raw, pos)
        start = pos + 8
        assert start + size <= len(raw), 'float: truncated RIFF chunk'
        if name == b'fmt ': fmt = raw[start:start+size]
        if name == b'data': payload = raw[start:start+size]
        pos = start + size + (size & 1)
    assert fmt is not None and len(fmt) >= 16, 'float: fmt chunk'
    tag, channels, sample_rate, byte_rate, block_align, bits = struct.unpack_from('<HHIIHH', fmt)
    assert tag == 3, 'float: IEEE format tag %d' % tag
    assert channels == ch and sample_rate == rate and bits == 32, 'float: format fields'
    assert block_align == ch * 4 and byte_rate == rate * block_align, 'float: alignment'
    assert payload is not None and len(payload) == frames * ch * 4, 'float: data size'
    for i in range(0, frames, 97):
        for c in range(ch):
            v = struct.unpack_from('<f', payload, (i*ch+c)*4)[0]
            assert abs(v - sig(i, c)) <= tol, 'float: sample %d %d %f %f' % (i, c, v, sig(i, c))
    print('ok stdlib float RIFF')
    sys.exit(0)
if kind != 'f32':
    with wave.open(path, 'rb') as w:
        assert w.getframerate() == rate, 'wave: rate'
        assert w.getnchannels() == ch, 'wave: channels'
        assert w.getnframes() == frames, 'wave: frames %d' % w.getnframes()
        assert w.getsampwidth() == (2 if kind == 'p16' else 3), 'wave: width'
        raw = w.readframes(frames)
    width = 2 if kind == 'p16' else 3
    full = float(1 << (8*width - 1))
    for i in range(0, frames, 97):
        for c in range(ch):
            o = (i*ch + c)*width
            v = int.from_bytes(raw[o:o+width], 'little', signed=True) / full
            assert abs(v - sig(i, c)) <= tol, 'wave: sample %d %d %f %f' % (i, c, v, sig(i, c))
try:
    import numpy as np
    from scipy.io import wavfile
except ImportError:
    print('scipy-missing; stdlib checks passed'); sys.exit(0)
r, d = wavfile.read(path)
assert r == rate, 'scipy: rate'
d = d.reshape(frames, ch) if d.ndim == 1 else d
assert d.shape == (frames, ch), 'scipy: shape %s' % (d.shape,)
if kind == 'f32':
    assert d.dtype == np.float32, 'scipy: dtype %s' % d.dtype
    f = d.astype(np.float64)
elif kind == 'p16':
    assert d.dtype == np.int16; f = d / 32768.0
else:
    assert d.dtype == np.int32, 'scipy: dtype %s' % d.dtype   # 24-bit is returned left-justified in int32
    f = d / 2147483648.0
for c in range(ch):
    ref = np.array([sig(i, c) for i in range(frames)])
    assert np.max(np.abs(f[:, c] - ref)) <= tol, 'scipy: max err %g' % np.max(np.abs(f[:, c] - ref))
print('ok')
)PY";

    if (std::system ("python3 -I -c \"import sys\" > /dev/null 2>&1") != 0)
    {
        std::cout << "SKIPPED python interop: python3 not available\n";
        CHECK (std::getenv ("SNARAL_REQUIRE_PYTHON") == nullptr);
        return;
    }
    TempDir dir;
    const auto scriptPath = dir.path() / "check.py";
    { std::ofstream f (scriptPath); f << script; }

    const int frames = 48000 / 4 + 3;
    bool scipyChecked = false;
    struct Case { wav::SampleFormat f; const char* kind; };
    for (Case c : { Case { wav::SampleFormat::Pcm16, "p16" }, Case { wav::SampleFormat::Pcm24, "p24" }, Case { wav::SampleFormat::Float32, "f32" } })
        for (int rate : { 44100, 96000 })
            for (int chans : { 1, 2 })
            {
                std::vector<std::vector<float>> data (static_cast<std::size_t> (chans), std::vector<float> (static_cast<std::size_t> (frames)));
                for (int ch = 0; ch < chans; ++ch)
                    for (int i = 0; i < frames; ++i)
                        data[static_cast<std::size_t> (ch)][static_cast<std::size_t> (i)] =
                            static_cast<float> ((ch == 0 ? 0.5 : 0.25) * std::sin (2.0 * 3.14159265358979323846 * 440.0 * i / rate + 0.3 * ch));
                const float* ptr[2] = { data[0].data(), data.back().data() };
                const auto path = dir.path() / (std::string (c.kind) + std::to_string (rate) + std::to_string (chans) + ".wav");
                CHECK (wav::writeWavFile (path, { rate, chans, c.f }, ptr, static_cast<std::uint64_t> (frames), { "Snaral", "t", "2026-10-20" }).status == wav::Status::Ok);
                const auto out = dir.path() / "out.txt";
                const std::string cmd = "python3 -I \"" + scriptPath.string() + "\" \"" + path.string() + "\" " + c.kind + " " + std::to_string (rate) + " "
                                        + std::to_string (chans) + " " + std::to_string (frames) + " > \"" + out.string() + "\" 2>&1";
                const int rc = std::system (cmd.c_str());
                const auto text = slurp (out);
                CHECK_MSG (rc == 0, cmd + "\n" + text);
                if (text.find ("scipy-missing") != std::string::npos) { std::cout << "NOTE python interop: scipy missing, stdlib wave only\n"; CHECK (std::string (c.kind) != "f32" || std::getenv ("SNARAL_REQUIRE_PYTHON") == nullptr); }
                else scipyChecked = true;
            }
    std::cout << "python interop: stdlib wave + " << (scipyChecked ? "scipy.io.wavfile" : "(scipy unavailable)") << " decoded all files\n";
}
} // namespace

int main()
{
    testConversionAndNaming();
    testPhrases();
    testValidation();
    testTimeline();
    testFileFormatsAndRenderMatch();
    testOnsetsAndLatency();
    testDeterminismAndDither();
    testClipCountAndStereoSub();
    testNormaliseAndMono();
    testMidiLearnInRender();
    testMappedCcIsSampleAccurate();
    testPythonInterop();
    testFailuresLeaveDestinationUntouched();
    std::cout << "WavExportTests OK\n";
    return 0;
}
