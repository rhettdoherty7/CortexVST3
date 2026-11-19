#include "TestSignals.h"

#include <analysis/AnalysisEngine.h>
#include <analysis/FrameFifo.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <thread>
#include <vector>

using namespace cortex;
using Catch::Approx;

namespace
{

struct Collector : FrameSink
{
    std::vector<AnalysisFrame> frames;
    void onFrame (const AnalysisFrame& f) noexcept override { frames.push_back (f); }
};

std::vector<AnalysisFrame> analyse (const std::vector<float>& left, const std::vector<float>& right, double sampleRate,
                                    int blockSize = 512, AnalysisSettings settings = {}, HostTime host = {})
{
    AnalysisEngine engine;
    engine.prepare (sampleRate);

    Collector collector;
    collector.frames.reserve (left.size() / (size_t) engine.hopSize() + 4);

    for (size_t pos = 0; pos < left.size(); pos += (size_t) blockSize)
    {
        const int n = (int) std::min ((size_t) blockSize, left.size() - pos);
        const float* channels[2] = { left.data() + pos, right.data() + pos };
        HostTime blockHost = host;
        if (host.playing)
            blockHost.ppq = host.ppq + (double) pos * host.bpm / (60.0 * sampleRate);
        engine.process (channels, 2, n, blockHost, settings, collector);
    }

    return collector.frames;
}

std::vector<float> sine (double sampleRate, double hz, double seconds, float amplitude)
{
    std::vector<float> out ((size_t) (sampleRate * seconds));
    for (size_t i = 0; i < out.size(); ++i)
        out[i] = amplitude * (float) std::sin (2.0 * testsignals::pi * hz * (double) i / sampleRate);
    return out;
}

struct Score
{
    int expected = 0, detected = 0, matched = 0;
    double meanLatencyMs = 0.0, maxLatencyMs = 0.0;

    double recall() const     { return expected > 0 ? (double) matched / expected : 1.0; }
    double precision() const  { return detected > 0 ? (double) matched / detected : 1.0; }
};

/** Matches detections to expected onsets. A detection counts if it lands from 5 ms before to
    45 ms after the onset; detections that match an `optional` onset are ignored entirely. */
Score score (const std::vector<int64_t>& expected, std::vector<int64_t> detections, double sampleRate,
             const std::vector<int64_t>& optional = {})
{
    const auto early = (int64_t) (0.005 * sampleRate), late = (int64_t) (0.045 * sampleRate);
    const auto take = [&] (int64_t onset) -> int64_t
    {
        for (auto it = detections.begin(); it != detections.end(); ++it)
        {
            if (*it >= onset - early && *it <= onset + late)
            {
                const auto found = *it;
                detections.erase (it);
                return found;
            }
        }
        return -1;
    };

    Score s;
    s.expected = (int) expected.size();
    double latencySum = 0.0;

    for (auto onset : expected)
    {
        const auto found = take (onset);
        if (found >= 0)
        {
            ++s.matched;
            const double ms = (double) (found - onset) * 1000.0 / sampleRate;
            latencySum += ms;
            s.maxLatencyMs = std::max (s.maxLatencyMs, ms);
        }
    }

    for (auto onset : optional)
        take (onset);

    s.detected = s.matched + (int) detections.size();
    s.meanLatencyMs = s.matched > 0 ? latencySum / s.matched : 0.0;
    return s;
}

struct DrumScores { Score kick, snare, hat; };

DrumScores scorePattern (const testsignals::Pattern& p, int blockSize = 512)
{
    const auto frames = analyse (p.left, p.right, p.sampleRate, blockSize);
    std::vector<int64_t> kicks, snares, hats;

    for (const auto& f : frames)
    {
        if (f.kickHit)  kicks.push_back (f.samplePos);
        if (f.snareHit) snares.push_back (f.samplePos);
        if (f.hatHit)   hats.push_back (f.samplePos);
    }

    return { score (p.kicks, kicks, p.sampleRate),
             score (p.snares, snares, p.sampleRate),
             score (p.hats, hats, p.sampleRate, p.optionalHats) };
}

void requireGood (const char* name, const Score& s)
{
    INFO (name << ": expected " << s.expected << ", detected " << s.detected << ", matched " << s.matched
               << ", mean latency " << s.meanLatencyMs << " ms, max " << s.maxLatencyMs << " ms");
    CHECK (s.recall() >= 0.9);
    CHECK (s.precision() >= 0.9);
    CHECK (s.maxLatencyMs < 30.0);
}

void requireGood (const DrumScores& d)
{
    requireGood ("kick", d.kick);
    requireGood ("snare", d.snare);
    requireGood ("hat", d.hat);
}

} // namespace

