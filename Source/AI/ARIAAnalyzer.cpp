#include "ARIAAnalyzer.h"

#include "Core/PluginProcessor.h"
#include "DSP/DspUtils.h"

#include <algorithm>
#include <cmath>
#include <numeric>

namespace aurora
{
namespace
{
struct BiquadState
{
    dsp::BiquadCoeffs c {};
    double z1 = 0.0, z2 = 0.0;

    void reset() noexcept { z1 = z2 = 0.0; }
    double process(double x) noexcept
    {
        const double y = c.b0 * x + z1;
        z1 = c.b1 * x - c.a1 * y + z2;
        z2 = c.b2 * x - c.a2 * y;
        if (!std::isfinite(y))
        {
            reset();
            return 0.0;
        }
        return y;
    }
};

struct KWeighting
{
    BiquadState shelf, highPass;

    void prepare(double fs)
    {
        const double f0 = 1681.974450955533;
        const double G = 3.999843853973347;
        const double Q = 0.7071752369554196;
        const double K = std::tan(dsp::pi * f0 / fs);
        const double Vh = std::pow(10.0, G / 20.0);
        const double Vb = std::pow(Vh, 0.4996667741545416);
        const double a0 = 1.0 + K / Q + K * K;
        shelf.c = { (Vh + Vb * K / Q + K * K) / a0,
                    2.0 * (K * K - Vh) / a0,
                    (Vh - Vb * K / Q + K * K) / a0,
                    2.0 * (K * K - 1.0) / a0,
                    (1.0 - K / Q + K * K) / a0 };

        const double f1 = 38.13547087602444;
        const double Q1 = 0.5003270373238773;
        const double K1 = std::tan(dsp::pi * f1 / fs);
        const double d0 = 1.0 + K1 / Q1 + K1 * K1;
        highPass.c = { 1.0, -2.0, 1.0,
                       2.0 * (K1 * K1 - 1.0) / d0,
                       (1.0 - K1 / Q1 + K1 * K1) / d0 };
        reset();
    }

    void reset() noexcept
    {
        shelf.reset();
        highPass.reset();
    }

    double process(double x) noexcept { return highPass.process(shelf.process(x)); }
};

float loudnessFromPower(double power)
{
    return static_cast<float>(-0.691 + 10.0 * std::log10(juce::jmax(1.0e-20, power)));
}

float mean(const std::vector<float>& v)
{
    if (v.empty())
        return 0.0f;
    double sum = 0.0;
    for (auto x : v)
        sum += x;
    return static_cast<float>(sum / static_cast<double>(v.size()));
}
} // namespace

ARIAAnalyzer::ARIAAnalyzer(AuroraProcessor& p)
    : owner(p)
{
    juce::ignoreUnused(owner);
}

ARIAAnalyzer::~ARIAAnalyzer()
{
    stop();
}

void ARIAAnalyzer::prepare(double newSampleRate, int maxBlockSize, int numChannels)
{
    juce::ignoreUnused(maxBlockSize);
    stop();
    sampleRate = newSampleRate;
    channels = juce::jlimit(1, 2, numChannels);
    capacityFrames = juce::jmax(static_cast<int>(sampleRate * 2.0), static_cast<int>(sampleRate * 8.0));
    for (auto& b : capture)
    {
        b.setSize(channels, capacityFrames);
        b.clear();
    }
    activeBuffer.store(0, std::memory_order_relaxed);
    frozenBuffer.store(0, std::memory_order_relaxed);
    frozenLength.store(0, std::memory_order_relaxed);
    writePosition.store(0, std::memory_order_relaxed);
    freezeRequested.store(false, std::memory_order_relaxed);
    snapshotReady.store(false, std::memory_order_relaxed);
    hasNewReport.store(false, std::memory_order_relaxed);
    worker = std::jthread([this](std::stop_token token) { run(token); });
}

void ARIAAnalyzer::stop()
{
    if (worker.joinable())
    {
        worker.request_stop();
        worker.join();
    }
    running.store(false, std::memory_order_relaxed);
}

void ARIAAnalyzer::push(const juce::AudioBuffer<float>& buffer) noexcept
{
    if (capacityFrames <= 0 || buffer.getNumSamples() <= 0)
        return;

    if (freezeRequested.exchange(false, std::memory_order_acq_rel))
    {
        const int current = activeBuffer.load(std::memory_order_relaxed);
        frozenBuffer.store(current, std::memory_order_relaxed);
        frozenLength.store(writePosition.load(std::memory_order_relaxed), std::memory_order_relaxed);
        activeBuffer.store(current ^ 1, std::memory_order_relaxed);
        writePosition.store(0, std::memory_order_relaxed);
        snapshotReady.store(true, std::memory_order_release);
    }

    const int active = activeBuffer.load(std::memory_order_relaxed);
    auto& dest = capture[static_cast<size_t>(active)];
    int pos = writePosition.load(std::memory_order_relaxed);
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());

