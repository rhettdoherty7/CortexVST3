#include "AnalysisEngine.h"

#include <juce_dsp/juce_dsp.h>

#include <algorithm>
#include <cmath>

namespace cortex
{

namespace
{
    constexpr double pi = 3.14159265358979323846;

    // Band amplitudes are log-compressed before flux so a hit at -30 dB counts nearly as much as one at 0 dB.
    constexpr float logCompression = 100.0f;

    constexpr float kickRetriggerSeconds  = 0.080f;
    constexpr float snareRetriggerSeconds = 0.100f;
    constexpr float hatRetriggerSeconds   = 0.040f;

    double fraction (double x) noexcept { return x - std::floor (x); }
}

AnalysisEngine::AnalysisEngine() = default;
AnalysisEngine::~AnalysisEngine() = default;

void AnalysisEngine::prepare (double newSampleRate)
{
    sampleRate = newSampleRate;
    rateScale  = sampleRate > 100000.0 ? 4 : (sampleRate > 50000.0 ? 2 : 1);
    fftLength  = 2048 * rateScale;
    hopLength  = 512 * rateScale;
    ringMask   = fftLength - 1;
    frameDuration = (double) hopLength / sampleRate;

    int order = 0;
    while ((1 << order) < fftLength)
        ++order;
    fft = std::make_unique<juce::dsp::FFT> (order);

    const auto n = (size_t) fftLength;
    ringL.assign (n, 0.0f);
    ringR.assign (n, 0.0f);
    fftBuffer.assign (n * 2, 0.0f);
    magnitudes.assign (n / 2 + 1, 0.0f);

    window.resize (n);
    for (size_t i = 0; i < n; ++i)
        window[i] = (float) (0.5 - 0.5 * std::cos (2.0 * pi * (double) i / (double) n));

    // K-weighting: high shelf then high-pass, coefficients re-derived for this sample rate.
    {
        const double gainDb = 3.999843853973347, q = 0.7071752369554196, fc = 1681.974450955533;
        const double a = std::pow (10.0, gainDb / 40.0);
        const double w0 = 2.0 * pi * fc / sampleRate;
        const double cosW = std::cos (w0), alpha = std::sin (w0) / (2.0 * q), rootA = std::sqrt (a);
        const double a0 = (a + 1.0) - (a - 1.0) * cosW + 2.0 * rootA * alpha;

        Biquad b;
        b.b0 = a * ((a + 1.0) + (a - 1.0) * cosW + 2.0 * rootA * alpha) / a0;
        b.b1 = -2.0 * a * ((a - 1.0) + (a + 1.0) * cosW) / a0;
        b.b2 = a * ((a + 1.0) + (a - 1.0) * cosW - 2.0 * rootA * alpha) / a0;
        b.a1 = 2.0 * ((a - 1.0) - (a + 1.0) * cosW) / a0;
        b.a2 = ((a + 1.0) - (a - 1.0) * cosW - 2.0 * rootA * alpha) / a0;
        shelf = { b, b };
    }
    {
        const double q = 0.5003270373238773, fc = 38.13547087602444;
        const double w0 = 2.0 * pi * fc / sampleRate;
        const double cosW = std::cos (w0), alpha = std::sin (w0) / (2.0 * q);
        const double a0 = 1.0 + alpha;

        Biquad b;
        b.b0 = (1.0 + cosW) * 0.5 / a0;
        b.b1 = -(1.0 + cosW) / a0;
        b.b2 = (1.0 + cosW) * 0.5 / a0;
        b.a1 = -2.0 * cosW / a0;
        b.a2 = (1.0 - alpha) / a0;
        highpass = { b, b };
    }

    loudnessHistory.assign ((size_t) std::max (1, (int) std::lround (3.0 / frameDuration)), 0.0);

    bands.prepare (sampleRate, fftLength, frameDuration);
    pitch.prepare (sampleRate);

    lowBand   = bandForHz (40.0f, 120.0f);
    bodyBand  = bandForHz (150.0f, 400.0f);
    noiseBand = bandForHz (2000.0f, 6000.0f);
    hatBand   = bandForHz (7000.0f, 16000.0f);

    for (auto* d : { &kickDetector, &bodyDetector, &noiseDetector, &hatDetector, &broadDetector })
        d->prepare (frameDuration);

    stereoCoeff = 1.0f - std::exp (-(float) frameDuration / 0.05f);
    reset();
}

void AnalysisEngine::reset()
{
    std::fill (ringL.begin(), ringL.end(), 0.0f);
    std::fill (ringR.begin(), ringR.end(), 0.0f);
    for (auto* b : { &lowBand, &bodyBand, &noiseBand, &hatBand })
        b->amplitude = b->level = b->flux = b->rise = 0.0f;
    std::fill (loudnessHistory.begin(), loudnessHistory.end(), 0.0);
    writeIndex = hopCounter = 0;
    samplesProcessed = 0;
    sumLL = sumRR = sumLR = sumMid = sumSide = sumKWeighted = 0.0;
    hopPeak = 0.0f;
    loudnessWrite = loudnessCount = 0;

    for (auto& b : shelf)    b.clear();
    for (auto& b : highpass) b.clear();

    bands.reset();
    pitch.reset();
    for (auto* d : { &kickDetector, &bodyDetector, &noiseDetector, &hatDetector, &broadDetector })
        d->reset();

    framesSinceBody = framesSinceNoise = 1000;
    kickCandidate = {};
    snareCandidate = {};
    secondsSinceSnare = 1000.0f;
    kickEnvelope = snareEnvelope = hatEnvelope = 0.0f;
    smoothedWidth = smoothedCorrelation = 0.0f;
    freePpq = 0.0;
    lastBarStartPpq = 0.0;
    pitchActive = false;
}

AnalysisEngine::DetectorBand AnalysisEngine::bandForHz (float loHz, float hiHz) const noexcept
{
    const float binHz   = (float) sampleRate / (float) fftLength;
    const int   lastBin = fftLength / 2;

    DetectorBand b;
    b.lo = std::clamp ((int) std::floor (loHz / binHz), 1, lastBin);
    b.hi = std::clamp ((int) std::ceil (hiHz / binHz), b.lo, lastBin);
    return b;
}

void AnalysisEngine::measure (DetectorBand& b) const noexcept
{
    float sum = 0.0f;
    for (int k = b.lo; k <= b.hi; ++k)
        sum += magnitudes[(size_t) k] * magnitudes[(size_t) k];

    const float amplitude = std::sqrt (sum);
    const float level     = std::log1p (logCompression * amplitude);

    b.rise = std::max (0.0f, amplitude - b.amplitude);
    b.flux = std::max (0.0f, level - b.level);
    b.amplitude = amplitude;
    b.level = level;
}

void AnalysisEngine::process (const float* const* channels, int numChannels, int numSamples,
                              const HostTime& host, const AnalysisSettings& settings, FrameSink& sink) noexcept
{
    if (numChannels < 1 || fft == nullptr)
        return;

    const float* left  = channels[0];
    const float* right = numChannels > 1 ? channels[1] : channels[0];

    const bool wantsPitch = settings.source == SourceType::bass
                         || settings.source == SourceType::vocal
                         || settings.source == SourceType::synth;
    if (wantsPitch && ! pitchActive)
    {
        pitch.reset();
        pitchFrameCounter = 0;
        lastPitchHz = lastPitchConfidence = 0.0f;
    }
    pitchActive = wantsPitch;

    const bool   hostRunning   = host.playing && host.ppq >= 0.0 && host.bpm > 0.0;
    const double ppqPerSample  = hostRunning ? host.bpm / (60.0 * sampleRate) : 0.0;

    for (int i = 0; i < numSamples; ++i)
    {
        const float l = left[i], r = right[i];
        const float mid = 0.5f * (l + r), side = 0.5f * (l - r);

        ringL[(size_t) writeIndex] = l;
        ringR[(size_t) writeIndex] = r;
        writeIndex = (writeIndex + 1) & ringMask;

        sumLL   += (double) l * l;
        sumRR   += (double) r * r;
        sumLR   += (double) l * r;
        sumMid  += (double) mid * mid;
        sumSide += (double) side * side;
        hopPeak  = std::max (hopPeak, std::max (std::abs (l), std::abs (r)));

        const double kl = highpass[0].process (shelf[0].process (l));
        const double kr = highpass[1].process (shelf[1].process (r));
        sumKWeighted += kl * kl + kr * kr;

        if (pitchActive)
            pitch.pushSample (mid);

        ++samplesProcessed;

        if (++hopCounter == hopLength)
        {
            hopCounter = 0;
            emitFrame (hostRunning ? host.ppq + (double) (i + 1) * ppqPerSample : -1.0, host, settings, sink);
        }
    }
}

void AnalysisEngine::emitFrame (double framePpq, const HostTime& host, const AnalysisSettings& settings,
                                FrameSink& sink) noexcept
{
    AnalysisFrame f {};
    const float dt  = (float) frameDuration;
    const float hop = (float) hopLength;

    // --- Spectrum ------------------------------------------------------------------------------
    for (int n = 0; n < fftLength; ++n)
    {
        const auto idx = (size_t) ((writeIndex + n) & ringMask);
        fftBuffer[(size_t) n] = 0.5f * (ringL[idx] + ringR[idx]) * window[(size_t) n];
    }
    std::fill (fftBuffer.begin() + fftLength, fftBuffer.end(), 0.0f);
    fft->performFrequencyOnlyForwardTransform (fftBuffer.data(), true);

    const int   numBins = fftLength / 2 + 1;
    const float norm    = 4.0f / (float) fftLength;   // Hann-windowed full-scale sine -> 1.0

    for (int k = 0; k < numBins; ++k)
        magnitudes[(size_t) k] = fftBuffer[(size_t) k] * norm;

    bands.process (magnitudes.data(), f.bands, f.spectrum);

    // --- Level ---------------------------------------------------------------------------------
    f.samplePos = samplesProcessed;
    f.rms  = (float) std::sqrt ((sumLL + sumRR) / (2.0 * hop));
    f.peak = hopPeak;

    loudnessHistory[(size_t) loudnessWrite] = sumKWeighted / hop;
    loudnessWrite = (loudnessWrite + 1) % (int) loudnessHistory.size();
    loudnessCount = std::min (loudnessCount + 1, (int) loudnessHistory.size());

    double meanSquare = 0.0;
    for (int i = 0; i < loudnessCount; ++i)
        meanSquare += loudnessHistory[(size_t) i];
    meanSquare /= (double) loudnessCount;
    f.lufsShort = (float) std::max (-70.0, -0.691 + 10.0 * std::log10 (meanSquare + 1.0e-12));

    // --- Stereo --------------------------------------------------------------------------------
    {
        const double energy = sumLL * sumRR;
        const float correlation = energy > 1.0e-14 ? (float) (sumLR / std::sqrt (energy)) : 0.0f;
        const double midRms = std::sqrt (sumMid), sideRms = std::sqrt (sumSide);
        const float width = (midRms + sideRms) > 1.0e-7 ? (float) (sideRms / (midRms + sideRms)) : 0.0f;

        smoothedCorrelation += (correlation - smoothedCorrelation) * stereoCoeff;
        smoothedWidth       += (width - smoothedWidth) * stereoCoeff;
        f.correlation = std::clamp (smoothedCorrelation, -1.0f, 1.0f);
        f.stereoWidth = std::clamp (smoothedWidth, 0.0f, 1.0f);
    }

    // --- Brightness ----------------------------------------------------------------------------
    {
        const float binHz = (float) sampleRate / (float) fftLength;
        float weighted = 0.0f, total = 0.0f;
        for (int k = 1; k < numBins; ++k)
        {
            const float p = magnitudes[(size_t) k] * magnitudes[(size_t) k];
            weighted += p * (float) k;
            total    += p;
        }

        if (total > 1.0e-10f)
        {
            const float centroidHz = weighted / total * binHz;
            f.centroid = std::clamp (std::log2 (std::max (centroidHz, 20.0f) / 20.0f) / std::log2 (1000.0f), 0.0f, 1.0f);
        }
    }

    // --- Onsets --------------------------------------------------------------------------------
    for (auto* b : { &lowBand, &bodyBand, &noiseBand, &hatBand })
        measure (*b);

    const float strongestFlux = std::max ({ lowBand.flux, bodyBand.flux, noiseBand.flux, hatBand.flux });
    f.flux = std::clamp (strongestFlux * 0.5f, 0.0f, 1.0f);

    // Every detector runs every frame so their adaptive thresholds stay current.
    const bool kickFired  = kickDetector.process (lowBand.flux, settings.kickSensitivity, kickRetriggerSeconds);
    const bool bodyFired  = bodyDetector.process (bodyBand.flux, settings.snareSensitivity, 0.03f);
    const bool noiseFired = noiseDetector.process (noiseBand.flux, settings.snareSensitivity, 0.03f);
    const bool hatFired   = hatDetector.process (hatBand.flux, settings.hatSensitivity, hatRetriggerSeconds);

    float broadSensitivity = 0.5f;
    if (settings.source == SourceType::kick)  broadSensitivity = settings.kickSensitivity;
    if (settings.source == SourceType::snare) broadSensitivity = settings.snareSensitivity;
    if (settings.source == SourceType::hats)  broadSensitivity = settings.hatSensitivity;
    const bool broadFired = broadDetector.process (strongestFlux, broadSensitivity, hatRetriggerSeconds);

    debug = { lowBand.flux, bodyBand.flux, noiseBand.flux, hatBand.flux,
              kickDetector.threshold(), bodyDetector.threshold(), noiseDetector.threshold(), hatDetector.threshold(),
              lowBand.rise, bodyBand.rise, noiseBand.rise, hatBand.rise };

    framesSinceBody  = bodyFired  ? 0 : std::min (framesSinceBody + 1, 1000);
    framesSinceNoise = noiseFired ? 0 : std::min (framesSinceNoise + 1, 1000);
    secondsSinceSnare = std::min (secondsSinceSnare + dt, 1000.0f);

    float kickStrength = 0.0f, snareStrength = 0.0f, hatStrength = 0.0f;

    switch (settings.source)
    {
        case SourceType::kick:
            // On a single-drum track any onset is that drum.
            if (broadFired) { f.kickHit = true; kickStrength = broadDetector.strength(); }
            break;

        case SourceType::snare:
            if (broadFired) { f.snareHit = true; snareStrength = broadDetector.strength(); }
            break;

        case SourceType::hats:
            if (broadFired) { f.hatHit = true; hatStrength = broadDetector.strength(); }
            break;

        case SourceType::fullMix:
        case SourceType::bass:
        case SourceType::vocal:
        case SourceType::synth:
        {
            // Snare = body (150-400 Hz) and noise (2-6 kHz) firing together, within two frames.
            const bool snareFired = (bodyFired || noiseFired)
                                 && framesSinceBody <= 2 && framesSinceNoise <= 2
                                 && secondsSinceSnare >= snareRetriggerSeconds;

            kickCandidate.update (kickFired, kickDetector.strength(), lowBand.rise, bodyBand.rise);
            snareCandidate.update (snareFired, std::max (bodyDetector.strength(), noiseDetector.strength()),
                                   noiseBand.rise, hatBand.rise);

            // The frame an onset first enters the window is too smeared to tell a kick from a
            // snare unless the low end clearly leads, so a close call waits one frame (~10 ms).
            if (kickCandidate.resolve (kickCandidate.age == 0 ? kickOverBodyImmediate : kickOverBody))
            {
                f.kickHit = true;
                kickStrength = kickCandidate.strength;
            }

            if (snareCandidate.resolve (snareOverHat))
            {
                f.snareHit = true;
                snareStrength = snareCandidate.strength;
                secondsSinceSnare = 0.0f;
            }

            if (hatFired && hatBand.rise >= noiseBand.rise * hatOverNoise)
            {
                f.hatHit = true;
                hatStrength = hatDetector.strength();
            }
            break;
        }
    }

    const auto follow = [dt] (float& envelope, float strength, float releaseSeconds)
    {
        envelope *= std::exp (-dt / std::max (releaseSeconds, 0.005f));
        envelope = std::max (envelope, strength);
        return envelope;
    };

    f.kick  = follow (kickEnvelope,  kickStrength,  settings.kickRelease);
    f.snare = follow (snareEnvelope, snareStrength, settings.snareRelease);
    f.hat   = follow (hatEnvelope,   hatStrength,   settings.hatRelease);

    // --- Pitch ---------------------------------------------------------------------------------
    if (pitchActive)
    {
        // Every other frame (~45 updates a second) keeps the worst case inside the CPU budget.
        if ((pitchFrameCounter++ & 1) == 0)
            pitch.analyse (lastPitchHz, lastPitchConfidence);

        f.pitchHz   = lastPitchHz;
        f.pitchConf = lastPitchConfidence;
    }

    // --- Musical time --------------------------------------------------------------------------
    f.bpm = host.bpm > 0.0 ? host.bpm : 120.0;
    f.ppq = framePpq;
    const double barLength = std::max (0.25, (double) host.timeSigNum * 4.0 / (double) std::max (1, host.timeSigDen));

    if (framePpq >= 0.0)
    {
        freePpq = framePpq;
        lastBarStartPpq = host.barStartPpq;
    }
    else
    {
        freePpq += (double) dt * f.bpm / 60.0;   // keep visuals moving while the host is stopped
    }

    f.beatPhase = (float) fraction (freePpq);
    f.barPhase  = (float) fraction ((freePpq - lastBarStartPpq) / barLength);

    // --- Scope ---------------------------------------------------------------------------------
    {
        const int stride = 4 * rateScale;
        const int start  = writeIndex - 256 * stride;
        for (int i = 0; i < 256; ++i)
        {
            const auto idx = (size_t) ((start + i * stride) & ringMask);
            f.waveL[i] = ringL[idx];
            f.waveR[i] = ringR[idx];
        }
    }

    sink.onFrame (f);

    sumLL = sumRR = sumLR = sumMid = sumSide = sumKWeighted = 0.0;
    hopPeak = 0.0f;
}

} // namespace cortex