// ---------------------------------------------------------------------------------------------

TEST_CASE ("FIFO passes frames in order and reports full/empty", "[fifo]")
{
    SpscFifo<int> fifo (8);
    int value = 0;
    CHECK_FALSE (fifo.pop (value));

    for (int i = 0; i < 7; ++i)
        CHECK (fifo.push (i));
    CHECK_FALSE (fifo.push (99));   // one slot stays empty

    for (int i = 0; i < 7; ++i)
    {
        REQUIRE (fifo.pop (value));
        CHECK (value == i);
    }
    CHECK_FALSE (fifo.pop (value));
}

TEST_CASE ("FIFO survives a producer and consumer on different threads", "[fifo]")
{
    SpscFifo<int> fifo (64);
    constexpr int total = 200000;
    std::vector<int> received;
    received.reserve (total);

    std::thread producer ([&]
    {
        for (int i = 0; i < total;)
            if (fifo.push (i))
                ++i;
    });

    int value = 0;
    while ((int) received.size() < total)
        if (fifo.pop (value))
            received.push_back (value);

    producer.join();

    bool inOrder = true;
    for (int i = 0; i < total; ++i)
        inOrder = inOrder && received[(size_t) i] == i;
    CHECK (inOrder);
}

TEST_CASE ("Silence produces quiet, hit-free frames", "[analysis]")
{
    const std::vector<float> silence (48000, 0.0f);
    const auto frames = analyse (silence, silence, 48000.0);
    REQUIRE (frames.size() == 48000 / 512);

    for (const auto& f : frames)
    {
        CHECK (f.rms == 0.0f);
        CHECK_FALSE ((f.kickHit || f.snareHit || f.hatHit));
        CHECK (f.flux == 0.0f);
    }
}

TEST_CASE ("A sine lands in the right band and spectrum slot", "[analysis]")
{
    const auto tone = sine (48000.0, 1000.0, 1.0, 0.5f);
    const auto frames = analyse (tone, tone, 48000.0);
    const auto& f = frames.back();

    CHECK (f.rms == Approx (0.5f / std::sqrt (2.0f)).margin (0.01));
    CHECK (f.peak == Approx (0.5f).margin (0.01));

    // 1 kHz sits in band 3 (500 Hz - 2 kHz); -6 dBFS on a 60 dB scale is 0.9.
    const auto loudestBand = (int) (std::max_element (f.bands, f.bands + 8) - f.bands);
    CHECK (loudestBand == 3);
    CHECK (f.bands[3] == Approx (0.9f).margin (0.03));

    // Slot i covers 20 * 1000^(i/128) Hz, so 1 kHz is slot ~72.
    const auto loudestSlot = (int) (std::max_element (f.spectrum, f.spectrum + 128) - f.spectrum);
    CHECK (std::abs (loudestSlot - 72) <= 1);

    // Centroid is log-mapped 20 Hz..20 kHz: 1 kHz is log(50)/log(1000) = 0.566.
    CHECK (f.centroid == Approx (0.566f).margin (0.02));
}

TEST_CASE ("Short-term loudness follows BS.1770", "[analysis]")
{
    // A stereo 1 kHz sine at -23 dBFS peak reads -23 LUFS.
    const auto tone = sine (48000.0, 997.0, 4.0, std::pow (10.0f, -23.0f / 20.0f));
    const auto frames = analyse (tone, tone, 48000.0);
    CHECK (frames.back().lufsShort == Approx (-23.0f).margin (0.3));
}

TEST_CASE ("Correlation and width describe the stereo image", "[analysis]")
{
    const auto tone = sine (48000.0, 440.0, 1.0, 0.5f);
    auto inverted = tone;
    for (auto& s : inverted)
        s = -s;

    const auto mono = analyse (tone, tone, 48000.0).back();
    CHECK (mono.correlation == Approx (1.0f).margin (0.01));
    CHECK (mono.stereoWidth == Approx (0.0f).margin (0.01));

    const auto wide = analyse (tone, inverted, 48000.0).back();
    CHECK (wide.correlation == Approx (-1.0f).margin (0.01));
    CHECK (wide.stereoWidth == Approx (1.0f).margin (0.01));
}

TEST_CASE ("Scope data is the most recent audio, oldest first", "[analysis]")
{
    std::vector<float> ramp (4096);
    for (size_t i = 0; i < ramp.size(); ++i)
        ramp[i] = (float) i / 4096.0f;

    const auto f = analyse (ramp, ramp, 48000.0).back();
    CHECK (f.samplePos == 4096);
    CHECK (f.waveL[255] == Approx (ramp[4092]));
    CHECK (f.waveL[0] == Approx (ramp[4096 - 1024]));
    CHECK (f.waveR[100] == Approx (f.waveL[100]));
}