    for (int i = 0; i < n; ++i)
    {
        if (pos >= capacityFrames)
            pos = 0;
        for (int ch = 0; ch < numCh; ++ch)
            dest.setSample(ch, pos, buffer.getSample(ch, i));
        ++pos;
    }
    writePosition.store(pos, std::memory_order_relaxed);
}

void ARIAAnalyzer::requestAnalysis()
{
    if (!worker.joinable() || running.load(std::memory_order_relaxed))
        return;
    hasNewReport.store(false, std::memory_order_relaxed);
    freezeRequested.store(true, std::memory_order_release);
}

bool ARIAAnalyzer::isRunning() const noexcept
{
    return running.load(std::memory_order_relaxed);
}

bool ARIAAnalyzer::hasReport() const noexcept
{
    return hasNewReport.load(std::memory_order_relaxed);
}

ARIAReport ARIAAnalyzer::getReport() const
{
    const std::scoped_lock lock(reportMutex);
    return report;
}

void ARIAAnalyzer::run(std::stop_token token)
{
    while (!token.stop_requested())
    {
        if (snapshotReady.exchange(false, std::memory_order_acq_rel))
        {
            running.store(true, std::memory_order_relaxed);
            analyseSnapshot();
            running.store(false, std::memory_order_relaxed);
        }
        else
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
    }
}

