#include "ParametricEQ.h"

#include <cmath>

namespace aurora
{
namespace
{
dsp::BiquadCoeffs makeCoeffs(int type, double fs, double f, double q, double gainDb) noexcept
{
    switch (type)
    {
        case 0: return dsp::BiquadCoeffs::lowPass(fs, f, q);
        case 1: return dsp::BiquadCoeffs::highPass(fs, f, q);
        case 2: return dsp::BiquadCoeffs::bandPass(fs, f, q);
        case 3: return dsp::BiquadCoeffs::notch(fs, f, q);
        case 5: return dsp::BiquadCoeffs::lowShelf(fs, f, q, gainDb);
        case 6: return dsp::BiquadCoeffs::highShelf(fs, f, q, gainDb);
        case 4:
        default: return dsp::BiquadCoeffs::peak(fs, f, q, gainDb);
    }
}
} // namespace

ParametricEQ::ParametricEQ(ModuleId id, juce::AudioProcessorValueTreeState& state)
    : ModuleBase(id)
{
    const auto prefix = instancePrefix(id);
    outputGain.bind(state, prefix + ".output");
    for (int i = 0; i < numBands; ++i)
    {
        const auto b = "b" + juce::String(i + 1);
        auto& band = bands[static_cast<size_t>(i)];
        band.enabled.bind(state, prefix + "." + b + ".on");
        band.type.bind(state, prefix + "." + b + ".type");
        band.freq.bind(state, prefix + "." + b + ".freq");
        band.gain.bind(state, prefix + "." + b + ".gain");
        band.q.bind(state, prefix + "." + b + ".q");
    }
}

void ParametricEQ::prepare(double newSampleRate, int, int numChannels)
{
    sampleRate = newSampleRate;
    channels = juce::jlimit(1, 2, numChannels);
    outputGain.prepare(sampleRate, 20.0, 0.0f);
    for (auto& band : bands)
    {
        band.freq.prepare(sampleRate, 30.0, 1000.0f);
        band.gain.prepare(sampleRate, 20.0, 0.0f);
        band.q.prepare(sampleRate, 20.0, 0.707f);
        for (auto& f : band.filters)
            f.reset();
    }
    reset();
}

void ParametricEQ::reset() noexcept
{
    for (auto& band : bands)
    {
        for (auto& f : band.filters)
            f.reset();
        band.freq.snap(band.freq.target());
        band.gain.snap(band.gain.target());
        band.q.snap(band.q.target());
    }
    outputGain.snap(outputGain.target());
    level.store(-120.0f, std::memory_order_relaxed);
}

void ParametricEQ::process(juce::AudioBuffer<float>& buffer) noexcept
{
    const int n = buffer.getNumSamples();
    const int numCh = juce::jmin(channels, buffer.getNumChannels());
    if (n <= 0 || numCh <= 0)
        return;

    for (auto& band : bands)
    {
        band.freq.updateTarget();
        band.gain.updateTarget();
        band.q.updateTarget();
    }
    outputGain.updateTarget();

    constexpr int subBlock = 32;
    float peak = 0.0f;

    for (int offset = 0; offset < n; offset += subBlock)
    {
        const int count = juce::jmin(subBlock, n - offset);

        std::array<bool, numBands> active {};
        std::array<std::array<dsp::BiquadCoeffs, 2>, numBands> coeffs {};
        outputGain.smoother.skip(count);
        const float outGain = dsp::dbToGain(outputGain.current());

        for (int b = 0; b < numBands; ++b)
        {
            auto& band = bands[static_cast<size_t>(b)];
            band.freq.smoother.skip(count);
            band.gain.smoother.skip(count);
            band.q.smoother.skip(count);
            const int type = band.type.getChoice();
            const bool on = band.enabled.getBool();
            if (type != band.lastType || on != band.lastEnabled)
            {
                for (auto& f : band.filters)
                    f.reset();
                band.lastType = type;
                band.lastEnabled = on;
            }
            active[static_cast<size_t>(b)] = on;
            if (!on)
                continue;

            const double f = juce::jlimit(20.0, sampleRate * 0.49, static_cast<double>(band.freq.current()));
            const double q = juce::jlimit(0.05, 30.0, static_cast<double>(band.q.current()));
            const double g = juce::jlimit(-36.0, 36.0, static_cast<double>(band.gain.current()));
            const auto c = makeCoeffs(type, sampleRate, f, q, g);
            for (int ch = 0; ch < numCh; ++ch)
                coeffs[static_cast<size_t>(b)][static_cast<size_t>(ch)] = c;
        }

        for (int ch = 0; ch < numCh; ++ch)
        {
            auto* data = buffer.getWritePointer(ch);
            for (int b = 0; b < numBands; ++b)
                if (active[static_cast<size_t>(b)])
                    bands[static_cast<size_t>(b)].filters[static_cast<size_t>(ch)].set(
                        coeffs[static_cast<size_t>(b)][static_cast<size_t>(ch)]);

            for (int i = offset; i < offset + count; ++i)
            {
                double x = static_cast<double>(data[i]);
                for (int b = 0; b < numBands; ++b)
                    if (active[static_cast<size_t>(b)])
                        x = bands[static_cast<size_t>(b)].filters[static_cast<size_t>(ch)].process(x);
                const float y = dsp::sanitize(static_cast<float>(x) * outGain);
                data[i] = y;
                peak = juce::jmax(peak, std::abs(y));
            }
        }
    }

    level.store(dsp::gainToDb(peak), std::memory_order_relaxed);
}
} // namespace aurora