TEST_CASE ("Pitch is tracked only for pitched source types", "[analysis][pitch]")
{
    AnalysisSettings bass;
    bass.source = SourceType::bass;

    for (double hz : { 41.2, 55.0, 110.0, 220.0, 440.0 })
    {
        const auto tone = sine (48000.0, hz, 0.5, 0.5f);
        const auto f = analyse (tone, tone, 48000.0, 512, bass).back();
        INFO ("target " << hz << " Hz, got " << f.pitchHz);
        CHECK (f.pitchHz == Approx (hz).epsilon (0.01));
        CHECK (f.pitchConf > 0.8f);
    }

    const auto tone = sine (48000.0, 110.0, 0.5, 0.5f);
    const auto fullMix = analyse (tone, tone, 48000.0).back();
    CHECK (fullMix.pitchHz == 0.0f);
}

TEST_CASE ("Beat and bar phase follow the host, and free-run without one", "[analysis][time]")
{
    const std::vector<float> silence (48000, 0.0f);

    HostTime host;
    host.playing = true;
    host.ppq = 8.0;
    host.barStartPpq = 8.0;
    host.bpm = 120.0;

    // One second at 120 BPM is two beats: the last frame ends 47616 samples in.
    const auto locked = analyse (silence, silence, 48000.0, 256, {}, host).back();
    const double expectedPpq = 8.0 + 47616.0 / 48000.0 * 2.0;
    CHECK (locked.ppq == Approx (expectedPpq).margin (1.0e-6));
    CHECK (locked.bpm == 120.0);
    CHECK (locked.beatPhase == Approx (expectedPpq - 9.0).margin (1.0e-4));
    CHECK (locked.barPhase == Approx ((expectedPpq - 8.0) / 4.0).margin (1.0e-4));

    const auto frames = analyse (silence, silence, 48000.0);
    CHECK (frames.back().ppq == -1.0);
    CHECK (frames.back().beatPhase > frames[frames.size() - 2].beatPhase);
}

// ---------------------------------------------------------------------------------------------

TEST_CASE ("Four-on-the-floor: kicks, snares and off-beat hats", "[detect]")
{
    requireGood (scorePattern (testsignals::fourOnFloor (48000.0)));
}

TEST_CASE ("Breakbeat: syncopated kicks and ghost snares", "[detect]")
{
    requireGood (scorePattern (testsignals::breakbeat (48000.0)));
}

TEST_CASE ("Trap: sparse kicks, clap, 32nd-note hat rolls", "[detect]")
{
    requireGood (scorePattern (testsignals::trapHats (48000.0)));
}

TEST_CASE ("Detection holds at every sample rate and buffer size", "[detect][rates]")
{
    for (double sampleRate : { 44100.0, 48000.0, 88200.0, 96000.0, 176400.0, 192000.0 })
    {
        const auto pattern = testsignals::fourOnFloor (sampleRate);
        for (int blockSize : { 32, 333, 4096 })
        {
            INFO ("sample rate " << sampleRate << ", block size " << blockSize);
            requireGood (scorePattern (pattern, blockSize));
        }
    }
}

TEST_CASE ("Results do not depend on the buffer size", "[detect][rates]")
{
    const auto pattern = testsignals::breakbeat (48000.0);
    const auto a = analyse (pattern.left, pattern.right, 48000.0, 32);
    const auto b = analyse (pattern.left, pattern.right, 48000.0, 4096);
    REQUIRE (a.size() == b.size());

    bool identical = true;
    for (size_t i = 0; i < a.size(); ++i)
        identical = identical && a[i].kickHit == b[i].kickHit && a[i].snareHit == b[i].snareHit
                 && a[i].hatHit == b[i].hatHit && a[i].kick == b[i].kick && a[i].bands[1] == b[i].bands[1];
    CHECK (identical);
}

TEST_CASE ("On a single-drum track every onset is that drum", "[detect][source]")
{
    const auto pattern = testsignals::fourOnFloor (48000.0);
    AnalysisSettings settings;
    settings.source = SourceType::kick;

    const auto frames = analyse (pattern.left, pattern.right, 48000.0, 512, settings);
    int kicks = 0, others = 0;
    for (const auto& f : frames)
    {
        kicks  += f.kickHit ? 1 : 0;
        others += (f.snareHit || f.hatHit) ? 1 : 0;
    }

    CHECK (others == 0);
    CHECK (kicks >= (int) (pattern.kicks.size() + pattern.hats.size()) - 2);
}