void ARIAAnalyzer::analyseSnapshot()
{
    ARIAReport result;
    const int length = frozenLength.load(std::memory_order_relaxed);
    const auto& source = capture[static_cast<size_t>(frozenBuffer.load(std::memory_order_relaxed))];
    const int numCh = juce::jmin(channels, source.getNumChannels());
    const int minFrames = static_cast<int>(sampleRate * 2.0);

    if (numCh <= 0 || length < minFrames)
    {
        result.diagnosis = "Insuficiente audio capturado. Reproduce al menos 3 segundos y vuelve a analizar.";
        const std::scoped_lock lock(reportMutex);
        report = result;
        hasNewReport.store(true, std::memory_order_release);
        return;
    }

    result.valid = true;
    result.framesAnalysed = length;

    std::vector<float> mono(static_cast<size_t>(length), 0.0f);
    KWeighting kL, kR;
    kL.prepare(sampleRate);
    kR.prepare(sampleRate);

    const int chunkFrames = juce::jmax(1, static_cast<int>(sampleRate * 0.1));
    std::vector<double> chunkPower;
    chunkPower.reserve(static_cast<size_t>(length / chunkFrames + 2));

    double chunkSum = 0.0;
    int chunkCount = 0;
    double sumL = 0.0, sumR = 0.0, sumLR = 0.0;
    double sumSquares = 0.0;
    float samplePeak = 0.0f;

    for (int i = 0; i < length; ++i)
    {
        const float l = source.getSample(0, i);
        const float r = numCh > 1 ? source.getSample(1, i) : l;
        mono[static_cast<size_t>(i)] = 0.5f * (l + r);
        const double kl = kL.process(static_cast<double>(l));
        const double kr = kR.process(static_cast<double>(r));
        const double p = 0.5 * (kl * kl + kr * kr);
        chunkSum += p;
        sumL += static_cast<double>(l) * l;
        sumR += static_cast<double>(r) * r;
        sumLR += static_cast<double>(l) * r;
        sumSquares += 0.5 * (static_cast<double>(l) * l + static_cast<double>(r) * r);
        samplePeak = juce::jmax(samplePeak, std::abs(l), std::abs(r));

        if (++chunkCount >= chunkFrames)
        {
            chunkPower.push_back(chunkSum / static_cast<double>(chunkCount));
            chunkSum = 0.0;
            chunkCount = 0;
        }
    }

    result.samplePeakDb = dsp::gainToDb(samplePeak);
    result.rmsDb = dsp::gainToDb(static_cast<float>(std::sqrt(sumSquares / static_cast<double>(length))));
    result.crestDb = result.samplePeakDb - result.rmsDb;
    result.correlation = sumL > 1.0e-12 && sumR > 1.0e-12
                             ? static_cast<float>(sumLR / std::sqrt(sumL * sumR))
                             : 0.0f;

    std::vector<float> momentary;
    std::vector<float> shortTerm;
    for (int i = 3; i < static_cast<int>(chunkPower.size()); ++i)
    {
        const double p4 = (chunkPower[static_cast<size_t>(i)] + chunkPower[static_cast<size_t>(i - 1)]
                           + chunkPower[static_cast<size_t>(i - 2)] + chunkPower[static_cast<size_t>(i - 3)]) * 0.25;
        momentary.push_back(loudnessFromPower(p4));
        if (i >= 29)
        {
            double p30 = 0.0;
            for (int j = 0; j < 30; ++j)
                p30 += chunkPower[static_cast<size_t>(i - j)];
            shortTerm.push_back(loudnessFromPower(p30 / 30.0));
        }
    }

    if (!momentary.empty())
    {
        result.shortTermLufs = shortTerm.empty() ? momentary.back() : shortTerm.back();
        std::vector<float> gated;
        for (auto v : momentary)
            if (v > -70.0f)
                gated.push_back(v);
        const float ungated = mean(gated);
        std::vector<float> relative;
        for (auto v : gated)
            if (v > ungated - 10.0f)
                relative.push_back(v);
        const float integrated = mean(relative);
        result.integratedLufs = std::isfinite(integrated) ? integrated : -70.0f;

        if (!shortTerm.empty())
        {
            std::vector<float> lraValues;
            for (auto v : shortTerm)
                if (v > -70.0f && v > result.integratedLufs - 20.0f)
                    lraValues.push_back(v);
            if (lraValues.size() > 2)
            {
                std::sort(lraValues.begin(), lraValues.end());
                const size_t lo = static_cast<size_t>(0.10 * static_cast<double>(lraValues.size() - 1));
                const size_t hi = static_cast<size_t>(0.95 * static_cast<double>(lraValues.size() - 1));
                result.lra = lraValues[hi] - lraValues[lo];
            }
        }
    }

    // True peak via 4x FIR oversampling (worker thread; allocation is allowed here).
    try
    {
        juce::dsp::Oversampling<float> os(static_cast<size_t>(numCh), 2,
                                          juce::dsp::Oversampling<float>::filterHalfBandFIREquiripple, true);
        constexpr int blockSize = 512;
        os.initProcessing(blockSize);
        juce::dsp::AudioBlock<const float> full(source);
        float truePeak = 0.0f;
        for (int offset = 0; offset < length; offset += blockSize)
        {
            const int count = juce::jmin(blockSize, length - offset);
            auto sub = full.getSubBlock(static_cast<size_t>(offset), static_cast<size_t>(count));
            auto up = os.processSamplesUp(sub);
            for (size_t ch = 0; ch < up.getNumChannels(); ++ch)
                for (size_t i = 0; i < up.getNumSamples(); ++i)
                    truePeak = juce::jmax(truePeak, std::abs(up.getSample(static_cast<int>(ch), static_cast<int>(i))));
        }
        result.truePeakDb = dsp::gainToDb(truePeak);
    }
    catch (...)
    {
        result.truePeakDb = result.samplePeakDb;
    }

    // Spectrum analysis (Hann, 4096 points, 50% overlap, power average).
    constexpr int fftOrder = 12;
    constexpr int fftSize = 1 << fftOrder;
    juce::dsp::FFT fft(fftOrder);
    std::vector<float> fftData(static_cast<size_t>(fftSize * 2), 0.0f);
    std::vector<float> accumulated(static_cast<size_t>(fftSize / 2), 0.0f);
    int frames = 0;
    for (int offset = 0; offset + fftSize <= length; offset += fftSize / 2)
    {
        for (int i = 0; i < fftSize; ++i)
        {
            const float w = 0.5f - 0.5f * std::cos(static_cast<float>(dsp::twoPi) * static_cast<float>(i) / static_cast<float>(fftSize - 1));
            fftData[static_cast<size_t>(i)] = mono[static_cast<size_t>(offset + i)] * w;
        }
        std::fill(fftData.begin() + fftSize, fftData.end(), 0.0f);
        fft.performFrequencyOnlyForwardTransform(fftData.data());
        for (int i = 1; i < fftSize / 2; ++i)
            accumulated[static_cast<size_t>(i)] += fftData[static_cast<size_t>(i)] * fftData[static_cast<size_t>(i)];
        ++frames;
    }

    if (frames > 0)
    {
        double centroidNum = 0.0, centroidDen = 0.0;
        std::array<float, 31> bandCenters {};
        for (int b = 0; b < 31; ++b)
        {
            const double center = 20.0 * std::pow(2.0, static_cast<double>(b) / 3.0);
            bandCenters[static_cast<size_t>(b)] = static_cast<float>(center);
            const double lo = center / std::pow(2.0, 1.0 / 6.0);
            const double hi = center * std::pow(2.0, 1.0 / 6.0);
            const int binLo = juce::jlimit(1, fftSize / 2 - 1, static_cast<int>(lo * fftSize / sampleRate));
            const int binHi = juce::jlimit(binLo, fftSize / 2 - 1, static_cast<int>(hi * fftSize / sampleRate));
            double power = 0.0;
            for (int i = binLo; i <= binHi; ++i)
                power += accumulated[static_cast<size_t>(i)];
            power /= static_cast<double>(juce::jmax(1, binHi - binLo + 1)) * static_cast<double>(frames);
            result.bandsDb[static_cast<size_t>(b)] = dsp::gainToDb(static_cast<float>(std::sqrt(power)));
        }

        for (int i = 1; i < fftSize / 2; ++i)
        {
            const double f = static_cast<double>(i) * sampleRate / static_cast<double>(fftSize);
            const double p = accumulated[static_cast<size_t>(i)];
            centroidNum += f * p;
            centroidDen += p;
        }
        result.spectralCentroidHz = static_cast<float>(centroidDen > 0.0 ? centroidNum / centroidDen : 0.0);

        std::vector<std::pair<double, double>> points;
        for (int b = 0; b < 31; ++b)
        {
            const double f = bandCenters[static_cast<size_t>(b)];
            const float db = result.bandsDb[static_cast<size_t>(b)];
            if (f >= 100.0 && f <= 10000.0 && db > -90.0f)
                points.emplace_back(std::log2(f), db);
        }
        if (points.size() > 2)
        {
            double sx = 0.0, sy = 0.0, sxx = 0.0, sxy = 0.0;
            for (auto [x, y] : points)
            {
                sx += x; sy += y; sxx += x * x; sxy += x * y;
            }
            const double denom = static_cast<double>(points.size()) * sxx - sx * sx;
            result.spectralTiltDbPerOct = static_cast<float>(denom != 0.0
                                                                 ? (static_cast<double>(points.size()) * sxy - sx * sy) / denom
                                                                 : 0.0);
        }

        float lowMid = 0.0f, sib = 0.0f;
        int lowMidCount = 0, sibCount = 0;
        for (int b = 0; b < 31; ++b)
        {
            const float f = bandCenters[static_cast<size_t>(b)];
            if (f >= 200.0f && f <= 400.0f) { lowMid += result.bandsDb[static_cast<size_t>(b)]; ++lowMidCount; }
            if (f >= 5000.0f && f <= 9000.0f) { sib += result.bandsDb[static_cast<size_t>(b)]; ++sibCount; }
        }
        result.lowMidDb = lowMidCount > 0 ? lowMid / static_cast<float>(lowMidCount) : -120.0f;
        result.sibilanceDb = sibCount > 0 ? sib / static_cast<float>(sibCount) : -120.0f;
    }

    // Deterministic, explainable rules.
    float overall = 0.0f;
    int overallCount = 0;
    for (int b = 0; b < 31; ++b)
    {
        const float f = static_cast<float>(20.0 * std::pow(2.0, static_cast<double>(b) / 3.0));
        if (f >= 100.0f && f <= 10000.0f && result.bandsDb[static_cast<size_t>(b)] > -90.0f)
        {
            overall += result.bandsDb[static_cast<size_t>(b)];
            ++overallCount;
        }
    }
    overall = overallCount > 0 ? overall / static_cast<float>(overallCount) : -60.0f;

    auto addSuggestion = [&result](const juce::String& id, float value, const juce::String& label, bool discrete = false)
    {
        result.suggestions.push_back({ id, value, label, discrete });
    };

    if (result.lowMidDb > overall + 1.5f)
    {
        const float cut = juce::jlimit(0.5f, 4.0f, (result.lowMidDb - overall) * 0.5f);
        result.details.add("Energia 200-400 Hz: " + juce::String(result.lowMidDb, 1) + " dB (" + juce::String(result.lowMidDb - overall, 1) + " dB sobre la media).");
        addSuggestion("eqA.b3.type", 4.0f, "EQ B3: Bell 300 Hz", true);
        addSuggestion("eqA.b3.freq", 300.0f, "EQ B3 frecuencia");
        addSuggestion("eqA.b3.gain", -cut, "EQ B3 corte");
        addSuggestion("eqA.b3.q", 1.0f, "EQ B3 Q");
        addSuggestion("eqA.b3.on", 1.0f, "Activar banda 3", true);
        addSuggestion("eqA.active", 1.0f, "Activar EQ A", true);
    }
    if (result.spectralTiltDbPerOct < -3.5f)
    {
        result.details.add("Balance espectral oscuro: tilt " + juce::String(result.spectralTiltDbPerOct, 2) + " dB/oct.");
        addSuggestion("eqA.b8.type", 6.0f, "EQ B8: High Shelf", true);
        addSuggestion("eqA.b8.freq", 8000.0f, "EQ B8 frecuencia");
        addSuggestion("eqA.b8.gain", juce::jlimit(0.5f, 3.0f, -result.spectralTiltDbPerOct * 0.5f), "EQ B8 realce");
        addSuggestion("eqA.b8.on", 1.0f, "Activar banda 8", true);
        addSuggestion("eqA.active", 1.0f, "Activar EQ A", true);
    }
    if (result.crestDb > 16.0f)
    {
        result.details.add("Crest factor alto: " + juce::String(result.crestDb, 1) + " dB.");
        addSuggestion("compA.active", 1.0f, "Activar Compressor A", true);
        addSuggestion("compA.threshold", -18.0f, "Compressor threshold");
        addSuggestion("compA.ratio", 2.5f, "Compressor ratio");
        addSuggestion("compA.attack", 20.0f, "Compressor attack");
        addSuggestion("compA.release", 150.0f, "Compressor release");
    }
    if (result.sibilanceDb > overall + 3.0f)
    {
        result.details.add("Sibilancia 5-9 kHz elevada: " + juce::String(result.sibilanceDb, 1) + " dB.");
        addSuggestion("dsA.active", 1.0f, "Activar De-Esser", true);
        addSuggestion("dsA.freq", 6500.0f, "De-Esser frecuencia");
        addSuggestion("dsA.threshold", -28.0f, "De-Esser threshold");
        addSuggestion("dsA.ratio", 4.0f, "De-Esser ratio");
        addSuggestion("dsA.range", 8.0f, "De-Esser range");
    }
    if (result.correlation < 0.15f)
    {
        result.details.add("Correlacion estereo baja: " + juce::String(result.correlation, 2) + ".");
        addSuggestion("imgA.active", 1.0f, "Activar Stereo Imager", true);
        addSuggestion("imgA.width", 80.0f, "Reducir width");
    }
    if (result.truePeakDb > -0.5f)
    {
        result.details.add("True peak: " + juce::String(result.truePeakDb, 2) + " dBTP.");
        addSuggestion("masterA.active", 1.0f, "Activar Mastering", true);
        addSuggestion("masterA.ceiling", -1.0f, "Limiter ceiling -1 dBTP");
    }

    if (result.details.isEmpty())
        result.details.add("Balance tonal y dinamica dentro de los margenes de referencia.");

    result.diagnosis = "Analisis deterministico de " + juce::String(length / sampleRate, 2) + " s: "
        + juce::String(result.integratedLufs, 1) + " LUFS integrados, "
        + juce::String(result.truePeakDb, 2) + " dBTP, LRA "
        + juce::String(result.lra, 1) + " LU. "
        + result.details[0];

    const std::scoped_lock lock(reportMutex);
    report = std::move(result);
    hasNewReport.store(true, std::memory_order_release);
}
} // namespace aurora
