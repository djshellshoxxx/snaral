// Micro-benchmark for the engine-output blocks. Build+run: g++ -std=c++20 -O2 -DNDEBUG -ISource tools/bench_output_stage.cpp -o bench_out && ./bench_out
#include "Dsp/Drive.h"
#include "Dsp/Limiter.h"
#include "Dsp/Texture.h"
#include "Dsp/Width.h"
#include <chrono>
#include <cstdio>
#include <algorithm>
using namespace snaral;
using Clk = std::chrono::steady_clock;
template <class F> void bench (const char* name, double fs, int block, F&& f)
{
    const int blocks = static_cast<int> (fs * 6.0 / block);   // 6 s of audio per measurement
    std::vector<double> t; t.reserve (blocks);
    for (int i = 0; i < blocks; ++i) { auto a = Clk::now(); f (i); auto b = Clk::now(); t.push_back (std::chrono::duration<double, std::micro> (b - a).count()); }
    t.erase (t.begin(), t.begin() + blocks / 10);
    std::sort (t.begin(), t.end());
    const double budget = 1.0e6 * block / fs, med = t[t.size() / 2], p99 = t[static_cast<std::size_t> (t.size() * 0.99)];
    std::printf ("%-34s fs %6.0f blk %4d  median %7.2f us (%.3f%% of block)  p99 %7.2f us  worst %7.2f us\n", name, fs, block, med, 100.0 * med / budget, p99, t.back());
}
int main()
{
    for (const double fs : { 48000.0 , 96000.0 })
    for (const int block : { 64, 256, 1024 })
    {
        std::vector<float> buf (static_cast<std::size_t> (block)), r (static_cast<std::size_t> (block));
        for (int i = 0; i < block; ++i) buf[static_cast<std::size_t> (i)] = 0.4f * std::sin (0.07f * static_cast<float> (i));
        for (const auto mode : { OversamplingMode::Off, OversamplingMode::X2, OversamplingMode::X4 })
        {
            Oversampler os; os.prepare (fs, block); os.setMode (mode);
            Drive d; d.prepare (fs, block); DriveParams p; p.amountPercent = 70; d.setParams (p); d.reset();
            char nm[64]; std::snprintf (nm, 64, "drive warm os=%dx (1 mid bus)", oversamplingFactor (mode));
            auto in = buf;
            bench (nm, fs, block, [&] (int) { in = buf; d.process (in.data(), block, &os); });
        }
        TextureLayer tl; tl.prepare (fs, 1); TextureParams tp; tp.noiseLevel = 0.5f; tp.metalLevel = 0.5f; tl.setParams (tp);
        auto in = buf;
        bench ("texture noise+metal (1 voice)", fs, block, [&] (int i) { if (i % 200 == 0) tl.noteOn (36.0f); std::fill (in.begin(), in.end(), 0.0f); tl.addTo (in.data(), block); });
        Width w; w.prepare (fs, block); w.setParams ({ 100.0f }); w.reset();
        bench ("width", fs, block, [&] (int) { w.process (buf.data(), in.data(), r.data(), block); });
        Limiter lim; lim.prepare (fs, block); lim.setParams ({ true, -6.0f }); lim.reset();
        bench ("limiter enabled (stereo, limiting)", fs, block, [&] (int) { for (int i = 0; i < block; ++i) { in[static_cast<std::size_t> (i)] = 1.5f * buf[static_cast<std::size_t> (i)]; r[static_cast<std::size_t> (i)] = in[static_cast<std::size_t> (i)]; } lim.process (in.data(), r.data(), block); });
        lim.setParams ({ false, -6.0f });
        bench ("limiter bypassed (stereo)", fs, block, [&] (int) { for (int i = 0; i < block; ++i) { in[static_cast<std::size_t> (i)] = 1.5f * buf[static_cast<std::size_t> (i)]; r[static_cast<std::size_t> (i)] = in[static_cast<std::size_t> (i)]; } lim.process (in.data(), r.data(), block); });
    }
}