TEST_CASE ("Sensitivity changes how much gets detected", "[detect]")
{
    auto pattern = testsignals::fourOnFloor (48000.0);
    for (auto& s : pattern.left)  s *= 0.02f;   // about -34 dB: quiet enough that low sensitivity misses hits
    for (auto& s : pattern.right) s *= 0.02f;

    const auto count = [&] (float sensitivity)
    {
        AnalysisSettings settings;
        settings.kickSensitivity = settings.snareSensitivity = settings.hatSensitivity = sensitivity;
        int hits = 0;
        for (const auto& f : analyse (pattern.left, pattern.right, 48000.0, 512, settings))
            hits += (f.kickHit ? 1 : 0) + (f.snareHit ? 1 : 0) + (f.hatHit ? 1 : 0);
        return hits;
    };

    CHECK (count (1.0f) >= count (0.5f));
    CHECK (count (0.5f) >= count (0.0f));
    CHECK (count (1.0f) > count (0.0f));
}

// Hidden diagnostic: run with  cortex_tests "[.dump]"  to print per-frame detector values.
TEST_CASE ("Dump detector values", "[.dump]")
{
    for (const auto& p : { testsignals::fourOnFloor (48000.0), testsignals::breakbeat (48000.0) })
    {
        AnalysisEngine engine;
        engine.prepare (48000.0);

        struct Printer : FrameSink
        {
            AnalysisEngine* engine = nullptr;
            void onFrame (const AnalysisFrame& f) noexcept override
            {
                const auto& d = engine->detectorDebug();
                if (std::max ({ d.kickFlux, d.bodyFlux, d.noiseFlux, d.hatFlux }) < 0.02f && ! (f.kickHit || f.snareHit || f.hatHit))
                    return;
                std::printf ("%7.1f ms  %c%c%c  kick %5.2f/%4.2f  body %5.2f/%4.2f  noise %5.2f/%4.2f  hat %5.2f/%4.2f  rise low %.3f body %.3f noise %.3f hat %.3f\n",
                             (double) f.samplePos / 48.0, f.kickHit ? 'K' : '.', f.snareHit ? 'S' : '.', f.hatHit ? 'H' : '.',
                             d.kickFlux, d.kickThreshold, d.bodyFlux, d.bodyThreshold, d.noiseFlux, d.noiseThreshold, d.hatFlux, d.hatThreshold,
                             d.lowRise, d.bodyRise, d.noiseRise, d.hatRise);
            }
        } printer;
        printer.engine = &engine;

        const float* channels[2] = { p.left.data(), p.right.data() };
        engine.process (channels, 2, (int) std::min (p.left.size(), (size_t) (48000 * 30)), {}, {}, printer);

        std::printf ("kicks at:");    for (auto k : p.kicks)  std::printf (" %.0f", (double) k / 48.0);
        std::printf ("\nsnares at:"); for (auto k : p.snares) std::printf (" %.0f", (double) k / 48.0);
        std::printf ("\nhats at:");   for (auto k : p.hats)   std::printf (" %.0f", (double) k / 48.0);
        std::printf ("\n\n");
    }
}

// Hidden benchmark: run with  cortex_tests "[.bench]"  to print the engine's share of real time.
TEST_CASE ("Benchmark the analysis engine", "[.bench]")
{
    const auto p = testsignals::trapHats (48000.0);

    for (const auto source : { SourceType::fullMix, SourceType::bass })
    {
        AnalysisEngine engine;
        engine.prepare (48000.0);
        Collector sink;
        sink.frames.reserve (200000);
        AnalysisSettings settings;
        settings.source = source;

        const int blockSize = 256, passes = 20;
        const auto start = std::chrono::steady_clock::now();
        for (int pass = 0; pass < passes; ++pass)
        {
            for (size_t pos = 0; pos + blockSize <= p.left.size(); pos += blockSize)
            {
                const float* channels[2] = { p.left.data() + pos, p.right.data() + pos };
                engine.process (channels, 2, blockSize, {}, settings, sink);
            }
        }
        const double elapsed = std::chrono::duration<double> (std::chrono::steady_clock::now() - start).count();
        const double audio   = (double) p.left.size() * passes / 48000.0;
        std::printf ("%s: %.3f%% of one core at 48 kHz / 256 samples\n",
                     source == SourceType::fullMix ? "full mix" : "bass (pitch on)", 100.0 * elapsed / audio);
    }
}
